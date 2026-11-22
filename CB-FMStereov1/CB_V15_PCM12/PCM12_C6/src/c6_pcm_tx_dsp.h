/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdint.h>
#include "esp_attr.h"
#include "c6_tx.h"

namespace c6_pcm_tx_dsp {

constexpr unsigned RdsSideSamples = 30;

// 36 -> 120 kHz Q14 polyphase FIR with a 15 kHz passband and 21 kHz
// stopband edge. Each phase has unity DC gain. Phase zero is an exact
// seven-sample delay, so it skips all the multiplies.
static const DRAM_ATTR int16_t Fir[10][14] = {
    {0, 0, 0, 0, 0, 0, 0, 16384, 0, 0, 0, 0, 0, 0},
    {325, -199, 124, -306, 465, -773, 1763, 16123, -1418, 702, -406, 209, -192, -33},
    {242, -268, 328, -606, 887, -1587, 3752, 15305, -2479, 1219, -715, 424, -266, 148},
    {232, -377, 502, -804, 1308, -2301, 5959, 14031, -3096, 1583, -967, 551, -436, 199},
    {256, -449, 573, -1004, 1609, -2918, 8202, 12334, -3365, 1794, -1086, 675, -466, 229},
    {257, -453, 654, -1113, 1785, -3305, 10367, 10367, -3305, 1785, -1113, 654, -453, 257},
    {229, -466, 675, -1086, 1794, -3365, 12334, 8202, -2918, 1609, -1004, 573, -449, 256},
    {199, -436, 551, -967, 1583, -3096, 14031, 5959, -2301, 1308, -804, 502, -377, 232},
    {148, -266, 424, -715, 1219, -2479, 15305, 3752, -1587, 887, -606, 328, -268, 242},
    {-33, -192, 209, -406, 702, -1418, 16123, 1763, -773, 465, -306, 124, -199, 325},
};

// The pilot and the two subcarriers share a phase origin and sample clock.
static const DRAM_ATTR int16_t Pilot[120] = {
    0, 2472, 2693, 461, -2191, -2847, -911, 1855, 2932, 1338, -1474, -2944,
    -1733, 1056, 2883, 2084, -613, -2752, -2385, 154, 2553, 2626, 308, -2291,
    -2803, -763, 1972, 2911, 1199, -1605, -2948, -1605, 1199, 2911, 1972, -763,
    -2803, -2291, 308, 2626, 2553, 154, -2385, -2752, -613, 2084, 2883, 1056,
    -1733, -2944, -1474, 1338, 2932, 1855, -911, -2847, -2191, 461, 2693, 2472,
    0, -2472, -2693, -461, 2191, 2847, 911, -1855, -2932, -1338, 1474, 2944,
    1733, -1056, -2883, -2084, 613, 2752, 2385, -154, -2553, -2626, -308, 2291,
    2803, 763, -1972, -2911, -1199, 1605, 2948, 1605, -1199, -2911, -1972, 763,
    2803, 2291, -308, -2626, -2553, -154, 2385, 2752, 613, -2084, -2883, -1056,
    1733, 2944, 1474, -1338, -2932, -1855, 911, 2847, 2191, -461, -2693, -2472
};
static const DRAM_ATTR int16_t Difference[120] = {
    0, 17138, -13941, -5797, 18657, -9380, -11027, 18350, -3900, -15177, 16246, 1961,
    -17842, 12553, 7630, -18760, 7630, 12553, -17842, 1961, 16246, -15177, -3900, 18350,
    -11027, -9380, 18657, -5797, -13941, 17138, 0, -17138, 13941, 5797, -18657, 9380,
    11027, -18350, 3900, 15177, -16246, -1961, 17842, -12553, -7630, 18760, -7630, -12553,
    17842, -1961, -16246, 15177, 3900, -18350, 11027, 9380, -18657, 5797, 13941, -17138,
    0, 17138, -13941, -5797, 18657, -9380, -11027, 18350, -3900, -15177, 16246, 1961,
    -17842, 12553, 7630, -18760, 7630, 12553, -17842, 1961, 16246, -15177, -3900, 18350,
    -11027, -9380, 18657, -5797, -13941, 17138, 0, -17138, 13941, 5797, -18657, 9380,
    11027, -18350, 3900, 15177, -16246, -1961, 17842, -12553, -7630, 18760, -7630, -12553,
    17842, -1961, -16246, 15177, 3900, -18350, 11027, 9380, -18657, 5797, 13941, -17138
};
static const DRAM_ATTR int16_t RdsCarrier[120] = {
    330, -326, 314, -294, 267, -233, 194, -150, 102, -52, 0, 52,
    -102, 150, -194, 233, -267, 294, -314, 326, -330, 326, -314, 294,
    -267, 233, -194, 150, -102, 52, 0, -52, 102, -150, 194, -233,
    267, -294, 314, -326, 330, -326, 314, -294, 267, -233, 194, -150,
    102, -52, 0, 52, -102, 150, -194, 233, -267, 294, -314, 326,
    -330, 326, -314, 294, -267, 233, -194, 150, -102, 52, 0, -52,
    102, -150, 194, -233, 267, -294, 314, -326, 330, -326, 314, -294,
    267, -233, 194, -150, 102, -52, 0, 52, -102, 150, -194, 233,
    -267, 294, -314, 326, -330, 326, -314, 294, -267, 233, -194, 150,
    -102, 52, 0, -52, 102, -150, 194, -233, 267, -294, 314, -326
};

struct alignas(4) Stereo { int16_t left, right; };
static Stereo history[32];
static uint8_t historyAt, interpolationPhase, mpxPhase, rdsPhase, rdsOffset;
static int16_t rdsCurrent, rdsStep, rdsInterpolated;

static inline void reset() {
    for (auto &pair : history) pair = {};
    historyAt = mpxPhase = rdsPhase = rdsOffset = 0;
    interpolationPhase = 10;
    rdsCurrent = rdsStep = rdsInterpolated = 0;
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

static inline __attribute__((always_inline)) int16_t mix(int32_t leftQ4, int32_t rightQ4, const uint8_t* envelope) {
    const int32_t difference = (((leftQ4 - rightQ4) >> 1) * Difference[mpxPhase] + 4096) >> 13;
    const int32_t programme = (leftQ4 + rightQ4 + difference) >> 1;
    if (rdsPhase == 0) {
        const int16_t previous = rdsCurrent;
        rdsCurrent = int8_t(envelope[rdsOffset]);
        rdsStep = rdsCurrent - previous;
        rdsInterpolated = previous * 12;
        if (++rdsOffset == RdsSideSamples) rdsOffset = 0;
    }
    const int32_t rds = (rdsInterpolated * RdsCarrier[mpxPhase] + 128) >> 8;
    rdsInterpolated += rdsStep;
    if (++rdsPhase == 12) rdsPhase = 0;
    const int32_t mpx = programme + Pilot[mpxPhase] + rds;
    if (++mpxPhase == 120) mpxPhase = 0;
    if (mpx > C6_TX_MPX_Q4_FULL_SCALE) return C6_TX_MPX_Q4_FULL_SCALE;
    if (mpx < -C6_TX_MPX_Q4_FULL_SCALE) return -C6_TX_MPX_Q4_FULL_SCALE;
    return int16_t(mpx);
}

struct StereoQ4 { int32_t left, right; };
static DRAM_ATTR StereoQ4 preparedSample;

static inline __attribute__((always_inline)) StereoQ4 interpolateSample() {
    const uint8_t phase = interpolationPhase;
    const Stereo* current = &history[historyAt + 16];
    if (phase == 0) {
        const Stereo delayed = current[-7];
        return {delayed.left * 16, delayed.right * 16};
    }
    const int16_t* coefficients = Fir[phase];
    int32_t left = 512, right = 512;
    // Read each signed halfword directly and consume each pair as soon as
    // it is multiplied. This avoids unpacking stereo and spilling the taps.
#define C6_PCM_TAP(Coeff, Left, Right) \
    "lh t0, " #Coeff "(%[coefficients])\n\t" \
    "lh t1, " #Left "(%[samples])\n\t" \
    "lh t2, " #Right "(%[samples])\n\t" \
    "mul t1, t1, t0\n\t" \
    "mul t2, t2, t0\n\t" \
    "add %[left], %[left], t1\n\t" \
    "add %[right], %[right], t2\n\t"
    asm(
        C6_PCM_TAP(0, 0, 2)
        C6_PCM_TAP(2, -4, -2)
        C6_PCM_TAP(4, -8, -6)
        C6_PCM_TAP(6, -12, -10)
        C6_PCM_TAP(8, -16, -14)
        C6_PCM_TAP(10, -20, -18)
        C6_PCM_TAP(12, -24, -22)
        C6_PCM_TAP(14, -28, -26)
        C6_PCM_TAP(16, -32, -30)
        C6_PCM_TAP(18, -36, -34)
        C6_PCM_TAP(20, -40, -38)
        C6_PCM_TAP(22, -44, -42)
        C6_PCM_TAP(24, -48, -46)
        C6_PCM_TAP(26, -52, -50)
        : [left] "+&r" (left), [right] "+&r" (right)
        : [coefficients] "r" (coefficients), [samples] "r" (current)
        : "t0", "t1", "t2", "memory"
    );
#undef C6_PCM_TAP
    return {left >> 10, right >> 10};
}

static inline __attribute__((always_inline)) int16_t emitSample(StereoQ4 sample, const uint8_t* envelope) {
    interpolationPhase += 3;
    return mix(sample.left, sample.right, envelope);
}

// Preparation keeps the current pair and packet pinned while the radio
// crosses an edge. The caller must finish it before loading another pair.
static void IRAM_ATTR __attribute__((optimize("O2"), unused)) prepareSample() {
    preparedSample = interpolateSample();
}

static int16_t IRAM_ATTR __attribute__((optimize("O2"), unused)) finishSample(const uint8_t* envelope) {
    return emitSample(preparedSample, envelope);
}

static int16_t IRAM_ATTR __attribute__((optimize("O2"), unused)) renderSample(const uint8_t* envelope) {
    return emitSample(interpolateSample(), envelope);
}

}  // namespace c6_pcm_tx_dsp
