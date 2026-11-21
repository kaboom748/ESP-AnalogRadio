/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdint.h>
#include "esp_attr.h"

struct CbRadioConfig {
    uint32_t loKHz;
    uint8_t power, rfGain, vga, ask, ifK, bbGain;
    bool listenOnly;
};

bool cbRadioInit();
bool cbRadioConfigure(const CbRadioConfig& config);
void cbRadioSetGainOverride(int index);
// Reports the programmed manual row (zero for hardware automatic gain).
unsigned IRAM_ATTR cbRadioCurrentGain();
unsigned IRAM_ATTR cbRadioMaxGain();
bool IRAM_ATTR cbRadioAutoGain();
bool IRAM_ATTR cbRadioTxBegin();
void IRAM_ATTR cbRadioTone(int direction);
void IRAM_ATTR cbRadioTxEnd();
void cbRadioRelease();
bool IRAM_ATTR cbRadioHealthy();

struct CbRadioStats {
    uint32_t maxTxBeginCycles, maxTxEndCycles, busTimeouts;
    uint32_t ncoMode, ncoStep, switchWords, attenuationClipped;
};
void cbRadioGetStats(CbRadioStats& result);
struct CbRadioBusWord { uint8_t selector, bank; uint16_t rx, tx; };
bool cbRadioGetBusWord(unsigned index, CbRadioBusWord& result);
struct CbRadioTxGainRow { int16_t digital; uint16_t rf, bb; };
struct CbRadioTxPower {
    uint16_t requestedQuarterDb, baselineQuarterDb, ncoAttenuation;
    uint8_t row, stage;
    CbRadioTxGainRow gain;
};
bool cbRadioGetTxGainRow(unsigned index, CbRadioTxGainRow& result);
void cbRadioGetTxPower(CbRadioTxPower& result);

/* The native C6 IQ register contains 64 times the sum of 64 signed ADC10
 * samples. Shifting it by eight gives the mean with four fractional bits. */
namespace CbIq {
constexpr uint32_t control0 = 0x600a0470u, control1 = 0x600a0474u;
constexpr uint32_t sumI = 0x600a0488u, sumQ = 0x600a048cu;
constexpr uint32_t status = 0x600a04a0u, done = 1u << 16;
constexpr unsigned windowSamples = 64u, sumShift = 8u;
}
