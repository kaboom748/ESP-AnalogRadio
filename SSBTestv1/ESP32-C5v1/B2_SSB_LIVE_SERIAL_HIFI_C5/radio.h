// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <stdint.h>
#include "esp_attr.h"

void radioSetBand(bool highBand);
bool radioCW(uint8_t apwr);
bool radioSSB();
void radioOff();
void IRAM_ATTR radioIQ(int16_t i, int16_t q);

// The PLL supplies +294 kHz and the software oscillator supplies +18.5 kHz.
constexpr uint32_t ssbDigitalIFHz = 18500;
