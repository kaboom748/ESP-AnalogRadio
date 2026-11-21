/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stdint.h>
#include "esp_attr.h"

// The Arduino sketch calls these on the main task. Starting AM arms the
// phase-continuous NCO with its gate closed until the first envelope sample.
bool radioInit();
void radioSetBand(bool highBand);
bool radioCW(uint8_t apwr);
bool radioAM(uint8_t apwr);
void radioOff();

// The timer interrupt supplies ASK in quarter-dB steps relative to APWR.
// Mute closes the gate without releasing the PHY or disarming AM.
void IRAM_ATTR radioEnvelope(uint8_t ask);
void IRAM_ATTR radioMute();
