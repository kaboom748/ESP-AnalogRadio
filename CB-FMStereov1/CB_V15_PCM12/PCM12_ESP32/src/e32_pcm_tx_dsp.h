/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdint.h>
#include "esp_attr.h"

namespace e32_pcm_tx_dsp {

constexpr unsigned RdsSideSamples = 30;
constexpr int32_t FullScale = 2047;

// The V5 36 -> 120 kHz Q14 polyphase interpolator. Phase zero is a delay.
alignas(4) static const DRAM_ATTR int16_t Fir[10][10] = {
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
    const int32_t mpx = (programme + Pilot[mpxPhase] + rds + 8) >> 4;
    if (++mpxPhase == 120) mpxPhase = 0;
    if (mpx > FullScale) return FullScale;
    if (mpx < -FullScale) return -FullScale;
    return int16_t(mpx);
}

// The Classic LX6 MAC16 reads two signed halfwords from an aligned word.
// Process each channel in order using its 40-bit accumulator. The sum of
// ten products fits in 32 bits, so ACCLO contains the complete result.
#define E32_FIR_MAC_PAIR(First, Second, Coefficient, Newer, Older) \
    "l32i %[sample], %[pairs], " #Newer "\n\t" \
    First " %[sample], %[" #Coefficient "]\n\t" \
    "l32i %[sample], %[pairs], " #Older "\n\t" \
    Second " %[sample], %[" #Coefficient "]\n\t"

static inline __attribute__((always_inline)) void filterStereo(
    const Stereo* oldest, const int16_t* coefficients, int32_t& left, int32_t& right) {
    uint32_t sample, c0, c1, c2, c3, c4;
    asm volatile(
        "l32i %[c0], %[coefficients], 0\n\t"
        "l32i %[c1], %[coefficients], 4\n\t"
        "l32i %[c2], %[coefficients], 8\n\t"
        "l32i %[c3], %[coefficients], 12\n\t"
        "l32i %[c4], %[coefficients], 16\n\t"
        E32_FIR_MAC_PAIR("mul.aa.ll", "mula.aa.lh", c0, 36, 32)
        E32_FIR_MAC_PAIR("mula.aa.ll", "mula.aa.lh", c1, 28, 24)
        E32_FIR_MAC_PAIR("mula.aa.ll", "mula.aa.lh", c2, 20, 16)
        E32_FIR_MAC_PAIR("mula.aa.ll", "mula.aa.lh", c3, 12, 8)
        E32_FIR_MAC_PAIR("mula.aa.ll", "mula.aa.lh", c4, 4, 0)
        "rsr.ACCLO %[left]\n\t"
        E32_FIR_MAC_PAIR("mul.aa.hl", "mula.aa.hh", c0, 36, 32)
        E32_FIR_MAC_PAIR("mula.aa.hl", "mula.aa.hh", c1, 28, 24)
        E32_FIR_MAC_PAIR("mula.aa.hl", "mula.aa.hh", c2, 20, 16)
        E32_FIR_MAC_PAIR("mula.aa.hl", "mula.aa.hh", c3, 12, 8)
        E32_FIR_MAC_PAIR("mula.aa.hl", "mula.aa.hh", c4, 4, 0)
        "rsr.ACCLO %[right]"
        : [left] "=&a" (left), [right] "=&a" (right), [sample] "=&a" (sample),
          [c0] "=&a" (c0), [c1] "=&a" (c1), [c2] "=&a" (c2), [c3] "=&a" (c3), [c4] "=&a" (c4)
        : [pairs] "a" (oldest), [coefficients] "a" (coefficients)
        : "memory");
}

#undef E32_FIR_MAC_PAIR

// The envelope remains valid until the current 108-pair packet is consumed.
static int16_t IRAM_ATTR __attribute__((optimize("O2"), unused)) renderSample(const uint8_t* envelope) {
    const uint8_t phase = interpolationPhase;
    interpolationPhase += 3;
    const Stereo* current = &history[historyAt + 16];
    if (phase == 0) {
        const Stereo delayed = current[-5];
        return mix(delayed.left * 16, delayed.right * 16, envelope);
    }
    int32_t left, right;
    filterStereo(current - 9, Fir[phase], left, right);
    return mix((left + 512) >> 10, (right + 512) >> 10, envelope);
}

static inline unsigned renderPair(const uint8_t* stereo, const uint8_t* envelope, int16_t output[4]) {
    loadPair(stereo);
    unsigned count = 0;
    do { output[count++] = renderSample(envelope); } while (pairPending());
    return count;
}

}  // namespace e32_pcm_tx_dsp
