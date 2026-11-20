/* SPDX-License-Identifier: GPL-3.0-or-later
 * CB V16: 4 signed 10-bit MPX samples per 5 octets, little-endian bit order.
 * No reserved octets; byte 0x80 is valid audio data in this protocol.
 */
#pragma once
#include <stdint.h>
#define C5_PCM10_GROUP_BYTES 5u
#define C5_PCM10_GROUP_SAMPLES 4u

static inline int16_t c5_pcm10_sign_extend(uint16_t raw) {
    return (int16_t)((int32_t)(raw ^ 0x0200u) - 0x0200);
}
static inline void c5_pcm10_decode(const uint8_t bytes[5], int16_t samples[4]) {
    const uint16_t a = (uint16_t)bytes[0] | (uint16_t)((bytes[1] & 0x03u) << 8);
    const uint16_t b = (uint16_t)(bytes[1] >> 2) | (uint16_t)((bytes[2] & 0x0fu) << 6);
    const uint16_t c = (uint16_t)(bytes[2] >> 4) | (uint16_t)((bytes[3] & 0x3fu) << 4);
    const uint16_t d = (uint16_t)(bytes[3] >> 6) | (uint16_t)((uint16_t)bytes[4] << 2);
    samples[0] = c5_pcm10_sign_extend(a);
    samples[1] = c5_pcm10_sign_extend(b);
    samples[2] = c5_pcm10_sign_extend(c);
    samples[3] = c5_pcm10_sign_extend(d);
}
