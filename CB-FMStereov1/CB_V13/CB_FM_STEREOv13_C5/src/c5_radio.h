/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_attr.h"
#ifdef __cplusplus
extern "C" {
#endif

esp_err_t c5_radio_init(void);
/* The characterized C5 PHY tuning function has 1 kHz resolution. */
esp_err_t c5_radio_tune(uint32_t carrier_hz);
/* Retune an already calibrated/keyed chain without re-entering a channel. */
esp_err_t c5_radio_set_frequency_only(uint32_t carrier_hz);
uint32_t c5_radio_frequency_hz(void);
/* Optional bench override; -1 uses stable automatic calibrated gain.
 * Applied by the next receive initialization, never by the UART parser hot. */
void c5_radio_set_rx_gain(int index);
bool IRAM_ATTR c5_radio_rx_gain_automatic(void);
unsigned IRAM_ATTR c5_radio_rx_gain_index(void);
unsigned IRAM_ATTR c5_radio_rx_gain_maximum(void);
/* Hot level update follows phy_force_rx_gain(1,index): force remains set
 * while the calibrated gain/DC row changes. No pause, I2C or delay. */
bool IRAM_ATTR c5_radio_rx_gain_update(unsigned index);
esp_err_t c5_radio_receive(uint32_t carrier_hz, int32_t fine_hz);
void c5_radio_idle(void);
#ifdef __cplusplus
}
#endif
