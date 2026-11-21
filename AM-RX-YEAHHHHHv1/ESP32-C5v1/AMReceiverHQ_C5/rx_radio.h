/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stdint.h>
#include "esp_attr.h"

// Start Wi-Fi PHY reception without joining a network. gainIndex=-1 uses
// hardware AGC. Nonnegative values select a calibrated PHY table index.
bool rxStart(uint32_t carrierKHz, int gainIndex, uint64_t* programmedCarrierHz);

// Read one native 64-point I/Q average, in signed ADC units with four
// fractional bits. The 96 kSa/s scheduler supplies the +24 kHz rotation.
bool IRAM_ATTR rxSample(int16_t* i, int16_t* q);

// Call after disabling sampling. Restore estimator and gain controls.
void rxStop();
