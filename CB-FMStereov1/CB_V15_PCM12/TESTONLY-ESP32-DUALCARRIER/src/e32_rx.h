/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
/* Detailed cycle accounting costs real-time margin on the 240MHz ESP32.
 * Keep it available for bench builds; default streaming retains timing,
 * acquisition and loss counters without per-IQ64-bit profiling updates. */
#ifndef E32_RX_PROFILE
#define E32_RX_PROFILE 0
#endif
#if E32_RX_PROFILE != 0 && E32_RX_PROFILE != 1
#error "E32_RX_PROFILE must be0 or1"
#endif
#ifndef CB13_BENCH_IQ
#define CB13_BENCH_IQ 0
#endif
#if CB13_BENCH_IQ != 0 && CB13_BENCH_IQ != 1
#error "CB13_BENCH_IQ must be 0 or 1"
#endif
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* captured/skipped: IQ slots; samples/invalid: multiplex sample slots;
     * overruns: CPU deadline events containing at least one lost IQ slot. */
    uint32_t captured, skipped, timeouts, samples, invalid, overruns;
    uint32_t max_lag_bytes, window_lsb, peak;
    /* Legacy PARLIO edge counters stay zero with native IQ acquisition. */
    uint32_t pcm_clipped, iq_edge_codes, iq_edge_windows;
    uint32_t fm_unwraps;
    int32_t frequency_hz;
    int32_t dc_i_adc, dc_q_adc;
    int32_t discriminator_min_hz, discriminator_max_hz;
    /* With E32_RX_PROFILE=1, productive native IQ work excludes waiting;
     * cycle/IQ is computed outside the capture loop. Timing counters remain
     * zero (unmeasured) in the default build; acquisition metadata such as
     * iq_window_samples is still populated independently. */
    uint64_t read_cycles, profiled_iq;
    uint32_t read_calls, max_read_cycles, mean_cycles_per_iq, iq_window_samples;
    uint32_t iq_rate_hz, source_rate_hz, max_late_cycles, late_slots;
    /* Readback diagnostic: every IQ in profile builds, once/256 otherwise. */
    uint32_t clear_still_high;
    /* Stable continuous-MPX AGC uses calibrated hardware gain/DC rows.
     * This does not change IQ rate, estimator resolution or output indices. */
    uint32_t gain_updates,gain_index,gain_maximum;
    /* FM IQ slots whose phase difference was held across a calibrated
     * gain change. Real IQ, output indices and PCM validity are retained. */
    uint32_t gain_held_slots;
#if CB13_BENCH_IQ
    /* Snapshot at the end of live RX, before releasing the peer CPU and
     * before stop restores ADC/estimator ownership. Native words only. */
    uint32_t bench_gain_word,bench_pbus_word,bench_bb_word,bench_iq_word,bench_adc_word;
    uint32_t bench_debug_word,bench_mode_word;
    /* Native total ADC energy, including DC, from the same completed IQ
     * windows. Converted ADC-squared summaries are computed cold. */
    uint64_t bench_power_sum_raw;
    uint32_t bench_power_max_raw,bench_power_mean_adc2,bench_power_max_adc2;
#endif
} e32_rx_stats_t;

/* Profiles: 0=HQ (120 kHz), 1=ROBUST (96 kHz), 2=MONO (48 kHz).
 * Native hardware IQ accumulator; no diagnostic GPIO or DMA capture.
 * start/stop run outside the hot loop and save/restore estimator controls. */
esp_err_t e32_rx_start(uint32_t carrier_hz, int32_t fine_hz, unsigned profile);
#define E32_RX_PCM_FRAME_SIZE 400u
#define E32_RX_PCM_AUDIO_RATE 40000u
/* PCM mode keeps the 120 kHz multiplex inside the receiver. The profile
 * still selects audio bandwidth and mono/stereo decoding. */
esp_err_t e32_rx_start_pcm(uint32_t carrier_hz, int32_t fine_hz, unsigned profile);
void e32_rx_stop(void);
typedef void (*e32_rx_emit_cb_t)(uint32_t index, uint8_t pcm,
                               uint16_t strength, void *user);
typedef uint8_t *(*e32_rx_pcm_commit_cb_t)(void *user);
typedef bool (*e32_rx_pump_cb_t)(void *user);
/* Continuous fixed-clock loop; disables interrupts until pump returns false.
 * Both callbacks must be IRAM-safe and call no RTOS/PHY/driver functions.
 * emit produces indexed signed PCM8 (reserved0x80 for real missing slots).
 * pump owns UART service and stops on a pending command, preserving any
 * consumed command byte in the caller's parser latch.
 * No UART access occurs inside this module. */
void e32_rx_run(e32_rx_emit_cb_t emit, e32_rx_pump_cb_t pump, void *user);
/* The decoder fills a caller-owned 400-byte frame directly. The commit
 * callback publishes it or drops it and returns the next writable slot. */
void e32_rx_run_pcm(uint8_t *frame, e32_rx_pcm_commit_cb_t commit,
                    e32_rx_pump_cb_t pump, void *user);
void e32_rx_get_stats(e32_rx_stats_t *stats);
#if CB13_BENCH_IQ
/* Bench only, while RX is stopped. Select32/64/128 native conversions
 * for the next start; the mean always retains the same four fractions. */
bool e32_rx_bench_window(unsigned n);
/* Bench only, applied at the next start. False retains mean-IQ AGC;
 * true observes native total ADC window energy instead. */
void e32_rx_bench_power_agc(bool enabled);
/* Bench only, call after capture stops. Interleaved native signed I/Q
 * before DC/FM processing, in the same Q4 scale as the DSP input.
 * count is valid pairs (at most 4096), next is the next write slot.
 * Once full, chronological order begins at next and wraps at count.
 * The pointer remains valid until the next receive capture starts. */
const int16_t *e32_rx_bench_iq(uint32_t *count, uint32_t *next);
typedef struct {
    uint32_t n,min_cycles,mean_cycles,max_cycles,timeouts;
} e32_rx_bench_timing_t;
/* Call after rx_start and before streaming, then perform normal stop/idle.
 * Four native lengths32/64/128/256,100 measured captures each after two
 * warmups. Mean excludes timed-out captures; all cycles are CPU cycles.
 * No DSP, UART or sample-rate waiting contributes to these measurements. */
esp_err_t e32_rx_bench_timing(e32_rx_bench_timing_t out[4]);
#endif
#ifdef __cplusplus
}
#endif
