/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_attr.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Continuous CB13 NCO backend. All full calibration occurs before READY. */
bool e32_tx_radio_prepare(uint32_t carrier_hz,uint8_t apwr);
/* Vendor calls: key before entering the parked-core streaming runtime. */
bool e32_tx_radio_key(void);
/* Native ESP32 fine steps are 39062.5 Hz. K=8 is LO + 312500 Hz. */
#define E32_TX_NCO_STEP_HZ_X2 78125u
#define E32_TX_NCO_CARRIER_CODE 8
void IRAM_ATTR e32_tx_radio_write_k(unsigned k);
void IRAM_ATTR e32_tx_radio_gate_off(void);
/* Vendor cleanup: call after leaving the streaming runtime. */
void e32_tx_radio_off(void);
void e32_tx_radio_release(void);
uint32_t e32_tx_radio_lo_hz(void);
void e32_tx_radio_power(uint16_t *rf,uint16_t *bb,uint16_t *attenuation);
#ifdef __cplusplus
}
#endif
