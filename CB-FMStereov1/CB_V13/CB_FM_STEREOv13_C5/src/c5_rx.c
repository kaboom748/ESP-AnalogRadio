/* SPDX-License-Identifier: GPL-3.0-or-later
 * ESP32-C5 native hardware IQ accumulator. Register format, signed ADC10,
 * Q6 sum scale, 80MS/s clock and positive RF-LO Q orientation are verified
 * by the project's hardware probes of the pinned Espressif libphy path.
 * DSP/filter conventions follow ESP-AnalogTV ESP_VIDEO_V3/FIRMWARE/iq_rx.h.
 */
#include "c5_rx.h"
#include "c5_radio.h"
#include <math.h>
#include <string.h>
#include "esp_attr.h"
#include "esp_cpu.h"
#include "esp_rom_sys.h"
#include "esp_wifi.h"
#include "riscv/rv_utils.h"
#include "soc/soc.h"

#if !CONFIG_IDF_TARGET_ESP32C5
#error "Native IQ capture requires ESP32-C5"
#endif
#if CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ != 240 || CONFIG_PM_ENABLE
#error "Native C5 IQ timing requires a fixed 240 MHz CPU clock"
#endif
#ifndef C5_RX_NATIVE_HQ_IQ_HZ
#define C5_RX_NATIVE_HQ_IQ_HZ 240000u
#endif
#if C5_RX_NATIVE_HQ_IQ_HZ != 240000
#error "First native qualification uses 240000 IQ/s; higher rates require the next filter stage"
#endif
#ifndef C5_RX_EST_SAMPLES
#define C5_RX_EST_SAMPLES 64
#endif
#if C5_RX_EST_SAMPLES == 32
#define IQ_SUM_SHIFT 7u
#elif C5_RX_EST_SAMPLES == 64
#define IQ_SUM_SHIFT 8u
#elif C5_RX_EST_SAMPLES == 128
#define IQ_SUM_SHIFT 9u
#else
#error "Native estimator window must be 32, 64 or 128 samples"
#endif
#ifndef C5_RX_CIRCULAR_FM
#define C5_RX_CIRCULAR_FM 1
#endif

#define CPU_HZ 240000000u
#define SOURCE_RATE 80000000u
#define IQ_WINDOW ((unsigned)C5_RX_EST_SAMPLES)
#define EST_CTL0 0x600a044cu
#define EST_CTL1 0x600a0450u
#define EST_I 0x600a0464u
#define EST_Q 0x600a0468u
#define EST_STATUS 0x600a047cu
#define EST_DONE (1u << 16)
/* Native register acquisition has no RF-dump SRAM/DMA owner.
 * Use the ordinary Arduino/IDF internal-memory layout. */

static DRAM_ATTR int16_t robust[] = {0,22,7,-66,-40,140,127,-239,-314,353,681,-459,-1496,535,5128,7626};
static DRAM_ATTR int16_t mono[] = {3,-2,-21,-57,-106,-151,-167,-120,21,275,637,1074,1528,1927,2201,2300};
static DRAM_ATTR uint16_t angles[257];
static DRAM_ATTR uint32_t reciprocals[256];
static DRAM_ATTR c5_rx_stats_t stats;
static wifi_promiscuous_filter_t quiet_filter;
static bool quiet_saved, estimator_owned;
static volatile bool active;
static uint32_t saved_ctl0, saved_ctl1, estimator;
static DRAM_ATTR struct {
    uint32_t iq_rate, period, decimation, taps, half, phase, index;
    const int16_t *coefficients;
    uint8_t history_index, history_valid;
    bool previous, bad_group;
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
        int selected=(int)c5_radio_rx_gain_index()+step;
        if(selected<0)selected=0;
        if(selected>(int)c5_radio_rx_gain_maximum())selected=(int)c5_radio_rx_gain_maximum();
        if(c5_radio_rx_gain_update((unsigned)selected))++stats.gain_updates;
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

static void IRAM_ATTR base_sample(int32_t mpx, bool valid, int32_t ci, int32_t cq,
                                 int32_t scale, c5_rx_emit_cb_t emit, void *user)
{
    if (!dsp.phase) dsp.bad_group = false;
    if (!valid) dsp.bad_group = true;
    dsp.history_index = (dsp.history_index + 1u) & 127u;
    dsp.history[dsp.history_index] = mpx;
    dsp.history[dsp.history_index + 128u] = mpx;
    if (dsp.history_valid < dsp.taps) ++dsp.history_valid;
    if (++dsp.phase != dsp.decimation) return;
    dsp.phase = 0;
    uint8_t sample = 0x80;
    if (!dsp.bad_group && dsp.history_valid == dsp.taps) {
        int32_t value = ((filter() >> 14) * scale + 32768) >> 16;
        if (value < -127 || value > 127) ++stats.pcm_clipped;
        if (value < -127) value = -127;
        if (value > 127) value = 127;
        sample = (uint8_t)value;
    }
    if (valid && (dsp.index & 7u) == 0u) {
        int32_t mag = (ci < 0 ? -ci : ci) + (cq < 0 ? -cq : cq);
        if (mag > 30000) mag = 30000;
        dsp.signal_q4 += mag - (dsp.signal_q4 >> 4);
    }
    emit(dsp.index++, sample, (uint16_t)(dsp.signal_q4 >> 4), user);
    ++stats.samples;
    if (sample == 0x80) ++stats.invalid;
}

static void IRAM_ATTR process_slot(int32_t i, int32_t q, bool acquired,
                                  int32_t scale, c5_rx_emit_cb_t emit, void *user)
{
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
#if C5_RX_CIRCULAR_FM
            int32_t residual = value - (dsp.frequency_dc >> 12);
            if (residual > 32767) { corrected -= 65536; ++stats.fm_unwraps; }
            else if (residual < -32768) { corrected += 65536; ++stats.fm_unwraps; }
#endif
            dsp.frequency_dc += (corrected * 4096 - dsp.frequency_dc) >> 13;
            dsp.mpx = corrected - (dsp.frequency_dc >> 12);
#if C5_RX_CIRCULAR_FM
            if (dsp.frequency_dc >= 134217728) dsp.frequency_dc -= 268435456;
            else if (dsp.frequency_dc < -134217728) dsp.frequency_dc += 268435456;
#endif
            valid = true;
        }
        dsp.old_i = ci; dsp.old_q = cq; dsp.previous = true;
    } else dsp.previous = false;
    base_sample(dsp.mpx, valid, ci, cq, scale, emit, user);
}

esp_err_t c5_rx_start(uint32_t carrier_hz, int32_t fine_hz, unsigned profile)
{
    if (profile > 2u) return ESP_ERR_INVALID_ARG;
    c5_rx_stop();
    memset(&stats, 0, sizeof(stats)); memset(&dsp, 0, sizeof(dsp));
    dsp.iq_rate = profile ? 192000u : C5_RX_NATIVE_HQ_IQ_HZ;
    dsp.period = CPU_HZ / dsp.iq_rate;
    dsp.decimation = profile == 2u ? 4u : 2u;
    dsp.half = profile ? 15u : 9u; dsp.taps = dsp.half * 2u + 1u;
    dsp.coefficients = profile == 1u ? robust : profile == 2u ? mono : NULL;
    dsp.phase_min = 32767; dsp.phase_max = -32768;
    tables();
    esp_err_t result = c5_radio_receive(carrier_hz, fine_hz);
    if (result != ESP_OK) return result;
    dsp.gain_auto=c5_radio_rx_gain_automatic();
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

void c5_rx_stop(void)
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

void IRAM_ATTR c5_rx_run(c5_rx_emit_cb_t emit, c5_rx_pump_cb_t pump, void *user)
{
    if (!active || !emit || !pump) return;
    const int32_t scale = (128u * dsp.iq_rate + 37500u) / 75000u;
    int32_t pending_i = 0, pending_q = 0;
    bool have_pending = false, pending_valid = false;
    const unsigned irq_state = RV_CLEAR_CSR(mstatus, MSTATUS_MIE);
    uint32_t deadline = cycles() + dsp.period;
    while (active) {
        uint32_t pump_start = cycles();
        if (!pump(user)) break;
        uint32_t pump_cycles = cycles() - pump_start;
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
            /* raw=64*sum(ADC); raw/(4*N) retains four fractional ADC bits.
             * CW proves positive RF-LO native Q: no PARLIO conjugation. */
            pending_i = (int32_t)REG_READ(EST_I) >> IQ_SUM_SHIFT;
            pending_q = (int32_t)REG_READ(EST_Q) >> IQ_SUM_SHIFT;
            if (pending_i < -15000) pending_i = -15000;
            if (pending_i > 15000) pending_i = 15000;
            if (pending_q < -15000) pending_q = -15000;
            if (pending_q > 15000) pending_q = 15000;
            if ((stats.captured & 255u) == 0u) {
                uint32_t ai = (pending_i < 0 ? -pending_i : pending_i) >> 4;
                uint32_t aq = (pending_q < 0 ? -pending_q : pending_q) >> 4;
                if (ai > stats.peak) stats.peak = ai;
                if (aq > stats.peak) stats.peak = aq;
            }
            ++stats.captured;
        } else ++stats.timeouts;
        control(estimator);
        if (REG_READ(EST_STATUS) & EST_DONE) ++stats.clear_still_high;
        if(pending_valid)gain_observe(pending_i,pending_q);
        have_pending = true;
        uint32_t elapsed = cycles() - work_start + pump_cycles;
        stats.read_cycles += elapsed;
        ++stats.profiled_iq; ++stats.read_calls;
        if (elapsed > stats.max_read_cycles) stats.max_read_cycles = elapsed;
        deadline += dsp.period;
    }
    control(estimator);
    RV_SET_CSR(mstatus, irq_state & MSTATUS_MIE);
}

void c5_rx_get_stats(c5_rx_stats_t *result)
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
    result->gain_index=c5_radio_rx_gain_index();
    result->gain_maximum=c5_radio_rx_gain_maximum();
}
