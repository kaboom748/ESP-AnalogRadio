/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_attr.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Continuous CB13 NCO backend. All full calibration occurs before READY. */
bool c5_tx_radio_prepare(uint64_t carrier_hz,uint8_t apwr);
bool IRAM_ATTR c5_tx_radio_key(void);
/* Native C5 fine steps are 39062.5 Hz. K=8 is LO + 312500 Hz. */
#define C5_TX_NCO_STEP_HZ_X2 78125u
#define C5_TX_NCO_CARRIER_CODE 8
void IRAM_ATTR c5_tx_radio_write_k(unsigned k);
void IRAM_ATTR c5_tx_radio_off(void);
void c5_tx_radio_release(void);
uint64_t c5_tx_radio_lo_hz(void);
/* Diagnostics for 5 GHz TX calibration/bench verification. */
const char *c5_tx_radio_last_error(void);
bool c5_tx_radio_is_5g(void);
unsigned c5_tx_radio_table_index(void);
void c5_tx_radio_power(uint16_t *rf,uint16_t *bb,uint16_t *attenuation);
#ifdef __cplusplus
}
#endif
