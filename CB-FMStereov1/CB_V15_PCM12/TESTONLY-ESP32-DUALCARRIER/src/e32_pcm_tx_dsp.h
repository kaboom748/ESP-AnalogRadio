/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdint.h>
#include "e32_tx.h"
#include "esp_attr.h"

namespace e32_pcm_tx_dsp {

constexpr unsigned RdsSideSamples = 30;

// 36 -> 120 kHz Q14 interpolator. The 17.7 kHz sinc uses a Kaiser window
// (beta 4.6) and eight input samples of delay. Mirror the fractional phases
// and normalize each coefficient row to 16384 for unity gain at DC.
alignas(4) static const DRAM_ATTR int16_t Fir[10][16] = {
    {0, 37, -71, 113, -158, 202, -240, 264, 16090, 264, -240, 202, -158, 113, -71, 37},
    {-26, 72, -144, 249, -398, 614, -983, 1996, 15854, -1158, 437, -186, 72, -19, -1, 5},
    {-39, 104, -212, 377, -624, 1010, -1728, 3971, 15098, -2226, 996, -518, 272, -135, 60, -22},
    {-49, 131, -267, 481, -811, 1348, -2402, 6096, 13877, -2920, 1402, -769, 426, -224, 107, -42},
    {-57, 147, -301, 547, -936, 1587, -2928, 8265, 12270, -3245, 1634, -924, 525, -283, 137, -54},
    {-59, 150, -308, 564, -978, 1692, -3231, 10362, 10362, -3231, 1692, -978, 564, -308, 150, -59},
    {-54, 137, -283, 525, -924, 1634, -3245, 12270, 8265, -2928, 1587, -936, 547, -301, 147, -57},
    {-42, 107, -224, 426, -769, 1402, -2920, 13877, 6096, -2402, 1348, -811, 481, -267, 131, -49},
    {-22, 60, -135, 272, -518, 996, -2226, 15098, 3971, -1728, 1010, -624, 377, -212, 104, -39},
    {5, -1, -19, 72, -186, 437, -1158, 15854, 1996, -983, 614, -398, 249, -144, 72, -26},
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
    // Preserve the four fractional bits through the handoff to the RF core.
    const int32_t mpx = programme + Pilot[mpxPhase] + rds;
    if (++mpxPhase == 120) mpxPhase = 0;
    if (mpx > E32_TX_MPX_Q4_FULL_SCALE) return E32_TX_MPX_Q4_FULL_SCALE;
    if (mpx < -E32_TX_MPX_Q4_FULL_SCALE) return -E32_TX_MPX_Q4_FULL_SCALE;
    return int16_t(mpx);
}

// The Classic LX6 MAC16 reads two signed halfwords from an aligned word.
// Process each channel in order using its 40-bit accumulator. The sum of
// sixteen products fits in 32 bits, so ACCLO contains the complete result.
#define E32_FIR_MAC_PAIR(First, Second, CoefficientOffset, Newer, Older) \
    "l32i %[coefficient], %[coefficients], " #CoefficientOffset "\n\t" \
    "l32i %[sample], %[pairs], " #Newer "\n\t" \
    First " %[sample], %[coefficient]\n\t" \
    "l32i %[sample], %[pairs], " #Older "\n\t" \
    Second " %[sample], %[coefficient]\n\t"

#define E32_FIR_MAC_CHANNEL(Start, AccumulateLow, AccumulateHigh) \
    E32_FIR_MAC_PAIR(Start,         AccumulateHigh,  0, 60, 56) \
    E32_FIR_MAC_PAIR(AccumulateLow, AccumulateHigh,  4, 52, 48) \
    E32_FIR_MAC_PAIR(AccumulateLow, AccumulateHigh,  8, 44, 40) \
    E32_FIR_MAC_PAIR(AccumulateLow, AccumulateHigh, 12, 36, 32) \
    E32_FIR_MAC_PAIR(AccumulateLow, AccumulateHigh, 16, 28, 24) \
    E32_FIR_MAC_PAIR(AccumulateLow, AccumulateHigh, 20, 20, 16) \
    E32_FIR_MAC_PAIR(AccumulateLow, AccumulateHigh, 24, 12,  8) \
    E32_FIR_MAC_PAIR(AccumulateLow, AccumulateHigh, 28,  4,  0)

static inline __attribute__((always_inline)) void filterStereo(
    const Stereo* oldest, const int16_t* coefficients, int32_t& left, int32_t& right) {
    uint32_t sample, coefficient;
    asm volatile(
        E32_FIR_MAC_CHANNEL("mul.aa.ll", "mula.aa.ll", "mula.aa.lh")
        "rsr.ACCLO %[left]\n\t"
        E32_FIR_MAC_CHANNEL("mul.aa.hl", "mula.aa.hl", "mula.aa.hh")
        "rsr.ACCLO %[right]"
        : [left] "=&a" (left), [right] "=&a" (right), [sample] "=&a" (sample),
          [coefficient] "=&a" (coefficient)
        : [pairs] "a" (oldest), [coefficients] "a" (coefficients)
        : "memory");
}

#undef E32_FIR_MAC_CHANNEL
#undef E32_FIR_MAC_PAIR

// The envelope remains valid until the current 108-pair packet is consumed.
static int16_t IRAM_ATTR __attribute__((optimize("O2"), unused)) renderSampleQ4(const uint8_t* envelope) {
    const uint8_t phase = interpolationPhase;
    interpolationPhase += 3;
    const Stereo* current = &history[historyAt + 16];
    int32_t left, right;
    filterStereo(current - 15, Fir[phase], left, right);
    return mix((left + 512) >> 10, (right + 512) >> 10, envelope);
}

}  // namespace e32_pcm_tx_dsp
