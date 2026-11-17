/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* captured/skipped: IQ slots; samples/invalid: output PCM samples;
     * overruns: CPU deadline events containing at least one lost IQ slot. */
    uint32_t captured, skipped, timeouts, samples, invalid, overruns;
    uint32_t max_lag_bytes, window_lsb, peak;
    /* Legacy PARLIO edge counters stay zero with native IQ acquisition. */
    uint32_t pcm_clipped, iq_edge_codes, iq_edge_windows;
    uint32_t fm_unwraps;
    int32_t frequency_hz;
    int32_t dc_i_adc, dc_q_adc;
    int32_t discriminator_min_hz, discriminator_max_hz;
    /* Optional DIAGHW: productive native IQ work excludes deadline waiting;
     * cycle/IQ is computed when fetching stats, outside the capture loop. */
    uint64_t read_cycles, profiled_iq;
    uint32_t read_calls, max_read_cycles, mean_cycles_per_iq, iq_window_samples;
    uint32_t iq_rate_hz, source_rate_hz, max_late_cycles, late_slots;
    uint32_t clear_still_high;
    /* Stable continuous-MPX AGC uses calibrated hardware gain/DC rows.
     * This does not change IQ rate, estimator resolution or output indices. */
    uint32_t gain_updates,gain_index,gain_maximum;
} c5_rx_stats_t;

/* Profiles: 0=HQ (120 kHz), 1=ROBUST (96 kHz), 2=MONO (48 kHz).
 * Native hardware IQ accumulator; no diagnostic GPIO or DMA capture.
 * start/stop run outside the hot loop and save/restore estimator controls. */
esp_err_t c5_rx_start(uint32_t carrier_hz, int32_t fine_hz, unsigned profile);
void c5_rx_stop(void);
typedef void (*c5_rx_emit_cb_t)(uint32_t index, uint8_t pcm,
                               uint16_t strength, void *user);
typedef bool (*c5_rx_pump_cb_t)(void *user);
/* Continuous fixed-clock loop; disables interrupts until pump returns false.
 * Both callbacks must be IRAM-safe and call no RTOS/PHY/driver functions.
 * emit produces indexed signed PCM8 (reserved0x80 for real missing slots).
 * pump owns UART service and stops on a pending command, preserving any
 * consumed command byte in the caller's parser latch.
 * No UART, framing or CRC access occurs inside this module. */
void c5_rx_run(c5_rx_emit_cb_t emit, c5_rx_pump_cb_t pump, void *user);
void c5_rx_get_stats(c5_rx_stats_t *stats);
#ifdef __cplusplus
}
#endif
