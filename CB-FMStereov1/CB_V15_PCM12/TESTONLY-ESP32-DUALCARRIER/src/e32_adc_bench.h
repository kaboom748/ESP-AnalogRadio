/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdint.h>
#include "esp_attr.h"
#include "esp_err.h"
#ifndef CB13_BENCH_IQ
#define CB13_BENCH_IQ 0
#endif
#ifdef __cplusplus
extern "C" {
#endif

#if CB13_BENCH_IQ
#define E32_ADC_BENCH_MAX_SAMPLES 16380u
typedef struct {
    const uint32_t *raw; /* Native words: low ten I bits, next ten -Q bits. */
    uint32_t samples, capture_cycles, control, status;
    uint32_t saved_owner, saved_lane, saved_control;
    uint32_t restored; /* bits0/1/2/3: owner/lane/control/ADC force restored */
    uint32_t bad_index; /* UINT32_MAX unless the sentinel check failed. */
    int32_t native_i, native_q; /* Unshifted native signed Q6 accumulations. */
    uint32_t native_power, native_ready, trigger_delta_cycles;
    uint32_t native_done_at_raw_end; /* Qualifies equal-time-window comparisons. */
} e32_adc_bench_stats_t;

/* Bench only. Call after e32_rx_start(), before its continuous run loop.
 * The PHY/ADC must already be on. n=256..16380, SRC0, nominal 80 MS/s.
 * The result stays in its boot-reserved 64 KiB until the next capture.
 * No RF configuration, serial access or allocation is performed here.
 * The linked image must pass the supplied SRAM/IRAM linker assertions.
 * The existing source1 estimator runs with the same n, triggered first;
 * native_ready qualifies its final sums independently of RAW success.
 * Its controls are restored before the peer CPU is released.
 * On failure raw=NULL and samples=0; control/status preserve diagnostics. */
esp_err_t IRAM_ATTR e32_adc_bench_capture(unsigned n, e32_adc_bench_stats_t *stats);
#endif

#ifdef __cplusplus
}
#endif
