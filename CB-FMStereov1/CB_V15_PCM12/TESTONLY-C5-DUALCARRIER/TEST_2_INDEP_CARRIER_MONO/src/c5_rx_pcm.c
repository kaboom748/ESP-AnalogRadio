/* SPDX-License-Identifier: GPL-3.0-or-later
 * Fixed-point stereo decoder and PCMR framing, adapted from PCM12bitV5.
 */
#include "c5_rx_pcm.h"
#include <math.h>
#include <string.h>
#include "esp_attr.h"

enum {
    HeaderBytes = 10,
    FramePairs = 120,
    RdsOffset = HeaderBytes + 3 * FramePairs,
    PhaseTaps = 13,
    OscillatorPeriod = 120,
};
_Static_assert(RdsOffset + 2 * 15 == C5_RX_PCM_FRAME_SIZE, "PCMR layout mismatch");

typedef struct { int16_t sine, cosine; } Phasor;
typedef struct { Phasor pilot, stereo, rds; } Carriers;
static DRAM_ATTR Phasor oscillators[OscillatorPeriod];
static DRAM_ATTR Carriers carriers[OscillatorPeriod];
static bool oscillator_tables_ready;

/* Phase rows run in input time order. The HQ filter passes 14.2 kHz audio
 * and places a zero at the 19 kHz pilot. MONO shares this audio response. */
static DRAM_ATTR const int16_t hq_taps[3][PhaseTaps] = {
    {-38, 135, -321, 629, -1156, 2698, 3930, -467, -13, 135, -131, 82, -34},
    {-68, 194, -388, 624, -855, 1026, 4373, 1026, -855, 624, -388, 194, -68},
    {-34, 82, -131, 135, -13, -467, 3930, 2698, -1156, 629, -321, 135, -38},
};
static DRAM_ATTR const int16_t robust_taps[3][PhaseTaps] = {
    {-46, 42, 178, -406, -99, 2460, 3141, 616, -563, 60, 126, -50, -10},
    {-28, -22, 188, -158, -491, 1539, 3392, 1539, -491, -158, 188, -22, -28},
    {-10, -50, 126, 60, -563, 616, 3141, 2460, -99, -406, 178, 42, -46},
};

typedef struct {
    uint32_t integrator[3], delay[3];
} RdsCic;

static DRAM_ATTR struct {
    c5_rx_pcm_emit_cb_t emit;
    c5_rx_pcm_angle_cb_t angle;
    void *user;
    const int16_t (*taps)[PhaseTaps];
    uint32_t index;
    uint16_t blend, missed_pilot_updates;
    uint8_t fill, oscillator, phase, history, pilot_ticks, pilot_confidence,
        pilot_rotation, rds_decimation, rds_offset;
    bool mono, locked, have_pilot_phase;
    Phasor correction;
    int32_t pilot_i, pilot_q, pilot_i2, pilot_q2, sum, difference;
    RdsCic rds_i, rds_q;
    int16_t mono_history[3][2 * PhaseTaps], stereo_history[3][2 * PhaseTaps];
    uint8_t frame[C5_RX_PCM_FRAME_SIZE];
} decoder;

void c5_rx_pcm_prepare(unsigned profile, c5_rx_pcm_angle_cb_t angle)
{
    if (!oscillator_tables_ready) {
        for (unsigned i = 0; i < OscillatorPeriod; ++i) {
            const float theta = (6.2831853071795864769f / OscillatorPeriod) * i;
            oscillators[i] = (Phasor){(int16_t)lroundf(4096.0f * sinf(theta)),
                                       (int16_t)lroundf(4096.0f * cosf(theta))};
        }
        for (unsigned i = 0; i < OscillatorPeriod; ++i) {
            carriers[i] = (Carriers){oscillators[(19u * i) % OscillatorPeriod],
                                     oscillators[(38u * i) % OscillatorPeriod],
                                     oscillators[(57u * i) % OscillatorPeriod]};
        }
        oscillator_tables_ready = true;
    }
    memset(&decoder, 0, sizeof(decoder));
    decoder.taps = profile == 1u ? robust_taps : hq_taps;
    decoder.mono = profile == 2u;
    decoder.angle = angle;
    decoder.correction.cosine = 4096;
    decoder.frame[0] = 'P'; decoder.frame[1] = 'C';
    decoder.frame[2] = 'M'; decoder.frame[3] = 'R';
}

void c5_rx_pcm_set_sink(c5_rx_pcm_emit_cb_t emit, void *user)
{
    decoder.emit = emit;
    decoder.user = user;
}

static inline __attribute__((always_inline)) void rds_input(RdsCic *cic, int32_t sample)
{
    /* CIC differences use modular arithmetic; the final signed value fits. */
    cic->integrator[0] += (uint32_t)sample;
    cic->integrator[1] += cic->integrator[0];
    cic->integrator[2] += cic->integrator[1];
}

static inline __attribute__((always_inline)) uint8_t rds_output(RdsCic *cic)
{
    uint32_t value = cic->integrator[2];
    for (unsigned stage = 0; stage < 3; ++stage) {
        const uint32_t previous = value;
        value -= cic->delay[stage];
        cic->delay[stage] = previous;
    }
    const int32_t code = (int32_t)value >> 16;
    return (uint8_t)(code < -127 ? -127 : code > 127 ? 127 : code);
}

static inline __attribute__((always_inline)) unsigned magnitude(int32_t n)
{ return (unsigned)(n < 0 ? -n : n); }

static void IRAM_ATTR update_pilot(void)
{
    const int32_t i = decoder.pilot_i2 >> 12;
    const int32_t q = decoder.pilot_q2 >> 12;
    const bool present = magnitude(i) + magnitude(q) >= 32u;
    bool coherent = false;
    if (present) {
        const uint16_t double_phase = (uint16_t)(2 * decoder.angle(i, q, 1, 0));
        unsigned rotation = ((uint32_t)double_phase * OscillatorPeriod + 32768u) >> 16;
        if (rotation == OscillatorPeriod) rotation = 0;
        const unsigned drift = magnitude((int32_t)rotation - decoder.pilot_rotation);
        coherent = !decoder.have_pilot_phase || drift <= 8u || drift >= 112u;
        if (coherent) decoder.correction = oscillators[rotation];
        decoder.pilot_rotation = rotation;
    }
    decoder.have_pilot_phase = present;
    if (coherent) {
        decoder.missed_pilot_updates = 0;
        if (decoder.pilot_confidence < 48) ++decoder.pilot_confidence;
        if (decoder.pilot_confidence == 48) decoder.locked = true;
    } else if (decoder.locked) {
        /* Revert to mono only after 0.4 s, to ride through narrow fades. */
        if (++decoder.missed_pilot_updates == 375u) {
            decoder.locked = false;
            decoder.pilot_confidence = 0;
        }
    } else {
        decoder.pilot_confidence = decoder.pilot_confidence < 4u ? 0 : decoder.pilot_confidence - 4u;
    }
}

static inline __attribute__((always_inline)) uint32_t packed12(int32_t value)
{ return (uint32_t)(value < -2048 ? -2048 : value > 2047 ? 2047 : value) & 0xfffu; }

void IRAM_ATTR c5_rx_pcm_sample(int32_t mpx, bool valid, uint16_t strength)
{
    const unsigned phase = decoder.phase;
    const unsigned history = decoder.history;
    if (!decoder.fill && !phase) {
        const uint32_t index = decoder.index;
        for (unsigned i = 0; i < 4; ++i) decoder.frame[4 + i] = index >> (8u * i);
        decoder.frame[8] = strength;
        decoder.frame[9] = ((strength >> 8) & 0x7fu) | (decoder.locked && !decoder.mono ? 0x80u : 0);
    }

    const Carriers *tone = &carriers[decoder.oscillator];
    if (++decoder.oscillator == OscillatorPeriod) decoder.oscillator = 0;
    int32_t difference = 0;
    if (!valid) mpx = 0;
    decoder.pilot_i += (mpx * tone->pilot.sine - decoder.pilot_i) >> 10;
    decoder.pilot_q += (mpx * tone->pilot.cosine - decoder.pilot_q) >> 10;
    decoder.pilot_i2 += (decoder.pilot_i - decoder.pilot_i2) >> 10;
    decoder.pilot_q2 += (decoder.pilot_q - decoder.pilot_q2) >> 10;
    rds_input(&decoder.rds_i, (mpx * tone->rds.cosine) >> 12);
    rds_input(&decoder.rds_q, (mpx * tone->rds.sine) >> 12);
    if (valid && !decoder.mono) {
        const Phasor correction = decoder.correction;
        const int32_t carrier = (tone->stereo.sine * correction.cosine +
                                 tone->stereo.cosine * correction.sine) >> 12;
        difference = (mpx * carrier) >> 11;
    }
    if (++decoder.rds_decimation == 24) {
        decoder.rds_decimation = 0;
        decoder.frame[RdsOffset + decoder.rds_offset++] = rds_output(&decoder.rds_i);
        decoder.frame[RdsOffset + decoder.rds_offset++] = rds_output(&decoder.rds_q);
    }

    int16_t *sum_history = decoder.mono_history[phase];
    int16_t *difference_history = decoder.stereo_history[phase];
    sum_history[history + PhaseTaps] = sum_history[history] = (int16_t)mpx;
    difference_history[history + PhaseTaps] = difference_history[history] = (int16_t)difference;
    if (valid) {
        const int16_t *coefficients = decoder.taps[phase];
        const int16_t *sum = sum_history + history + PhaseTaps;
        int32_t m = 0, d = 0;
        if (decoder.mono) {
#pragma GCC unroll 13
            for (unsigned tap = 0; tap < PhaseTaps; ++tap) m += coefficients[tap] * *sum--;
        } else {
            const int16_t *diff = difference_history + history + PhaseTaps;
#pragma GCC unroll 13
            for (unsigned tap = 0; tap < PhaseTaps; ++tap) {
                const int16_t coefficient = coefficients[tap];
                m += coefficient * *sum--;
                d += coefficient * *diff--;
            }
        }
        decoder.sum += m;
        decoder.difference += d;
    }
    if (++decoder.pilot_ticks == 128) {
        decoder.pilot_ticks = 0;
        update_pilot();
    }
    if (++decoder.phase < 3) return;

    decoder.phase = 0;
    if (++decoder.history == PhaseTaps) decoder.history = 0;
    const uint16_t target = decoder.locked && !decoder.mono ? 256 : 0;
    if (decoder.blend < target) ++decoder.blend;
    else if (decoder.blend > target) --decoder.blend;
    const int32_t stereo = ((decoder.difference >> 10) * decoder.blend) >> 8;
    const int32_t mono = decoder.sum >> 10;
    const uint32_t left = packed12((mono + stereo + 64) >> 7);
    const uint32_t right = packed12((mono - stereo + 64) >> 7);
    uint8_t *packed = decoder.frame + HeaderBytes + 3u * decoder.fill;
    packed[0] = left;
    packed[1] = (left >> 8) | (right << 4);
    packed[2] = right >> 4;
    decoder.sum = 0;
    decoder.difference = 0;
    ++decoder.index;
    if (++decoder.fill == FramePairs) {
        decoder.emit(decoder.frame, decoder.user);
        decoder.fill = 0;
        decoder.rds_offset = 0;
    }
}
