/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdint.h>
#include "esp_attr.h"

struct CbRadioConfig {
    uint32_t loKHz;
    uint8_t power, rfGain, vga, ask, ifK, bbGain;
    bool listenOnly;
};

/* Packetless native C5 PHY backend, pinned to Arduino-ESP32 3.3.12. */
bool cbRadioInit();
bool cbRadioConfigure(const CbRadioConfig& cfg);
/* Diagnostic calibrated gain-table index, -1 restores protocol controls.
 * Applied by the next configuration, never inside a timed RF slot. */
void cbRadioSetGainOverride(int index);
/* Burst-aware control uses only calibrated hardware gain/DC table entries.
 * Call outside an audio interval; changing an index takes a bounded4us pulse. */
bool IRAM_ATTR cbRadioSetRuntimeGain(unsigned index);
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
    uint8_t row, stage; // row255 means a native coarse stage below the32-row table
    CbRadioTxGainRow gain;
};
bool cbRadioGetTxGainRow(unsigned index, CbRadioTxGainRow& result);
void cbRadioGetTxPower(CbRadioTxPower& result);

/* Raw = 64*sum(signed ADC10). N=64, raw>>8 gives ADC10 mean with
 * four fractional bits. Native Q is positive for RF above the LO.
 * The application owns estimator setup/save/restore and bounded triggering. */
namespace CbIq {
constexpr uint32_t control0 = 0x600a044cu;
constexpr uint32_t control1 = 0x600a0450u;
constexpr uint32_t sumI = 0x600a0464u;
constexpr uint32_t sumQ = 0x600a0468u;
constexpr uint32_t status = 0x600a047cu;
constexpr uint32_t done = 1u << 16;
constexpr unsigned windowSamples = 64u;
constexpr unsigned sumShift = 8u;
}
