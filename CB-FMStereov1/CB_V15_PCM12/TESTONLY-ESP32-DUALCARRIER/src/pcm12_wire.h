/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdint.h>
#include "esp_attr.h"
#include "e32_dram_cursor.h"

namespace pcm12 {

constexpr unsigned RxRate = 40000;
constexpr unsigned RxFrameSize = 400;
constexpr unsigned TxPairs = 108;
constexpr unsigned TxAudioBytes = 3 * TxPairs;
constexpr unsigned TxRdsBytes = 30;
constexpr unsigned TxBodySize = 4 + TxAudioBytes + TxRdsBytes;
constexpr unsigned MpxPerFrame = 360;
constexpr unsigned IngressFrames = 16;
constexpr unsigned IngressMask = IngressFrames - 1;
constexpr unsigned CreditWindow = 20;
constexpr unsigned CreditStep = 4;

enum class TxError : uint8_t { None, Magic, Sequence, Queue, Credit, Uart };

struct Ingress {
    uint8_t frames[IngressFrames][TxBodySize];
    volatile uint32_t read = 0, write = 0;
    uint32_t nextIndex = 0, peak = 0;
    uint32_t granted = 0, played = 0, playedSamples = 0;
    uint32_t lastGrant = 0, lastCheck = 0;
    uint16_t bodyBytes = 0;
    uint8_t magicBytes = 0, pair = 0;
    bool indexed = false, flow = false, ready = false;
    TxError error = TxError::None;

    uint32_t readIndex() const { return e32_dram_load(&read); }
    uint32_t writeIndex() const { return e32_dram_load(&write); }
    void releaseFrame(uint32_t index) { e32_dram_store(&read, index + 1); }

    void reset(bool enableFlow) {
        e32_dram_store(&read, 0);
        e32_dram_store(&write, 0);
        nextIndex = peak = 0;
        granted = played = playedSamples = lastGrant = 0;
        lastCheck = 0;
        bodyBytes = magicBytes = pair = 0;
        indexed = ready = false;
        flow = enableFlow;
        error = TxError::None;
    }

    void IRAM_ATTR push(uint8_t byte) {
        if (error != TxError::None) return;
        constexpr uint32_t magic = UINT32_C(0x544d4350); // PCMT, little endian.
        if (magicBytes < 4) {
            if (byte != uint8_t(magic >> (8 * magicBytes))) {
                magicBytes = byte == 'P' ? 1 : 0;
                if (indexed) error = TxError::Magic;
                return;
            }
            if (++magicBytes != 4) return;
            if (flow && write == granted) error = TxError::Credit;
            else if (write - readIndex() >= IngressFrames) error = TxError::Queue;
            return;
        }

        auto& frame = frames[write & IngressMask];
        frame[bodyBytes++] = byte;
        if (bodyBytes != TxBodySize) return;
        bodyBytes = magicBytes = 0;
        const uint32_t index = uint32_t(frame[0]) | uint32_t(frame[1]) << 8 |
                               uint32_t(frame[2]) << 16 | uint32_t(frame[3]) << 24;
        if (indexed && index != nextIndex) {
            error = TxError::Sequence;
            return;
        }
        nextIndex = index + TxPairs;
        indexed = true;
        const uint32_t next = write + 1;
        e32_dram_store(&write, next);
        const uint32_t queued = next - readIndex();
        if (queued > peak) peak = queued;
    }

    uint32_t IRAM_ATTR credit(uint32_t consumedSamples) {
        const uint32_t framesConsumed = (consumedSamples - playedSamples) / MpxPerFrame;
        played += framesConsumed;
        playedSamples += framesConsumed * MpxPerFrame;
        const uint32_t ingressLimit = readIndex() + IngressFrames;
        const uint32_t rfLimit = played + CreditWindow;
        return int32_t(ingressLimit - rfLimit) < 0 ? ingressLimit : rfLimit;
    }
};

inline const char* errorText(TxError error) {
    switch (error) {
    case TxError::Magic: return "TX_PCM_MAGIC";
    case TxError::Sequence: return "TX_PCM_SEQUENCE";
    case TxError::Queue: return "TX_PCM_QUEUE";
    case TxError::Credit: return "TX_PCM_CREDIT";
    case TxError::Uart: return "TX_PCM_UART";
    default: return nullptr;
    }
}

} // namespace pcm12
