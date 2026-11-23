/* SPDX-License-Identifier: GPL-3.0-or-later
 * ESP32-C6 native hardware IQ accumulator. Register format, signed ADC10,
 * Q6 sum scale and register mapping follow ram_iq_est_enable/dc_iq_est_new
 * in the pinned Arduino-ESP32 3.3.12 C6 libphy. RF orientation and real-time
 * margins are hardware-qualified separately; no RF-dump SRAM is borrowed.
 * DSP/filter conventions follow ESP-AnalogTV ESP_VIDEO_V3/FIRMWARE/iq_rx.h.
 */
#include "c6_rx.h"
#include "c6_rx_pcm.h"
#include "c6_radio.h"
#include <math.h>
#include <string.h>
#include "esp_attr.h"
#include "esp_cpu.h"
#include "esp_rom_sys.h"
#include "esp_wifi.h"
#include "riscv/rv_utils.h"
#include "soc/soc.h"

#if !CONFIG_IDF_TARGET_ESP32C6
#error "Native IQ capture requires ESP32-C6"
#endif
#if CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ != 160 || CONFIG_PM_ENABLE
#error "Native C6 IQ timing requires a fixed 160 MHz CPU clock"
#endif
#ifndef C6_RX_NATIVE_HQ_IQ_HZ
#define C6_RX_NATIVE_HQ_IQ_HZ 240000u
#endif
#if C6_RX_NATIVE_HQ_IQ_HZ != 240000
#error "First native qualification uses 240000 IQ/s; higher rates require the next filter stage"
#endif
#ifndef C6_RX_EST_SAMPLES
#define C6_RX_EST_SAMPLES 64
#endif
#if C6_RX_EST_SAMPLES == 32
#define IQ_SUM_SHIFT 7u
#elif C6_RX_EST_SAMPLES == 64
#define IQ_SUM_SHIFT 8u
#elif C6_RX_EST_SAMPLES == 128
#define IQ_SUM_SHIFT 9u
#else
#error "Native estimator window must be 32, 64 or 128 samples"
#endif
#ifndef C6_RX_CIRCULAR_FM
#define C6_RX_CIRCULAR_FM 1
#endif

#define CPU_HZ 160000000u
#define SOURCE_RATE 80000000u
#define PCM_IQ_RATE 180000u
#define IQ_WINDOW ((unsigned)C6_RX_EST_SAMPLES)
#define EST_CTL0 0x600a0470u
#define EST_CTL1 0x600a0474u
#define EST_I 0x600a0488u
#define EST_Q 0x600a048cu
#define EST_STATUS 0x600a04a0u
#define EST_DONE (1u << 16)
/* Native register acquisition has no RF-dump SRAM/DMA owner.
 * Use the ordinary Arduino/IDF internal-memory layout. */

static DRAM_ATTR int16_t robust[] = {0,22,7,-66,-40,140,127,-239,-314,353,681,-459,-1496,535,5128,7626};
static DRAM_ATTR int16_t mono[] = {3,-2,-21,-57,-106,-151,-167,-120,21,275,637,1074,1528,1927,2201,2300};
static DRAM_ATTR uint16_t angles[257];
static DRAM_ATTR uint32_t reciprocals[256];
static DRAM_ATTR c6_rx_stats_t stats;
static wifi_promiscuous_filter_t quiet_filter;
static bool quiet_saved, estimator_owned;
static volatile bool active;
static uint32_t saved_ctl0, saved_ctl1, estimator;
static DRAM_ATTR struct {
    uint32_t iq_rate, period, period_remainder, period_fraction;
    uint32_t decimation, taps, half, phase, index;
    const int16_t *coefficients;
    uint8_t history_index, history_valid;
    bool previous, bad_group;
    bool pcm, pcm_finish_pending;
    int32_t dc_i, dc_q, frequency_dc, old_i, old_q, mpx, signal_q4;
    int32_t phase_min, phase_max, reported_min, reported_max;
    uint32_t phase_measure_count;
    int32_t history[256];
    uint32_t gain_peak;
    uint16_t gain_count;
    uint8_t gain_rails;
    bool gain_auto;
} dsp;

static inline __attribute__((always_inline)) uint32_t cycles(void)
{ return esp_cpu_get_cycle_count(); }

static inline __attribute__((always_inline)) void control(uint32_t value)
{
    REG_WRITE(EST_CTL1, value);
    __asm__ volatile("fence iorw,iorw" ::: "memory");
}

/* Run only after reading a completed IQ window and lowering its trigger.
 * The gain table includes analog DC corrections. Index updates hold the
 * force level; the existing idle time supplies settling before the next
 * acquisition, without changing a deadline or dropping an IQ slot. */
static inline __attribute__((always_inline)) void gain_observe(int32_t i,int32_t q)
{
    if(!dsp.gain_auto)return;
    const uint32_t ai=i<0?(uint32_t)-i:(uint32_t)i;
    const uint32_t aq=q<0?(uint32_t)-q:(uint32_t)q;
    const uint32_t peak=ai>aq?ai:aq;
    if(peak>dsp.gain_peak)dsp.gain_peak=peak;
    if(peak>=8100u)++dsp.gain_rails;else dsp.gain_rails=0;
    int step=0;
    if(dsp.gain_rails>=4u)step=-6;
    else if(++dsp.gain_count>=1024u) {
        if(dsp.gain_peak>6144u)step=-3;
        else if(dsp.gain_peak>4096u)step=-1;
        else if(dsp.gain_peak<1280u)step=1;
    } else return;
    dsp.gain_rails=0;dsp.gain_count=0;dsp.gain_peak=0;
    if(step) {
        int selected=(int)c6_radio_rx_gain_index()+step;
        if(selected<0)selected=0;
        if(selected>(int)c6_radio_rx_gain_maximum())selected=(int)c6_radio_rx_gain_maximum();
        if(c6_radio_rx_gain_update((unsigned)selected))++stats.gain_updates;
    }
}

static void tables(void)
{
    for (unsigned k = 0; k < 256; ++k) reciprocals[k] = (1u << 24) / (256u + k);
    for (unsigned k = 0; k <= 256; ++k)
        angles[k] = (uint16_t)(atanf(k / 256.0f) * (32768.0f / 3.14159265358979323846f) + 0.5f);
}

static inline __attribute__((always_inline)) uint32_t leading_zeros(uint32_t n)
{
    uint32_t shift = 0;
    if (!(n & 0xffff0000u)) { n <<= 16; shift += 16; }
    if (!(n & 0xff000000u)) { n <<= 8; shift += 8; }
    if (!(n & 0xf0000000u)) { n <<= 4; shift += 4; }
    if (!(n & 0xc0000000u)) { n <<= 2; shift += 2; }
    if (!(n & 0x80000000u)) ++shift;
    return shift;
}

static int32_t IRAM_ATTR discriminate(int32_t i, int32_t q, int32_t pi, int32_t pq)
{
    int32_t dot = i * pi + q * pq, cross = q * pi - i * pq;
    uint32_t a = dot < 0 ? -dot : dot, b = cross < 0 ? -cross : cross;
    uint32_t large = a >= b ? a : b, small = a >= b ? b : a;
    if (!large) return 0;
    uint32_t shift = leading_zeros(large), normalized = large << shift;
    uint32_t ratio = (((small << shift) >> 16) * reciprocals[(normalized >> 23) - 256]) >> 23;
    int32_t angle = angles[ratio > 256 ? 256 : ratio];
    if (b > a) angle = 16384 - angle;
    if (dot < 0) angle = 32768 - angle;
    return cross < 0 ? -angle : angle;
}

static inline __attribute__((always_inline)) int32_t halfband(const int32_t *n)
{
    return 78 * (n[0] + n[-18]) - 265 * (n[-2] + n[-16]) +
           640 * (n[-4] + n[-14]) - 1467 * (n[-6] + n[-12]) +
           5110 * (n[-8] + n[-10]) + 8192 * n[-9];
}

static int32_t IRAM_ATTR filter(void)
{
    const int32_t *n = dsp.history + dsp.history_index + 128u;
    if (!dsp.coefficients) return halfband(n);
    const int32_t *oldest = n - (dsp.taps - 1u);
    int32_t result = 0;
    for (unsigned k = 0; k < dsp.half; ++k)
        result += dsp.coefficients[k] * (*oldest++ + *n--);
    return result + dsp.coefficients[dsp.half] * *n;
}

/* PCM reads IQ at 180 kHz and decodes an exactly 120 kHz MPX stream.
 * Two symmetric 62.5 kHz Hamming-windowed FIR phases produce output at
 * sample offsets 0 and 1.5 within a three-input group, delayed by six
 * samples. Center/nearest coefficients close the Q14 unity-gain sums. */
static inline __attribute__((always_inline)) int32_t pcm_filter(const int32_t *n, bool half)
{
    if (half) {
        return 64 * (n[0] + n[-13]) - 67 * (n[-1] + n[-12]) -
               123 * (n[-2] + n[-11]) + 702 * (n[-3] + n[-10]) -
               1077 * (n[-4] + n[-9]) - 400 * (n[-5] + n[-8]) +
               9093 * (n[-6] + n[-7]);
    }
    return 35 * (n[0] + n[-12]) - 147 * (n[-1] + n[-11]) +
           260 * (n[-2] + n[-10]) + 243 * (n[-3] + n[-9]) -
           1885 * (n[-4] + n[-8]) + 4004 * (n[-5] + n[-7]) +
           11364 * n[-6];
}

static void IRAM_ATTR base_sample(int32_t mpx, bool valid, int32_t ci, int32_t cq,
                                 int32_t scale, c6_rx_emit_cb_t emit, void *user)
{
    if (!valid) dsp.bad_group = true;
    dsp.history_index = (dsp.history_index + 1u) & 127u;
    dsp.history[dsp.history_index] = mpx;
    dsp.history[dsp.history_index + 128u] = mpx;
    if (dsp.history_valid < dsp.taps) ++dsp.history_valid;
    const unsigned phase = dsp.phase;
    if (++dsp.phase == dsp.decimation) dsp.phase = 0;
    if (dsp.pcm ? phase == 1u : dsp.phase != 0u) return;
    const bool group_valid = !dsp.bad_group && dsp.history_valid == dsp.taps;
    dsp.bad_group = false;
    if (valid && (dsp.index & 7u) == 0u) {
        int32_t mag = (ci < 0 ? -ci : ci) + (cq < 0 ? -cq : cq);
        if (mag > 30000) mag = 30000;
        dsp.signal_q4 += mag - (dsp.signal_q4 >> 4);
    }
    const uint16_t strength = (uint16_t)(dsp.signal_q4 >> 4);
    if (dsp.pcm) {
        const int32_t *history = dsp.history + dsp.history_index + 128u;
        const int32_t filtered = group_valid ? pcm_filter(history, phase == 2u) >> 14 : 0;
        /* At 180 kHz, phase deltas times 0.6 map to Q14 at 75 kHz deviation. */
        int32_t audio = (filtered * 2458 + 2048) >> 12;
        if (audio < -16383 || audio > 16383) ++stats.pcm_clipped;
        audio = audio < -16383 ? -16383 : audio > 16383 ? 16383 : audio;
        dsp.pcm_finish_pending = c6_rx_pcm_sample(audio, group_valid, strength);
    } else {
        uint8_t sample = 0x80;
        if (group_valid) {
            const int32_t filtered = filter() >> 14;
            int32_t value = (filtered * scale + 32768) >> 16;
            if (value < -127 || value > 127) ++stats.pcm_clipped;
            if (value < -127) value = -127;
            if (value > 127) value = 127;
            sample = (uint8_t)value;
        }
        emit(dsp.index, sample, strength, user);
    }
    ++dsp.index;
    ++stats.samples;
    if (!group_valid) ++stats.invalid;
}

static void IRAM_ATTR process_slot(int32_t i, int32_t q, bool acquired,
                                  int32_t scale, c6_rx_emit_cb_t emit, void *user)
{
    if (dsp.pcm_finish_pending) {
        dsp.pcm_finish_pending = false;
        c6_rx_pcm_finish();
    }
    int32_t ci = 0, cq = 0;
    bool valid = false;
    if (acquired) {
        dsp.dc_i += (i * 65536 - dsp.dc_i) >> 12;
        dsp.dc_q += (q * 65536 - dsp.dc_q) >> 12;
        ci = i - (dsp.dc_i >> 16); cq = q - (dsp.dc_q >> 16);
        if (dsp.previous) {
            int32_t value = discriminate(ci, cq, dsp.old_i, dsp.old_q);
            if (value < dsp.phase_min) dsp.phase_min = value;
            if (value > dsp.phase_max) dsp.phase_max = value;
            if (++dsp.phase_measure_count == 4096u) {
                dsp.reported_min = dsp.phase_min; dsp.reported_max = dsp.phase_max;
                dsp.phase_min = 32767; dsp.phase_max = -32768;
                dsp.phase_measure_count = 0;
            }
            int32_t corrected = value;
#if C6_RX_CIRCULAR_FM
            int32_t residual = value - (dsp.frequency_dc >> 12);
            if (residual > 32767) { corrected -= 65536; ++stats.fm_unwraps; }
            else if (residual < -32768) { corrected += 65536; ++stats.fm_unwraps; }
#endif
            dsp.frequency_dc += (corrected * 4096 - dsp.frequency_dc) >> 13;
            dsp.mpx = corrected - (dsp.frequency_dc >> 12);
#if C6_RX_CIRCULAR_FM
            if (dsp.frequency_dc >= 134217728) dsp.frequency_dc -= 268435456;
            else if (dsp.frequency_dc < -134217728) dsp.frequency_dc += 268435456;
#endif
            valid = true;
        }
        dsp.old_i = ci; dsp.old_q = cq; dsp.previous = true;
    } else dsp.previous = false;
    base_sample(dsp.mpx, valid, ci, cq, scale, emit, user);
}

static esp_err_t start_receive(uint32_t carrier_hz, int32_t fine_hz,
                               unsigned profile, bool pcm)
{
    if (profile > 2u) return ESP_ERR_INVALID_ARG;
    c6_rx_stop();
    memset(&stats, 0, sizeof(stats)); memset(&dsp, 0, sizeof(dsp));
    dsp.pcm = pcm;
    dsp.iq_rate = pcm ? PCM_IQ_RATE : profile ? 192000u : C6_RX_NATIVE_HQ_IQ_HZ;
    dsp.period = CPU_HZ / dsp.iq_rate;
    dsp.period_remainder = CPU_HZ % dsp.iq_rate;
    dsp.decimation = pcm ? 3u : profile == 2u ? 4u : 2u;
    dsp.half = profile ? 15u : 9u;
    dsp.taps = pcm ? 14u : dsp.half * 2u + 1u;
    dsp.coefficients = profile == 1u ? robust : profile == 2u ? mono : NULL;
    dsp.phase_min = 32767; dsp.phase_max = -32768;
    tables();
    if (pcm) c6_rx_pcm_prepare(profile, discriminate);
    esp_err_t result = c6_radio_receive(carrier_hz, fine_hz);
    if (result != ESP_OK) return result;
    dsp.gain_auto=c6_radio_rx_gain_automatic();
    quiet_saved = esp_wifi_get_promiscuous_filter(&quiet_filter) == ESP_OK;
    wifi_promiscuous_filter_t quiet = {.filter_mask = 0};
    (void)esp_wifi_set_promiscuous_filter(&quiet);
    saved_ctl0 = REG_READ(EST_CTL0); saved_ctl1 = REG_READ(EST_CTL1);
    estimator_owned = true;
    control(saved_ctl1 & ~2u); esp_rom_delay_us(1);
    control(saved_ctl1 & ~3u);
    REG_WRITE(EST_CTL0, (saved_ctl0 & 0xf3ffffffu) | 0x04000000u);
    estimator = (saved_ctl1 & ~(0x00180000u | 0x0001fffcu | 3u)) |
                0x00100000u | ((IQ_WINDOW - 1u) << 2) | 1u;
    control(estimator);
    stats.iq_window_samples = IQ_WINDOW;
    stats.iq_rate_hz = dsp.iq_rate; stats.source_rate_hz = SOURCE_RATE;
    active = true;
    return ESP_OK;
}

esp_err_t c6_rx_start(uint32_t carrier_hz, int32_t fine_hz, unsigned profile)
{
    return start_receive(carrier_hz, fine_hz, profile, false);
}

esp_err_t c6_rx_start_pcm(uint32_t carrier_hz, int32_t fine_hz, unsigned profile)
{
    return start_receive(carrier_hz, fine_hz, profile, true);
}

void c6_rx_stop(void)
{
    active = false;
    if (estimator_owned) {
        control(REG_READ(EST_CTL1) & ~2u); esp_rom_delay_us(1);
        control(REG_READ(EST_CTL1) & ~1u);
        REG_WRITE(EST_CTL0, saved_ctl0); control(saved_ctl1);
        estimator_owned = false;
    }
    if (quiet_saved) {
        (void)esp_wifi_set_promiscuous_filter(&quiet_filter);
        quiet_saved = false;
    }
}

static void IRAM_ATTR run_receive(c6_rx_emit_cb_t emit, c6_rx_pump_cb_t pump, void *user)
{
    const int32_t scale = (128u * dsp.iq_rate + 37500u) / 75000u;
    int32_t pending_i = 0, pending_q = 0;
    bool have_pending = false, pending_valid = false;
    const unsigned irq_state = RV_CLEAR_CSR(mstatus, MSTATUS_MIE);
    uint32_t deadline = cycles() + dsp.period;
    dsp.period_fraction=dsp.period_remainder;
    while (active) {
#if C6_RX_PROFILE
        uint32_t pump_start = cycles();
#endif
        if (!pump(user)) break;
#if C6_RX_PROFILE
        uint32_t pump_cycles = cycles() - pump_start;
#endif
        uint32_t now;
        do { now = cycles(); } while ((int32_t)(now - deadline) < 0);
        uint32_t late = now - deadline;
        if (late > stats.max_late_cycles) stats.max_late_cycles = late;
        if (late > dsp.period / 4u) ++stats.late_slots;
        if (late >= dsp.period) {
            ++stats.overruns;
            if (have_pending) {
                process_slot(pending_i, pending_q, pending_valid, scale, emit, user);
                have_pending = false;
            }
            do {
                process_slot(0, 0, false, scale, emit, user);
                ++stats.skipped;
                deadline += dsp.period;
                dsp.period_fraction += dsp.period_remainder;
                if(dsp.period_fraction>=dsp.iq_rate) {
                    dsp.period_fraction-=dsp.iq_rate;++deadline;
                }
            } while ((int32_t)(now - deadline) >= (int32_t)dsp.period);
        }
        const uint32_t work_start = cycles();
        control(estimator | 2u);
        if (have_pending)
            process_slot(pending_i, pending_q, pending_valid, scale, emit, user);
        uint32_t status;
        do {
            status = REG_READ(EST_STATUS);
            if (status & EST_DONE) break;
        } while (cycles() - work_start < dsp.period);
        pending_valid = (status & EST_DONE) != 0;
        if (pending_valid) {
            /* Pinned dc_iq_est_new uses raw>>6 then /(N+1). For N64,
             * raw>>8 retains four fractional bits in that native DC scale.
             * The native Q orientation is checked against the RF fixture;
             * no PARLIO packing or conjugation is performed here. */
            pending_i = (int32_t)REG_READ(EST_I) >> IQ_SUM_SHIFT;
            pending_q = (int32_t)REG_READ(EST_Q) >> IQ_SUM_SHIFT;
            /* The unsigned range test is equivalent to both signed bounds
             * for every int32 input, including abnormal register values.
             * The common legal-input path uses one branch per axis. */
            if ((uint32_t)pending_i+15000u>30000u)
                pending_i=pending_i<0?-15000:15000;
            if ((uint32_t)pending_q+15000u>30000u)
                pending_q=pending_q<0?-15000:15000;
            if ((stats.captured & 255u) == 0u) {
                uint32_t ai = (pending_i < 0 ? -pending_i : pending_i) >> 4;
                uint32_t aq = (pending_q < 0 ? -pending_q : pending_q) >> 4;
                if (ai > stats.peak) stats.peak = ai;
                if (aq > stats.peak) stats.peak = aq;
            }
            ++stats.captured;
        } else ++stats.timeouts;
        control(estimator);
#if C6_RX_PROFILE
        if (REG_READ(EST_STATUS) & EST_DONE) ++stats.clear_still_high;
#else
        /* Spread this non-control readback away from the256-IQ frame edge.
         * The trigger's ordered MMIO clear itself still happens every IQ. */
        if ((stats.captured & 255u)==128u && (REG_READ(EST_STATUS)&EST_DONE))
            ++stats.clear_still_high;
#endif
        if(pending_valid)gain_observe(pending_i,pending_q);
        have_pending = true;
#if C6_RX_PROFILE
        uint32_t elapsed = cycles() - work_start + pump_cycles;
        stats.read_cycles += elapsed;
        ++stats.profiled_iq; ++stats.read_calls;
        if (elapsed > stats.max_read_cycles) stats.max_read_cycles = elapsed;
#endif
        deadline += dsp.period;
        dsp.period_fraction += dsp.period_remainder;
        if(dsp.period_fraction>=dsp.iq_rate) {
            dsp.period_fraction-=dsp.iq_rate;++deadline;
        }
    }
    if (dsp.pcm_finish_pending) {
        dsp.pcm_finish_pending = false;
        c6_rx_pcm_finish();
    }
    control(estimator);
    RV_SET_CSR(mstatus, irq_state & MSTATUS_MIE);
}

void c6_rx_run(c6_rx_emit_cb_t emit, c6_rx_pump_cb_t pump, void *user)
{
    if (active && !dsp.pcm && emit && pump) run_receive(emit, pump, user);
}

void c6_rx_run_pcm(uint8_t *frame, c6_rx_pcm_commit_cb_t commit,
                   c6_rx_pump_cb_t pump, void *user)
{
    if (!active || !dsp.pcm || !frame || !commit || !pump) return;
    c6_rx_pcm_set_sink(frame, commit, user);
    run_receive(NULL, pump, user);
}

void c6_rx_get_stats(c6_rx_stats_t *result)
{
    if (!result) return;
    *result = stats;
    result->mean_cycles_per_iq = stats.profiled_iq ?
        (uint32_t)(stats.read_cycles / stats.profiled_iq) : 0;
    result->frequency_hz = (int32_t)((int64_t)(dsp.frequency_dc >> 12) * dsp.iq_rate / 65536);
    result->dc_i_adc = (dsp.dc_i >> 16) / 16;
    result->dc_q_adc = (dsp.dc_q >> 16) / 16;
    result->discriminator_min_hz = (int32_t)((int64_t)dsp.reported_min * dsp.iq_rate / 65536);
    result->discriminator_max_hz = (int32_t)((int64_t)dsp.reported_max * dsp.iq_rate / 65536);
    result->gain_index=c6_radio_rx_gain_index();
    result->gain_maximum=c6_radio_rx_gain_maximum();
}
