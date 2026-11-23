/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdint.h>
#include "esp_attr.h"
#include "c5_tx.h"

namespace c5_pcm_tx_dsp {

// The V5 36 -> 120 kHz Q14 polyphase interpolator. Phase zero is a delay.
static const DRAM_ATTR int16_t Fir[10][10] = {
    {0, 0, 0, 0, 0, 16384, 0, 0, 0, 0},
    {68, -169, 350, -698, 1706, 15995, -1368, 605, -305, 144},
    {143, -348, 712, -1429, 3683, 15189, -2356, 1073, -540, 252},
    {219, -518, 1049, -2122, 5838, 13902, -2955, 1378, -693, 318},
    {284, -656, 1320, -2697, 8060, 12214, -3181, 1512, -759, 343},
    {329, -742, 1485, -3075, 10227, 10227, -3075, 1485, -742, 329},
    {343, -759, 1512, -3181, 12214, 8060, -2697, 1320, -656, 284},
    {318, -693, 1378, -2955, 13902, 5838, -2122, 1049, -518, 219},
    {252, -540, 1073, -2356, 15189, 3683, -1429, 712, -348, 143},
    {144, -305, 605, -1368, 15995, 1706, -698, 350, -169, 68},
};

struct alignas(4) Stereo { int16_t left, right; };
static Stereo history[32];
static uint8_t historyAt, interpolationPhase;

static inline void reset() {
    for (auto &pair : history) pair = {};
    historyAt = 0;
    interpolationPhase = 10;
}

static inline __attribute__((always_inline)) bool pairPending() { return interpolationPhase < 10; }

static inline __attribute__((always_inline)) int16_t extend12(uint16_t value) {
    return int16_t(int32_t(value ^ 0x800u) - 0x800);
}

// loadPair follows completion of the preceding pair (or reset). Its first
// call makes four samples available. The following two calls make three each.
static inline __attribute__((always_inline)) void loadPair(const uint8_t* stereo) {
    historyAt = (historyAt + 1) & 15;
    const Stereo pair = {
        extend12(uint16_t(stereo[0]) | (uint16_t(stereo[1] & 15) << 8)),
        extend12((uint16_t(stereo[2]) << 4) | (stereo[1] >> 4)),
    };
    history[historyAt] = history[historyAt + 16] = pair;
    interpolationPhase -= 10;
}

static inline __attribute__((always_inline)) int16_t clipQ4(int32_t sample) {
    if (sample > C5_TX_PCM_Q4_FULL_SCALE) return C5_TX_PCM_Q4_FULL_SCALE;
    if (sample < -C5_TX_PCM_Q4_FULL_SCALE) return -C5_TX_PCM_Q4_FULL_SCALE;
    return int16_t(sample);
}

// Keep each interpolated channel separate through the handoff to RF.
static Stereo IRAM_ATTR __attribute__((optimize("O2"))) renderSampleQ4() {
    const uint8_t phase = interpolationPhase;
    interpolationPhase += 3;
    const Stereo* current = &history[historyAt + 16];
    if (phase == 0) {
        const Stereo delayed = current[-5];
        return {clipQ4(delayed.left * 16), clipQ4(delayed.right * 16)};
    }
    const int16_t* coefficients = Fir[phase];
    int32_t left = 512, right = 512;
#pragma GCC unroll 10
    for (uint8_t tap = 0; tap < 10; ++tap) {
        const Stereo pair = *current--;
        const int16_t coefficient = coefficients[tap];
        left += pair.left * coefficient;
        right += pair.right * coefficient;
    }
    return {clipQ4(left >> 10), clipQ4(right >> 10)};
}

}  // namespace c5_pcm_tx_dsp
