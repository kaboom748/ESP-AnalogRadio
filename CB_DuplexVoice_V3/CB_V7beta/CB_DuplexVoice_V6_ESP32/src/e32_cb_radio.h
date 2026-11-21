/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdint.h>
#include "esp_attr.h"

struct CbRadioConfig {
    uint32_t loKHz;
    uint8_t power, rfGain, vga, ask, ifK, bbGain;
    bool listenOnly;
};

enum class StartupStage : uint32_t {
    Idle=0, Parsed=10, Saving=20, TxCalibration=21, RxTuning=22,
    RxReady=23, ForcedMode=24, RxSnapshot=25, TxSnapshot=26, RestoringRx=27,
    Configured=30, EnteringStream=31, StreamEntered=32, EstimatorReady=33,
    FirstLoop=34, Ending=35
};
void IRAM_ATTR cbRememberStartup(StartupStage stage);
void cbReportStartup(StartupStage stage);

/* Packetless original ESP32 PHY backend, pinned to Arduino-ESP32 3.3.12. */
bool cbRadioInit();
bool cbRadioConfigure(const CbRadioConfig& cfg);
/* Diagnostic calibrated gain-table index, -1 restores protocol controls.
 * Applied by the next configuration, never inside a timed RF slot. */
void cbRadioSetGainOverride(int index);
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

/* Classic estimator registers. The signed native sum shifted by eight keeps
 * four fractional ADC10 bits for these 64 conversions. */
namespace CbIq {
constexpr uint32_t control0 = 0x3ff46060u;
constexpr uint32_t control1 = 0x3ff4607cu;
constexpr uint32_t sumI = 0x3ff460dcu;
constexpr uint32_t sumQ = 0x3ff460e0u;
constexpr uint32_t status = control1;
constexpr uint32_t done = 1u << 31;
constexpr unsigned windowSamples = 64u;
constexpr unsigned sumShift = 8u;
}
