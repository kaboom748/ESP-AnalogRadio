/* SPDX-License-Identifier: GPL-3.0-or-later
 * Classic ESP32 native hardware IQ accumulator. Q6 signed sum scale and
 * register mapping follow ram_iq_est_enable/ram_dc_iq_est in the pinned
 * Arduino-ESP32 3.3.12 classic libphy. RF orientation and real-time
 * margins are hardware-qualified separately; no RF-dump SRAM is borrowed.
 * DSP/filter conventions follow ESP-AnalogTV ESP_VIDEO_V3/FIRMWARE/iq_rx.h.
 */
#include "e32_rx.h"
#include "e32_radio.h"
#include "arduino_runtime.h"
#include <math.h>
#include <string.h>
#include "esp_attr.h"
#include "esp_cpu.h"
#include "esp_rom_sys.h"
#include "esp_wifi.h"
#include "soc/soc.h"
#include "soc/dport_access.h"

#if !CONFIG_IDF_TARGET_ESP32
#error "Native IQ capture requires ESP32"
#endif
#if CONFIG_PM_ENABLE
#error "Native ESP32 IQ timing requires dynamic frequency scaling disabled"
#endif
#ifndef E32_RX_NATIVE_HQ_IQ_HZ
#define E32_RX_NATIVE_HQ_IQ_HZ 240000u
#endif
#if E32_RX_NATIVE_HQ_IQ_HZ != 240000
#error "First native qualification uses 240000 IQ/s; higher rates require the next filter stage"
#endif
#ifndef E32_RX_EST_SAMPLES
#define E32_RX_EST_SAMPLES 64
#endif
#if E32_RX_EST_SAMPLES == 32
#define IQ_SUM_SHIFT 7u
#elif E32_RX_EST_SAMPLES == 64
#define IQ_SUM_SHIFT 8u
#elif E32_RX_EST_SAMPLES == 128
#define IQ_SUM_SHIFT 9u
#else
#error "Native estimator window must be 32, 64 or 128 samples"
#endif
#ifndef E32_RX_CIRCULAR_FM
#define E32_RX_CIRCULAR_FM 1
#endif
/* Native mean scale used by the slow gain controller. This affects gain
 * thresholds only, never IQ bit depth or estimator output. The signed
 * sum-to-mean conversion above remains lossless apart from four retained
 * fractional bits. Keep a bench override for checking the classic ADC
 * scale against the calibrated native gain table. */
#ifndef E32_RX_ADC_BITS
#define E32_RX_ADC_BITS 10
#endif
#if E32_RX_ADC_BITS < 8 || E32_RX_ADC_BITS > 12
#error "Invalid native ADC scale"
#endif
#define GAIN_SCALE ((1u << (E32_RX_ADC_BITS + 3u)))

#define CPU_HZ 240000000u
#define SOURCE_RATE 80000000u
#define IQ_WINDOW ((unsigned)E32_RX_EST_SAMPLES)
#define EST_CTL0 0x3ff46060u
#define EST_CTL1 0x3ff4607cu
#define EST_I 0x3ff460dcu
#define EST_Q 0x3ff460e0u
#define EST_POWER 0x3ff460e4u
/* Classic ESP32's DONE is bit31 of control1, not the C6's separate
 * status bit16. Lowering trigger2 clears DONE for the next window. */
#define EST_STATUS EST_CTL1
#define EST_DONE (1u << 31)
#define ADC_CTRL 0x3ff460a0u
#define ADC_ON_FORCE ((1u << 9) | (1u << 8))
/* Native register acquisition has no RF-dump SRAM/DMA owner.
 * Use the ordinary Arduino/IDF internal-memory layout. */

static DRAM_ATTR int16_t robust[] = {0,22,7,-66,-40,140,127,-239,-314,353,681,-459,-1496,535,5128,7626};
static DRAM_ATTR int16_t mono[] = {3,-2,-21,-57,-106,-151,-167,-120,21,275,637,1074,1528,1927,2201,2300};
static DRAM_ATTR uint16_t angles[257];
static DRAM_ATTR uint32_t reciprocals[256];
static DRAM_ATTR e32_rx_stats_t stats;
#if CB13_BENCH_IQ
#define BENCH_IQ_PAIRS 4096u
static DRAM_ATTR int16_t bench_iq[BENCH_IQ_PAIRS][2];
static DRAM_ATTR uint32_t bench_iq_count, bench_iq_next;
static DRAM_ATTR unsigned bench_window=IQ_WINDOW;
static DRAM_ATTR unsigned bench_window_shift=IQ_SUM_SHIFT;
static bool bench_power_agc_enabled=true;
#endif
static wifi_promiscuous_filter_t quiet_filter;
static bool quiet_saved, estimator_owned;
static volatile bool active;
static uint32_t saved_ctl0, saved_ctl1, saved_adc_force, estimator;
static DRAM_ATTR struct {
    uint32_t iq_rate, period, period_remainder, period_fraction;
    uint32_t decimation, taps, half, phase, index;
    const int16_t *coefficients;
    uint8_t history_index, history_valid;
    bool previous, bad_group;
    int32_t dc_i, dc_q, frequency_dc, old_i, old_q, mpx, signal_q4;
    int32_t last_phase;
    int32_t phase_min, phase_max, reported_min, reported_max;
    uint32_t phase_measure_count;
    int32_t history[256];
    uint32_t gain_peak;
    uint16_t gain_count;
    uint8_t gain_rails, gain_settle_left;
    bool gain_auto;
#if CB13_BENCH_IQ
    bool gain_power;
#endif
} dsp;

static inline __attribute__((always_inline)) uint32_t cycles(void)
{ return esp_cpu_get_cycle_count(); }

static inline __attribute__((always_inline)) void control(uint32_t value)
{
    REG_WRITE(EST_CTL1, value);
    __asm__ volatile("memw" ::: "memory");
}

/* Run only after reading a completed IQ window and lowering its trigger.
 * The gain table includes analog DC corrections. Index updates hold the
 * force level. Mark the next two acquisitions for phase continuity while
 * RF/DC settles, without changing a deadline or dropping an IQ slot. */
#if CB13_BENCH_IQ
static inline __attribute__((always_inline)) void gain_observe(int32_t i,int32_t q)
{
    if(!dsp.gain_auto)return;
    const uint32_t ai=i<0?(uint32_t)-i:(uint32_t)i;
    const uint32_t aq=q<0?(uint32_t)-q:(uint32_t)q;
    const uint32_t peak=ai>aq?ai:aq;
    if(peak>dsp.gain_peak)dsp.gain_peak=peak;
    if(peak>=GAIN_SCALE*8100u/8192u)++dsp.gain_rails;else dsp.gain_rails=0;
    int step=0;
    if(dsp.gain_rails>=4u)step=-6;
    else if(++dsp.gain_count>=1024u) {
        if(dsp.gain_peak>GAIN_SCALE*3u/4u)step=-3;
        else if(dsp.gain_peak>GAIN_SCALE/2u)step=-1;
        else if(dsp.gain_peak<GAIN_SCALE*5u/32u)step=1;
    } else return;
    dsp.gain_rails=0;dsp.gain_count=0;dsp.gain_peak=0;
    if(step) {
        int selected=(int)e32_radio_rx_gain_index()+step;
        if(selected<0)selected=0;
        if(selected>(int)e32_radio_rx_gain_maximum())selected=(int)e32_radio_rx_gain_maximum();
        if(e32_radio_rx_gain_update((unsigned)selected)) {
            ++stats.gain_updates;
            dsp.gain_settle_left=2u;
        }
    }
}
#endif

/* E4 measures sum(ADC-I squared + ADC-Q squared)/8 across the full
 * native window. It does not cancel an out-of-band component like the
 * complex window mean. Input Q8 matches the square of a Q4 mean unit. */
static inline __attribute__((always_inline)) void gain_observe_power(uint32_t power)
{
    if(!dsp.gain_auto)return;
    const uint32_t rails=GAIN_SCALE*8100u/8192u;
    const uint32_t high=GAIN_SCALE*3u/4u;
    const uint32_t half=GAIN_SCALE/2u;
    const uint32_t low=GAIN_SCALE*5u/32u;
    if(power>dsp.gain_peak)dsp.gain_peak=power;
    if(power>=rails*rails)++dsp.gain_rails;else dsp.gain_rails=0;
    int step=0;
    if(dsp.gain_rails>=4u)step=-6;
    else if(++dsp.gain_count>=1024u) {
        if(dsp.gain_peak>high*high)step=-3;
        else if(dsp.gain_peak>half*half)step=-1;
        else if(dsp.gain_peak<low*low)step=1;
    } else return;
    dsp.gain_rails=0;dsp.gain_count=0;dsp.gain_peak=0;
    if(step) {
        int selected=(int)e32_radio_rx_gain_index()+step;
        if(selected<0)selected=0;
        if(selected>(int)e32_radio_rx_gain_maximum())selected=(int)e32_radio_rx_gain_maximum();
        if(e32_radio_rx_gain_update((unsigned)selected)) {
            ++stats.gain_updates;
            dsp.gain_settle_left=2u;
        }
    }
}

static void tables(void)
{
    for (unsigned k = 0; k < 256; ++k) reciprocals[k] = (1u << 24) / (256u + k);
    for (unsigned k = 0; k <= 256; ++k)
        angles[k] = (uint16_t)(atanf(k / 256.0f) * (32768.0f / 3.14159265358979323846f) + 0.5f);
}

static int32_t IRAM_ATTR discriminate(int32_t i, int32_t q, int32_t pi, int32_t pq)
{
    int32_t dot = i * pi + q * pq, cross = q * pi - i * pq;
    uint32_t a = dot < 0 ? -dot : dot, b = cross < 0 ? -cross : cross;
    uint32_t large = a >= b ? a : b, small = a >= b ? b : a;
    if (!large) return 0;
    /* Xtensa LX6 has a native NSAU instruction; large is nonzero here. */
    uint32_t shift = (uint32_t)__builtin_clz(large), normalized = large << shift;
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
                                 int32_t scale, e32_rx_emit_cb_t emit, void *user)
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

static void IRAM_ATTR process_slot(int32_t i, int32_t q, bool acquired, bool gain_settle,
                                  int32_t scale, e32_rx_emit_cb_t emit, void *user)
{
    int32_t ci = 0, cq = 0;
    bool valid = false;
    if (acquired) {
        dsp.dc_i += (i * 65536 - dsp.dc_i) >> 12;
        dsp.dc_q += (q * 65536 - dsp.dc_q) >> 12;
        ci = i - (dsp.dc_i >> 16); cq = q - (dsp.dc_q >> 16);
        if (dsp.previous) {
            /* The flag belongs to this acquisition, not to the gain state
             * at processing time: acquisition N is already pending when
             * its successor's gain is changed. Keep N intact; hold only
             * N+1 and N+2. Refresh old_i/q below even during this hold. */
            int32_t value;
            if(gain_settle) {
                value=dsp.last_phase;
                ++stats.gain_held_slots;
            } else {
                value=discriminate(ci,cq,dsp.old_i,dsp.old_q);
                dsp.last_phase=value;
            }
            if (value < dsp.phase_min) dsp.phase_min = value;
            if (value > dsp.phase_max) dsp.phase_max = value;
            if (++dsp.phase_measure_count == 4096u) {
                dsp.reported_min = dsp.phase_min; dsp.reported_max = dsp.phase_max;
                dsp.phase_min = 32767; dsp.phase_max = -32768;
                dsp.phase_measure_count = 0;
            }
            int32_t corrected = value;
#if E32_RX_CIRCULAR_FM
            int32_t residual = value - (dsp.frequency_dc >> 12);
            if (residual > 32767) { corrected -= 65536; ++stats.fm_unwraps; }
            else if (residual < -32768) { corrected += 65536; ++stats.fm_unwraps; }
#endif
            dsp.frequency_dc += (corrected * 4096 - dsp.frequency_dc) >> 13;
            dsp.mpx = corrected - (dsp.frequency_dc >> 12);
#if E32_RX_CIRCULAR_FM
            if (dsp.frequency_dc >= 134217728) dsp.frequency_dc -= 268435456;
            else if (dsp.frequency_dc < -134217728) dsp.frequency_dc += 268435456;
#endif
            valid = true;
        }
        dsp.old_i = ci; dsp.old_q = cq; dsp.previous = true;
    } else dsp.previous = false;
    base_sample(dsp.mpx, valid, ci, cq, scale, emit, user);
}

esp_err_t e32_rx_start(uint32_t carrier_hz, int32_t fine_hz, unsigned profile)
{
    if (profile > 2u) return ESP_ERR_INVALID_ARG;
    e32_rx_stop();
    memset(&stats, 0, sizeof(stats)); memset(&dsp, 0, sizeof(dsp));
#if CB13_BENCH_IQ
    bench_iq_count = bench_iq_next = 0;
    dsp.gain_power=bench_power_agc_enabled;
#endif
    dsp.iq_rate = profile ? 192000u : E32_RX_NATIVE_HQ_IQ_HZ;
    dsp.period = CPU_HZ / dsp.iq_rate;
    dsp.period_remainder = CPU_HZ % dsp.iq_rate;
    dsp.decimation = profile == 2u ? 4u : 2u;
    dsp.half = profile ? 15u : 9u; dsp.taps = dsp.half * 2u + 1u;
    dsp.coefficients = profile == 1u ? robust : profile == 2u ? mono : NULL;
    dsp.phase_min = 32767; dsp.phase_max = -32768;
    tables();
    esp_err_t result = e32_radio_receive(carrier_hz, fine_hz);
    if (result != ESP_OK) return result;
    dsp.gain_auto=e32_radio_rx_gain_automatic();
    quiet_saved = esp_wifi_get_promiscuous_filter(&quiet_filter) == ESP_OK;
    wifi_promiscuous_filter_t quiet = {.filter_mask = 0};
    (void)esp_wifi_set_promiscuous_filter(&quiet);
    saved_ctl0 = DPORT_REG_READ(EST_CTL0);
    saved_ctl1 = DPORT_REG_READ(EST_CTL1) & ~EST_DONE;
    /* Forcing RX clocks alone leaves converter gating enabled. Keep the
     * ADC active throughout streaming and restore only our two bits. */
    const uint32_t adc=DPORT_REG_READ(ADC_CTRL);
    saved_adc_force=adc&ADC_ON_FORCE;
    REG_WRITE(ADC_CTRL,adc|ADC_ON_FORCE);
    __asm__ volatile("memw":::"memory");
    esp_rom_delay_us(2);
    estimator_owned = true;
    control(saved_ctl1 & ~2u); esp_rom_delay_us(1);
    control(saved_ctl1 & ~3u);
    REG_WRITE(EST_CTL0, (saved_ctl0 & 0xf3ffffffu) | 0x04000000u);
#if CB13_BENCH_IQ
    const unsigned capture_window=bench_window;
#else
    const unsigned capture_window=IQ_WINDOW;
#endif
    estimator = (saved_ctl1 & ~(EST_DONE | 0x00180000u | 0x0001fffcu | 3u)) |
                0x00100000u | ((capture_window - 1u) << 2) | 1u;
    control(estimator);
    stats.iq_window_samples = capture_window;
    stats.iq_rate_hz = dsp.iq_rate; stats.source_rate_hz = SOURCE_RATE;
    active = true;
    return ESP_OK;
}

void e32_rx_stop(void)
{
    active = false;
    if (estimator_owned) {
        control(DPORT_REG_READ(EST_CTL1) & ~(EST_DONE|2u)); esp_rom_delay_us(1);
        control(DPORT_REG_READ(EST_CTL1) & ~(EST_DONE|1u));
        REG_WRITE(EST_CTL0, saved_ctl0); control(saved_ctl1);
        REG_WRITE(ADC_CTRL,(DPORT_REG_READ(ADC_CTRL)&~ADC_ON_FORCE)|saved_adc_force);
        __asm__ volatile("memw":::"memory");
        estimator_owned = false;
    }
    if (quiet_saved) {
        (void)esp_wifi_set_promiscuous_filter(&quiet_filter);
        quiet_saved = false;
    }
}

void IRAM_ATTR e32_rx_run(e32_rx_emit_cb_t emit, e32_rx_pump_cb_t pump, void *user)
{
    if (!active || !emit || !pump) return;
    const int32_t scale = (128u * dsp.iq_rate + 37500u) / 75000u;
    int32_t pending_i = 0, pending_q = 0;
    bool have_pending = false, pending_valid = false, pending_gain_settle = false;
#if CB13_BENCH_IQ
    const unsigned sum_shift=bench_window_shift;
#else
    const unsigned sum_shift=IQ_SUM_SHIFT;
#endif
    const unsigned power_shift=13u-sum_shift;
    uint32_t pending_power=0;
    /* The dual-core runtime parks the peer CPU before masking local
     * interrupts. Everything below is internal-memory DSP and MMIO only. */
    const uint32_t stream_token = e32_stream_enter();
    uint32_t deadline = cycles() + dsp.period;
    dsp.period_fraction=dsp.period_remainder;
    while (active) {
#if E32_RX_PROFILE
        uint32_t pump_start = cycles();
#endif
        if (!pump(user)) break;
#if E32_RX_PROFILE
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
                process_slot(pending_i,pending_q,pending_valid,pending_gain_settle,scale,emit,user);
                have_pending = false;
            }
            do {
                process_slot(0,0,false,false,scale,emit,user);
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
            process_slot(pending_i,pending_q,pending_valid,pending_gain_settle,scale,emit,user);
        uint32_t status;
        do {
            status = DPORT_SEQUENCE_REG_READ(EST_STATUS);
            if (status & EST_DONE) break;
        } while (cycles() - work_start < dsp.period);
        pending_valid = (status & EST_DONE) != 0;
        pending_gain_settle=false;
        if (pending_valid) {
            /* Pinned ram_dc_iq_est uses raw>>6 then /N. For N64,
             * raw>>8 retains four fractional bits in that native DC scale.
             * The native Q orientation is checked against the RF fixture;
             * no PARLIO packing or conjugation is performed here. */
            pending_i = (int32_t)DPORT_SEQUENCE_REG_READ(EST_I) >> sum_shift;
            pending_q = (int32_t)DPORT_SEQUENCE_REG_READ(EST_Q) >> sum_shift;
            /* Read before lowering this window's trigger, so energy and
             * signed I/Q refer to the same completed acquisition. */
            const uint32_t raw_power=DPORT_SEQUENCE_REG_READ(EST_POWER);
#if CB13_BENCH_IQ
            stats.bench_power_sum_raw+=raw_power;
            if(raw_power>stats.bench_power_max_raw)stats.bench_power_max_raw=raw_power;
#endif
            pending_power=raw_power>(UINT32_MAX>>power_shift)?UINT32_MAX:raw_power<<power_shift;
            /* Consume pre-existing settling state before gain_observe can
             * arm a new transition at the end of this acquisition. */
            pending_gain_settle=dsp.gain_settle_left!=0u;
            if(pending_gain_settle)--dsp.gain_settle_left;
            /* The unsigned range test is equivalent to both signed bounds
             * for every int32 input, including abnormal register values.
             * The common legal-input path uses one branch per axis. */
            if ((uint32_t)pending_i+15000u>30000u)
                pending_i=pending_i<0?-15000:15000;
            if ((uint32_t)pending_q+15000u>30000u)
                pending_q=pending_q<0?-15000:15000;
#if CB13_BENCH_IQ
            /* Last valid native IQ, before its next-slot DC/FM processing.
             * No copy, allocation, lock or serial call in this hot path. */
            const uint32_t slot = bench_iq_next;
            bench_iq[slot][0] = (int16_t)pending_i;
            bench_iq[slot][1] = (int16_t)pending_q;
            bench_iq_next = (slot + 1u) & (BENCH_IQ_PAIRS - 1u);
            if (bench_iq_count < BENCH_IQ_PAIRS) ++bench_iq_count;
#endif
            if ((stats.captured & 255u) == 0u) {
                uint32_t ai = (pending_i < 0 ? -pending_i : pending_i) >> 4;
                uint32_t aq = (pending_q < 0 ? -pending_q : pending_q) >> 4;
                if (ai > stats.peak) stats.peak = ai;
                if (aq > stats.peak) stats.peak = aq;
            }
            ++stats.captured;
        } else ++stats.timeouts;
        control(estimator);
#if E32_RX_PROFILE
        if (DPORT_SEQUENCE_REG_READ(EST_STATUS) & EST_DONE) ++stats.clear_still_high;
#else
        /* Spread this non-control readback away from the256-IQ frame edge.
         * The trigger's ordered MMIO clear itself still happens every IQ. */
        if ((stats.captured & 255u)==128u && (DPORT_SEQUENCE_REG_READ(EST_STATUS)&EST_DONE))
            ++stats.clear_still_high;
#endif
        /* A switching transient must not trigger another gain decision. */
        if(pending_valid&&!pending_gain_settle) {
#if CB13_BENCH_IQ
            if(!dsp.gain_power)gain_observe(pending_i,pending_q);
            else
#endif
            gain_observe_power(pending_power);
        }
        have_pending = true;
#if E32_RX_PROFILE
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
    control(estimator);
#if CB13_BENCH_IQ
    /* Read only once after capture, while the hardware still has its live
     * RX setup and the peer remains parked. No per-IQ transport or cost. */
    stats.bench_gain_word=DPORT_SEQUENCE_REG_READ(0x3ff5c02cu);
    stats.bench_pbus_word=DPORT_SEQUENCE_REG_READ(0x3ff460a8u);
    /* Classic PBUS(1,2), bits26:18, carries the applied BB gain.
     * PBUS(1,1) in the low A8 bits is the RX power/control word. */
    stats.bench_bb_word=DPORT_SEQUENCE_REG_READ(0x3ff460acu);
    stats.bench_iq_word=DPORT_SEQUENCE_REG_READ(0x3ff450dcu);
    stats.bench_adc_word=DPORT_SEQUENCE_REG_READ(0x3ff460a0u);
    stats.bench_debug_word=DPORT_SEQUENCE_REG_READ(0x3ff46094u);
    stats.bench_mode_word=DPORT_SEQUENCE_REG_READ(0x3ff4609cu);
#endif
    e32_stream_leave(stream_token);
}

void e32_rx_get_stats(e32_rx_stats_t *result)
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
    result->gain_index=e32_radio_rx_gain_index();
    result->gain_maximum=e32_radio_rx_gain_maximum();
#if CB13_BENCH_IQ
    if(stats.captured&&stats.iq_window_samples) {
        const uint64_t samples=(uint64_t)stats.captured*stats.iq_window_samples;
        result->bench_power_mean_adc2=(uint32_t)(stats.bench_power_sum_raw*8u/samples);
        result->bench_power_max_adc2=(uint32_t)((uint64_t)stats.bench_power_max_raw*8u/stats.iq_window_samples);
    }
#endif
}

#if CB13_BENCH_IQ
void e32_rx_bench_power_agc(bool enabled)
{
    bench_power_agc_enabled=enabled;
}

bool e32_rx_bench_window(unsigned n)
{
    if(active)return false;
    unsigned shift;
    if(n==32u)shift=7u;
    else if(n==64u)shift=8u;
    else if(n==128u)shift=9u;
    else return false;
    bench_window=n;
    bench_window_shift=shift;
    return true;
}

esp_err_t IRAM_ATTR e32_rx_bench_timing(e32_rx_bench_timing_t out[4])
{
    if(!out)return ESP_ERR_INVALID_ARG;
    if(!active||!estimator_owned)return ESP_ERR_INVALID_STATE;
    uint32_t totals[4]={0,0,0,0};
    memset(out,0,4u*sizeof(*out));
    const uint32_t timeout=CPU_HZ/10000u;
    const uint32_t token=e32_stream_enter();
    unsigned n=32u;
    for(unsigned entry=0;entry<4u;++entry,n<<=1) {
        const uint32_t word=(estimator&~0x0001fffcu)|((n-1u)<<2);
        out[entry].n=n;
        out[entry].min_cycles=UINT32_MAX;
        for(unsigned attempt=0;attempt<102u;++attempt) {
            control(word);
            /* A new edge must start a new window. This untimed clear
             * check prevents a stale DONE from shortening the result. */
            const uint32_t clear_start=cycles();
            uint32_t status;
            do {
                status=DPORT_SEQUENCE_REG_READ(EST_STATUS);
                if(!(status&EST_DONE))break;
            } while(cycles()-clear_start<timeout);
            bool completed=false;
            uint32_t elapsed=0;
            if(!(status&EST_DONE)) {
                control(word|2u);
                const uint32_t start=cycles();
                do {
                    status=DPORT_SEQUENCE_REG_READ(EST_STATUS);
                    elapsed=cycles()-start;
                    if(status&EST_DONE) {completed=true;break;}
                } while(elapsed<timeout);
            }
            control(word);
            if(attempt<2u)continue;
            if(!completed) {++out[entry].timeouts;continue;}
            if(elapsed<out[entry].min_cycles)out[entry].min_cycles=elapsed;
            if(elapsed>out[entry].max_cycles)out[entry].max_cycles=elapsed;
            totals[entry]+=elapsed;
        }
    }
    control(estimator);
    e32_stream_leave(token);
    for(unsigned entry=0;entry<4u;++entry) {
        const unsigned completed=100u-out[entry].timeouts;
        if(completed)out[entry].mean_cycles=totals[entry]/completed;
        else out[entry].min_cycles=0u;
    }
    return ESP_OK;
}

const int16_t *e32_rx_bench_iq(uint32_t *count, uint32_t *next)
{
    if (count) *count = bench_iq_count;
    if (next) *next = bench_iq_next;
    return &bench_iq[0][0];
}
#endif
