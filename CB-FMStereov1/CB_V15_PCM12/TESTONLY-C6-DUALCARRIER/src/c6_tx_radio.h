/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_attr.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Continuous CB13 NCO backend. All full calibration occurs before READY. */
#define C6_TX_CARRIER_MIN_HZ 2401000000u
#define C6_TX_SECOND_CARRIER_OFFSET_HZ 781250u
#define C6_TX_LO_STEP_HZ 1000u
/* Reserve room for the second centre and nearest-kHz LO rounding. */
#define C6_TX_CARRIER_MAX_HZ (2482500000u - C6_TX_SECOND_CARRIER_OFFSET_HZ - C6_TX_LO_STEP_HZ / 2u)
bool c6_tx_radio_prepare(uint32_t carrier_hz,uint8_t apwr,unsigned center_k);
bool IRAM_ATTR c6_tx_radio_key(void);
/* Native C6 fine steps are 39062.5 Hz. K=8 is LO + 312500 Hz. */
#define C6_TX_NCO_STEP_HZ_X2 78125u
#define C6_TX_NCO_CARRIER_CODE 8
#define C6_TX_NCO_IN_GROUP_CENTER_CODE 9
void IRAM_ATTR c6_tx_radio_write_k(unsigned k);
void IRAM_ATTR c6_tx_radio_off(void);
void c6_tx_radio_release(void);
uint32_t c6_tx_radio_lo_hz(void);
void c6_tx_radio_power(uint16_t *rf,uint16_t *bb,uint16_t *attenuation);
#ifdef __cplusplus
}
#endif
