/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_attr.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Continuous CB13 NCO backend. All full calibration occurs before READY. */
bool c5_tx_radio_prepare(uint32_t carrier_hz,uint8_t apwr);
bool IRAM_ATTR c5_tx_radio_key(void);
/* Original ESP8266 NCO units, 78125 Hz each. K=4 is the RF carrier. */
void IRAM_ATTR c5_tx_radio_write_k(unsigned k);
void IRAM_ATTR c5_tx_radio_off(void);
void c5_tx_radio_release(void);
uint32_t c5_tx_radio_lo_hz(void);
void c5_tx_radio_power(uint16_t *rf,uint16_t *bb,uint16_t *attenuation);
#ifdef __cplusplus
}
#endif
