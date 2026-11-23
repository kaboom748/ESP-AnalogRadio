/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_attr.h"
#ifndef CB13_BENCH_IQ
#define CB13_BENCH_IQ 0
#endif
#if CB13_BENCH_IQ != 0 && CB13_BENCH_IQ != 1
#error "CB13_BENCH_IQ must be 0 or 1"
#endif
#ifdef __cplusplus
extern "C" {
#endif

esp_err_t e32_radio_init(void);
/* Classic ESP32 ROM PLL steps are 1/1024 MHz (976.5625 Hz). The getter
 * reports the nearest integer Hz corresponding to the selected step. */
esp_err_t e32_radio_tune(uint32_t carrier_hz);
/* Retune an already calibrated/keyed chain without re-entering a channel. */
esp_err_t e32_radio_set_frequency_only(uint32_t carrier_hz);
uint32_t e32_radio_frequency_hz(void);
/* Optional bench override; -1 uses stable automatic calibrated gain.
 * Applied by the next receive initialization, never by the UART parser hot. */
void e32_radio_set_rx_gain(int index);
bool IRAM_ATTR e32_radio_rx_gain_automatic(void);
unsigned IRAM_ATTR e32_radio_rx_gain_index(void);
unsigned IRAM_ATTR e32_radio_rx_gain_maximum(void);
/* Hot level update follows rom_rx_gain_force(1,index): force remains set
 * while the calibrated gain/DC row changes. Caller must mask interrupts
 * using the streaming runtime for DPORT sequence reads. */
bool IRAM_ATTR e32_radio_rx_gain_update(unsigned index);
esp_err_t e32_radio_receive(uint32_t carrier_hz, int32_t fine_hz);
void e32_radio_idle(void);
#if CB13_BENCH_IQ
typedef struct {
    uint32_t lo_control,filter_i,filter_q,calibration_flags;
} e32_radio_bench_readback_t;
/* Cold bench APIs only: call after capture leaves the streaming runtime.
 * Gain codes are the original words written by this SDK's Wi-Fi table
 * builder, not the forced-index field. No radio setting is changed. */
bool e32_radio_bench_gain_code(unsigned index,uint16_t *code);
esp_err_t e32_radio_bench_readback(e32_radio_bench_readback_t *readback);
/* Bench A/B only, default false. Re-enter normal channel setup after
 * fresh DC calibration, then restore the requested fractional LO. */
void e32_radio_bench_reinitialize(bool enabled);
/* Bench A/B only, applied by the next RX initialization. Hardware mode
 * releases native forced gain and disables the software gain controller. */
void e32_radio_bench_hardware_agc(bool enabled);
/* Physical RX LO offset in Hz, valid0/192000/240000/312500. IQ cadence and
 * PCM transport stay unchanged; the PLL/DC calibration uses this LO.
 * The largest offset is for centre-band tests; existing PLL limits apply. */
bool e32_radio_bench_if(unsigned hz);
#endif
#ifdef __cplusplus
}
#endif
