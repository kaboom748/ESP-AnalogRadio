/*
 * Continuous sampled FM link over warm complementary TDD slots, ESP8266 160 MHz.
 * Both boards run this sketch. Host supplies signed 8-bit PCM over full-duplex
 * UART. START A is the slot clock source; START B follows markers heard over RF.
 * START L is passive receive-only monitoring: downsample alternate RF slots
 * to 1/2 rate each, so both directions fit the original 460800-baud UART.
 * V2: incoming PCM is gated by genuine RF sync; predicted missing slots
 * remain silent, rather than demodulating the sender's own RF after TX.
 * TX modulation and warm RF switching remain unchanged from V22.
 * RF register access is based on the measured RadioBidi bench and FMFileModem.
 */
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <stdlib.h>
#include <string.h>
extern "C" {
#include "user_interface.h"
void set_rf_freq_offset(int xtal, int mhz, int off);
extern uint8_t chip6_phy_init_ctrl[];
}

namespace {
constexpr uint32_t cpuHz = 160000000, baud = 460800, activeHz = 120000;
constexpr uint16_t markerSamples = 12, txCapacity = 8192, payload = 128;
constexpr uint16_t packetBytes = 16 + payload + 2;
constexpr uint8_t rxFrames = 8;
// 8N2 uses eleven bits per octet. Keep one additional bit of idle time.
constexpr uint32_t uartPacedBits = 12;
constexpr uint32_t uartByteCycles = (uartPacedBits * cpuHz + baud - 1u) / baud;
constexpr uint32_t iqPeriod = cpuHz / (2 * activeHz);
constexpr uint32_t uartFifo = 0x60000000, uartIntRaw = 0x60000004;
constexpr uint32_t uartIntEnable = 0x6000000C, uartIntClear = 0x60000010;
constexpr uint32_t uartStatus = 0x6000001C, uartConf0 = 0x60000020;
constexpr uint32_t uartErrorMask = 0x1cu;
constexpr uint32_t tone1 = 0x600005B8, tone2 = 0x600005BC, tone3 = 0x600005C4;
constexpr uint32_t pbusCmd = 0x60000594, pbusStat = 0x600005A0;
constexpr uint32_t rxCtrl = 0x60009B08, macIntEnable = 0x3FF20C18;
constexpr uint32_t iqCtrl = 0x6000057C, iqSumI = 0x600005DC, iqSumQ = 0x600005E0;
constexpr uint32_t kMask = 0x3ff, scaleMask = 0x0003fc00, gateMask = 0x00040000;
constexpr uint32_t rxStop = 0x08000000, iqMask = 0xFFFA0001, iqDone = 0x80000000;
constexpr uint8_t pbusSelectors[] = {2, 7, 1, 6};
constexpr uint16_t txPbusValues[] = {1, 95, 127, 127};
const auto setTxClock = reinterpret_cast<void(*)(int)>(0x4000650Cu);
const auto setScale = reinterpret_cast<uint8_t(*)(uint8_t)>(0x4000678Cu);
const auto readPbus = reinterpret_cast<uint32_t(*)(uint32_t, uint32_t)>(0x400074D8u);

struct Config {
  bool master, listenOnly;
  uint16_t data, guard;
  uint32_t lo;
  uint8_t power, rfGain, vga, ask, ifK, bbGain;
};
struct Radio {
  uint32_t tone[3], command, rx, iq, mac;
  uint16_t pbus[4];
};
struct Rx {
  uint32_t nextIq, iqRemainder, audioDeadline;
  int16_t phaseHistory[32], iHistory[32], qHistory[32];
  uint32_t timeHistory[32];
  uint8_t histIndex, histCount;
  int32_t oldI, oldQ, dcI, dcQ;
  int16_t center, audioCenter, calI, calQ;
  uint32_t calTime;
  uint16_t samples, validSamples;
  bool previous, audio, haveCenter, haveAudioCenter, calibrate;
  bool markerConfirmed; // distinguish actual Slave RF from predicted empty RX slots
  int8_t polarity;
};
struct Stream {
  bool stop, fault, timeline, locked, peerSeen, listener;
  uint8_t listenSlot; // first detected RF slot is channel 0
  uint32_t lastOtherMarker;
  uint32_t txHead, txTail, rxIndex, received, uartErrors, rxDrops, txOverflow;
  uint32_t slotsTx, slotsRx, misses, lockCount, markers;
  uint32_t iqTimeouts, iqLate, iqLateMax, iqRails;
  uint32_t nextTx, nextPeer, lastLock, nextUart, lastHostData;
  uint16_t partial, packetOffset, crc;
  uint8_t readFrame, writeFrame;
  uint16_t readyLength, readySent;
  const char* ready;
};
bool phyReady = false;
uint32_t tuned = 0;
uint8_t txRing[txCapacity];
uint8_t rxPackets[rxFrames][packetBytes];
uint16_t angles[257], crcTable[256];
uint32_t reciprocals[256];
int16_t pulseWidths[256];

static inline __attribute__((always_inline)) uint32_t nowCycles() {
  uint32_t value;
  asm volatile("rsr %0,ccount" : "=a"(value));
  return value;
}
static inline __attribute__((always_inline)) uint32_t reg(uint32_t address) {
  return *reinterpret_cast<volatile uint32_t*>(address);
}
static inline __attribute__((always_inline)) void reg(uint32_t address, uint32_t value) {
  *reinterpret_cast<volatile uint32_t*>(address) = value;
}
static inline __attribute__((always_inline)) void fence() {
  asm volatile("memw" ::: "memory");
}
static inline __attribute__((always_inline)) bool before(uint32_t a, uint32_t b) {
  return static_cast<int32_t>(a - b) < 0;
}
static inline __attribute__((always_inline)) int32_t magnitude(int32_t x) {
  return x < 0 ? -x : x;
}
static inline __attribute__((always_inline)) uint32_t duration(uint32_t samples) {
  return (4000u * samples) / 3u;
}
static inline __attribute__((always_inline)) void put16(uint8_t* out, uint16_t v) {
  out[0] = v; out[1] = v >> 8;
}
static inline __attribute__((always_inline)) void put32(uint8_t* out, uint32_t v) {
  out[0] = v; out[1] = v >> 8; out[2] = v >> 16; out[3] = v >> 24;
}

static void IRAM_ATTR pump(Stream& s) {
  const uint32_t status = reg(uartStatus);
  if (status & 0xffu) {
    const uint8_t byte = reg(uartFifo);
    if (byte == 0x80) {
      s.stop = true;
    } else if (s.txHead - s.txTail < txCapacity) {
      txRing[s.txHead++ & (txCapacity - 1)] = byte;
      ++s.received;
      s.lastHostData = nowCycles();
    } else {
      ++s.txOverflow;
    }
  }
  const uint32_t errors = reg(uartIntRaw) & uartErrorMask;
  if (errors) {
    ++s.uartErrors;
    reg(uartIntClear, errors);
  }
  if (((status >> 16) & 0xffu) >= 120 || before(nowCycles(), s.nextUart)) return;
  if (s.readySent != s.readyLength) {
    reg(uartFifo, s.ready[s.readySent++]);
  } else if (s.readFrame != s.writeFrame) {
    reg(uartFifo, rxPackets[s.readFrame][s.packetOffset++]);
    if (s.packetOffset == packetBytes) {
      s.packetOffset = 0;
      s.readFrame = (s.readFrame + 1) & (rxFrames - 1);
    }
  } else {
    return;
  }
  s.nextUart = nowCycles() + uartByteCycles;
}

static inline __attribute__((always_inline)) void waitUntil(uint32_t deadline, Stream& s) {
  while (before(nowCycles(), deadline) && !s.stop) pump(s);
}

static inline __attribute__((always_inline)) void spinUntil(uint32_t deadline) {
  while (before(nowCycles(), deadline)) {}
}

static void IRAM_ATTR outputSample(Stream& s, uint8_t byte) {
  const uint32_t index = s.rxIndex++;
  const uint8_t next = (s.writeFrame + 1) & (rxFrames - 1);
  if (!s.partial && next == s.readFrame) {
    ++s.rxDrops;
    return;
  }
  uint8_t* frame = rxPackets[s.writeFrame];
  if (!s.partial) {
    frame[0] = 'D'; frame[1] = 'P'; frame[2] = 'X'; frame[3] = '1';
    put32(frame + 4, index);
    put32(frame + 8, s.txTail);
    put16(frame + 12, payload);
    // Bit 2 reports recent observed activity in the second TDD slot.
    // It is advisory: passive role has no out-of-band station identity.
    const bool otherActive = s.listener && s.lastOtherMarker &&
        static_cast<uint32_t>(nowCycles() - s.lastOtherMarker) < cpuHz / 2;
    put16(frame + 14, (s.locked ? 1 : 0) | (s.peerSeen ? 2 : 0) | (otherActive ? 4 : 0));
    s.crc = 0xffff;
    for (uint8_t i = 4; i < 16; ++i) s.crc = (s.crc << 8) ^ crcTable[(s.crc >> 8) ^ frame[i]];
  }
  frame[16 + s.partial++] = byte;
  s.crc = (s.crc << 8) ^ crcTable[(s.crc >> 8) ^ byte];
  if (s.partial != payload) return;
  put16(frame + 16 + payload, s.crc);
  s.partial = 0;
  s.writeFrame = next;
}

static void prepareTables() {
  for (uint16_t i = 0; i < 256; ++i) {
    uint16_t crc = i << 8;
    for (uint8_t j = 0; j < 8; ++j) crc = (crc << 1) ^ ((crc & 0x8000) ? 0x1021 : 0);
    crcTable[i] = crc;
    reciprocals[i] = (1u << 24) / (256u + i);
    const int32_t pcm = static_cast<int8_t>(i);
    // 25 kHz full-scale FM. Center a dither pulse in each active PCM period.
    const int32_t width = (pcm * 1280) / 381;
    pulseWidths[i] = static_cast<int16_t>(width);
  }
  for (uint16_t i = 0; i <= 256; ++i) angles[i] = lroundf(atanf(i / 256.0f) * (32768.0f / PI));
}

static bool IRAM_ATTR writePbus(uint8_t selector, uint16_t value) {
  const uint32_t command = (reg(pbusCmd) & 0xffff0001) | (1u << 14) |
                           ((value & 0x1ffu) << 5) | (selector << 2) | 2u;
  reg(pbusCmd, command);
  fence();
  const uint32_t deadline = nowCycles() + 400000;
  while (reg(pbusStat) & 0x80000000u) {
    if (!before(nowCycles(), deadline)) {
      reg(pbusCmd, reg(pbusCmd) & ~2u);
      return false;
    }
  }
  reg(pbusCmd, reg(pbusCmd) & ~2u);
  fence();
  return true;
}

static bool IRAM_ATTR prepareTx(const Radio& saved) {
  reg(iqCtrl, (reg(iqCtrl) & iqMask & ~1u) | 0x1000);
  reg(rxCtrl, saved.rx | rxStop);
  reg(pbusCmd, (saved.command & ~2u) | 1u);
  fence();
  for (uint8_t i = 0; i < 4; ++i) {
    if (saved.pbus[i] != txPbusValues[i] && !writePbus(pbusSelectors[i], txPbusValues[i])) return false;
  }
  return true;
}

static bool IRAM_ATTR restoreRx(const Radio& saved, bool last) {
  if (last) setTxClock(0);
  reg(pbusCmd, reg(pbusCmd) | 1u);
  bool ok = true;
  for (int8_t i = 3; i >= 0; --i) {
    if (last || saved.pbus[i] != txPbusValues[i]) ok = writePbus(pbusSelectors[i], saved.pbus[i]) && ok;
  }
  reg(pbusCmd, saved.command & ~2u);
  reg(rxCtrl, saved.rx);
  reg(iqCtrl, saved.iq | 1u);
  fence();
  return ok;
}

static inline __attribute__((always_inline)) int16_t phase(int32_t i, int32_t q, int32_t pi, int32_t pq) {
  const int32_t dot = i * pi + q * pq;
  const int32_t cross = q * pi - i * pq;
  const uint32_t a = magnitude(dot), b = magnitude(cross);
  const uint32_t hi = a >= b ? a : b, lo = a >= b ? b : a;
  if (!hi) return 0;
  const uint8_t shift = __builtin_clz(hi);
  const uint32_t ratio = (((lo << shift) >> 16) * reciprocals[((hi << shift) >> 23) - 256]) >> 23;
  int32_t result = angles[ratio > 256 ? 256 : ratio];
  if (b > a) result = 16384 - result;
  if (dot < 0) result = 32768 - result;
  return static_cast<int16_t>(cross < 0 ? -result : result);
}

static inline __attribute__((always_inline)) int32_t iqValue(uint32_t address) {
  const int32_t raw = static_cast<int32_t>(reg(address)) >> 7;
  return raw < -15000 ? -15000 : raw > 15000 ? 15000 : raw;
}

static inline __attribute__((always_inline)) void stepIq(Rx& rx) {
  // A 240 kHz scan resolves the marker. Audio uses only the endpoints of
  // each 120 kHz MPX interval, which leaves the time between them for UART.
  rx.nextIq += rx.audio ? 1333 : 666;
  rx.iqRemainder += rx.audio ? 1 : 2;
  if (rx.iqRemainder >= 3) {
    rx.iqRemainder -= 3;
    ++rx.nextIq;
  }
}

static void IRAM_ATTR skipPeer(Stream& s, Rx& rx, const Config& cfg, uint32_t period) {
  s.peerSeen = false;
  const bool partial = rx.audio;
  // Passive monitor keeps half the samples of EACH slot to fit the UART.
  const uint16_t emitted = partial && rx.samples != UINT16_MAX
      ? (cfg.listenOnly ? (rx.samples + 1) / 2 : rx.samples) : 0;
  const uint16_t target = cfg.listenOnly ? cfg.data / 2 : cfg.data;
  for (uint16_t i = emitted; i < target; ++i) {
    // A predicted RX window is not proof that anyone transmitted.
    outputSample(s, partial && rx.markerConfirmed ? 0x80 : 0);
  }
  ++s.misses;
  s.nextPeer += cfg.listenOnly ? period / 2 : period;
  if (cfg.listenOnly) s.listenSlot ^= 1;
  rx.audio = false;
  rx.markerConfirmed = false;
  rx.samples = 0;
  // A listen-only station has only a four-sample guard before the next slot.
  // Keep its scan history after a missed slot so the following marker can lock.
  if (!cfg.listenOnly) rx.histCount = 0;
}

static inline __attribute__((always_inline)) int16_t history(const Rx& rx, uint8_t offset) {
  return rx.phaseHistory[(rx.histIndex - offset) & 31];
}

static inline __attribute__((always_inline)) void beginAudio(Rx& rx, const Config& cfg, uint32_t gate,
                                                               bool markerConfirmed) {
  // Never demodulate stale RF following our own TX as a real remote voice.
  rx.markerConfirmed = markerConfirmed;
  rx.nextIq = gate + duration(markerSamples) - 360;
  rx.audioDeadline = gate + duration(markerSamples + cfg.data) + 1800;
  rx.calibrate = false;
  // Compare two raw IQ endpoints in the final unmodulated header plateau.
  // Scan cadence can vary, so record the actual elapsed time for calibration.
  uint32_t best = 401;
  for (uint8_t offset = 0; offset < min(rx.histCount, static_cast<uint8_t>(6)); ++offset) {
    const uint8_t position = (rx.histIndex - offset) & 31;
    const uint32_t elapsed = rx.nextIq - rx.timeHistory[position];
    if (elapsed < 933 || elapsed > 1733) continue;
    const uint32_t error = magnitude(static_cast<int32_t>(elapsed) - 1333);
    if (error >= best) continue;
    best = error;
    rx.calTime = rx.timeHistory[position];
    rx.calI = rx.iHistory[position];
    rx.calQ = rx.qHistory[position];
    rx.calibrate = true;
  }
  rx.iqRemainder = 0;
  rx.previous = false;
  rx.audio = true;
  // The first IQ is the left endpoint of sample 0 and has no output.
  rx.samples = UINT16_MAX;
  rx.validSamples = 0;
  rx.histCount = 0;
}

static bool IRAM_ATTR marker(Rx& rx, uint32_t& start, int16_t& dc, int8_t& direction) {
  if (rx.histCount < 17) return false;
  const int16_t low = history(rx, 1);
  const int16_t h = static_cast<int16_t>(history(rx, 7) - low);
  const int16_t edgeA = static_cast<int16_t>(history(rx, 6) - low);
  const int16_t edgeB = static_cast<int16_t>(history(rx, 8) - low);
  if (magnitude(h) < 14000 || magnitude(h) > 28500 ||
      magnitude(static_cast<int16_t>(edgeA - h)) > 4600 ||
      magnitude(static_cast<int16_t>(edgeB - h)) > 4600 ||
      magnitude(static_cast<int16_t>(history(rx, 0) - low)) > 4600 ||
      magnitude(static_cast<int16_t>(history(rx, 2) - low)) > 4600 ||
      magnitude(static_cast<int16_t>(history(rx, 15) - low)) > 6800) return false;
  direction = h < 0 ? -1 : 1;
  dc = low + (static_cast<int16_t>(history(rx, 0) - low) +
              static_cast<int16_t>(history(rx, 2) - low)) / 3;
  for (int8_t offset = 5; offset >= 1; --offset) {
    const int32_t left = direction * static_cast<int16_t>(history(rx, offset + 1) - dc);
    const int32_t right = direction * static_cast<int16_t>(history(rx, offset) - dc);
    if (left > magnitude(h) / 2 && right <= magnitude(h) / 2) {
      const uint32_t end = rx.timeHistory[(rx.histIndex - offset) & 31];
      // The derivative represents the preceding IQ interval. If the
      // falling edge bisects adjacent averages, interpolate around midpoint.
      int32_t edgeAdjust = ((left + right - magnitude(h)) * static_cast<int32_t>(iqPeriod)) / magnitude(h);
      if (edgeAdjust < -static_cast<int32_t>(iqPeriod / 2)) edgeAdjust = -static_cast<int32_t>(iqPeriod / 2);
      if (edgeAdjust > static_cast<int32_t>(iqPeriod / 2)) edgeAdjust = iqPeriod / 2;
      start = end - iqPeriod + edgeAdjust - duration(8);
      return true;
    }
  }
  return false;
}

static void IRAM_ATTR processIq(Stream& s, Rx& rx, const Config& cfg, uint32_t half,
                                uint32_t period, uint32_t estimator) {
  const uint32_t timestamp = nowCycles();
  reg(iqCtrl, estimator | 2u);
  const uint32_t lateness = timestamp - rx.nextIq;
  if (lateness > 260) ++s.iqLate;
  if (lateness > s.iqLateMax) s.iqLateMax = lateness;
  const uint32_t iqTimeout = timestamp + (rx.audio ? 780 : 510);
  while (!(reg(iqCtrl) & iqDone) && before(nowCycles(), iqTimeout)) pump(s);
  const bool done = reg(iqCtrl) & iqDone;
  if (!done) ++s.iqTimeouts;
  int32_t i = 0, q = 0;
  if (done) {
    i = iqValue(iqSumI);
    q = iqValue(iqSumQ);
    if (rx.audio && (magnitude(i) >= 1700 || magnitude(q) >= 1700)) ++s.iqRails;
  }
  reg(iqCtrl, estimator);
  stepIq(rx);
  if (!rx.audio && before(rx.nextIq, nowCycles())) {
    rx.nextIq = nowCycles() + 40;
    rx.iqRemainder = 0;
  }
  // DC tracks noise between RF slots and is frozen during a valid waveform.
  if (done && !rx.audio && magnitude(i - (rx.dcI >> 8)) + magnitude(q - (rx.dcQ >> 8)) < 170) {
    rx.dcI += (i * 256 - rx.dcI) >> 7;
    rx.dcQ += (q * 256 - rx.dcQ) >> 7;
  }
  i -= rx.dcI >> 8;
  q -= rx.dcQ >> 8;
  const bool valid = done && (!rx.audio || lateness <= 360) && magnitude(i) + magnitude(q) >= 36;
  const bool hasPhase = rx.previous && valid;
  const int16_t turn = hasPhase ? phase(i, q, rx.oldI, rx.oldQ) : 0;
  rx.previous = valid;
  rx.oldI = i;
  rx.oldQ = q;
  if (rx.audio) {
    if (rx.samples == UINT16_MAX) {
      if (valid && rx.calibrate) {
        const int32_t elapsed = timestamp - rx.calTime;
        // Use the scan's rough estimate to unwrap the variable-length
        // calibration interval. Keep the calibrated 120 kHz result modulo 2π.
        const int32_t scanHint = 2 * rx.center;
        const int32_t hint = rx.haveAudioCenter ? scanHint + static_cast<int16_t>(rx.audioCenter - scanHint) : scanHint;
        const int32_t expected = (hint * elapsed) / 1333;
        const int16_t observed = phase(i, q, rx.calI, rx.calQ);
        const int32_t unwrapped = expected + static_cast<int16_t>(observed - expected);
        const int16_t baseline = (unwrapped * 1333) / elapsed;
        if (rx.haveAudioCenter) rx.audioCenter += static_cast<int16_t>(baseline - rx.audioCenter) / 8;
        else rx.audioCenter = baseline;
        rx.haveAudioCenter = true;
      }
      rx.samples = 0;
      return;
    }
    // An ordinary active radio forwards all its received samples.
    // The passive listener forwards every second physical sample from BOTH
    // the Master and Slave slots, preserving one logical UART sample stream.
    const bool forward = !cfg.listenOnly || !(rx.samples & 1u);
    if (!rx.markerConfirmed) {
      // A scheduled slot without an actual CRC-free timing marker is silence.
      // Hardware IQ can contain residual self-TX energy or random noise.
      if (forward) outputSample(s, 0);
    } else if (!hasPhase) {
      if (forward) outputSample(s, 0x80);
    } else {
      const int16_t swing = static_cast<int16_t>(turn - (rx.haveAudioCenter ? rx.audioCenter : 2 * rx.center));
      const int32_t pcm = (rx.polarity * swing * 38) >> 12;
      if (forward) outputSample(s, static_cast<uint8_t>(pcm > 127 ? 127 : pcm < -127 ? -127 : pcm));
      ++rx.validSamples;
    }
    if (++rx.samples == cfg.data) {
      rx.audio = false;
      rx.samples = 0;
      rx.histCount = 0;
      s.peerSeen = rx.markerConfirmed && rx.validSamples > cfg.data / 2;
      if (s.peerSeen) ++s.slotsRx;
      else ++s.misses;
      s.nextPeer += cfg.listenOnly ? half : period;
      if (cfg.listenOnly) s.listenSlot ^= 1;
    }
    return;
  }

  if (!hasPhase) {
    rx.histCount = 0;
    return;
  }
  rx.histIndex = (rx.histIndex + 1) & 31;
  rx.phaseHistory[rx.histIndex] = turn;
  rx.iHistory[rx.histIndex] = i;
  rx.qHistory[rx.histIndex] = q;
  rx.timeHistory[rx.histIndex] = timestamp;
  if (rx.histCount < 32) ++rx.histCount;
  uint32_t start;
  int16_t center;
  int8_t polarity;
  if (!marker(rx, start, center, polarity)) return;
  if (s.timeline && (cfg.master || s.locked) && magnitude(static_cast<int32_t>(start - s.nextPeer)) > 12800) return;
  const uint32_t dataStart = start + duration(markerSamples);
  const uint32_t first = dataStart - 360;
  if (!before(nowCycles() + 100, first)) return;
  ++s.markers;

  if (cfg.listenOnly) {
    if (!s.locked) {
      s.nextPeer = start;
      s.listenSlot = 0;
      s.timeline = s.locked = true;
      ++s.lockCount;
    } else {
      const int32_t correction = static_cast<int32_t>(start - s.nextPeer) / 8;
      s.nextPeer += correction;
    }
    if (s.listenSlot == 1) s.lastOtherMarker = timestamp;
  } else if (!cfg.master) {
    if (!s.locked) {
      s.nextTx = start + half;
      s.nextPeer = start;
      s.timeline = s.locked = true;
      ++s.lockCount;
    } else {
      const int32_t error = static_cast<int32_t>(start - s.nextPeer);
      const int32_t adjustment = error / 8;
      s.nextTx += adjustment;
      s.nextPeer += adjustment;
    }
  } else {
    s.nextPeer += static_cast<int32_t>(start - s.nextPeer) / 4;
  }
  s.lastLock = timestamp;
  s.peerSeen = true;
  if (rx.haveCenter && rx.polarity == polarity) {
    rx.center += static_cast<int16_t>(center - rx.center) / 4;
  } else {
    rx.center = center;
    rx.haveCenter = true;
  }
  rx.polarity = polarity;
  beginAudio(rx, cfg, start, true);
}

static bool IRAM_ATTR sendSlot(Stream& s, const Config& cfg, const Radio& radio, uint32_t base) {
  if (!prepareTx(radio)) return false;
  const uint32_t gate = s.nextTx;
  const uint32_t carrier = base | gateMask | cfg.ifK;
  const uint32_t up = base | gateMask | (cfg.ifK + 1);
  const uint32_t down = base | gateMask | ((cfg.ifK - 1u) & kMask);
  waitUntil(gate, s);
  if (!s.stop) reg(tone1, carrier);
  waitUntil(gate + duration(4), s);
  if (!s.stop) reg(tone1, up);
  waitUntil(gate + duration(8), s);
  if (!s.stop) reg(tone1, carrier);
  const uint32_t origin = gate + duration(markerSamples);
  int32_t residual = 0;
  for (uint16_t n = 0; n < cfg.data && !s.stop; ++n) {
    const uint8_t sample = txRing[s.txTail++ & (txCapacity - 1)];
    int32_t wanted = pulseWidths[sample] + residual;
    int32_t width = min(magnitude(wanted), 520);
    const uint32_t end = origin + duration(n + 1);
    if (width >= 80) {
      const uint32_t start = end - 667 - width / 2;
      waitUntil(start - 120, s);
      if (s.stop) break;
      spinUntil(start);
      width = min(width, static_cast<int32_t>(end - nowCycles()) - 200);
      if (width >= 80) {
        reg(tone1, wanted > 0 ? up : down);
        fence();
        const uint32_t started = nowCycles();
        spinUntil(started + width - 32);
        reg(tone1, carrier);
        fence();
        const int32_t elapsed = nowCycles() - started;
        wanted += wanted > 0 ? -elapsed : elapsed;
      }
    }
    residual = wanted > 700 ? 700 : wanted < -700 ? -700 : wanted;
    waitUntil(end, s);
  }
  reg(tone1, base);
  fence();
  ++s.slotsTx;
  return restoreRx(radio, false);
}

static void IRAM_ATTR stream(const Config& cfg, const Radio& radio, Stream& s) {
  const uint32_t half = duration(cfg.data + markerSamples + cfg.guard);
  const uint32_t period = 2 * half;
  const uint32_t active = duration(cfg.data + markerSamples);
  const uint32_t base = (radio.tone[0] & ~(kMask | scaleMask | gateMask)) |
                        (static_cast<uint32_t>(static_cast<uint8_t>(0u - cfg.ask)) << 10);
  const uint32_t uartMask = reg(uartIntEnable);
  const uint32_t ps = xt_rsil(15);
  reg(uartIntEnable, 0);
  reg(uartIntClear, 0x1ff);
  reg(macIntEnable, 0);
  reg(tone1, base);
  reg(tone2, radio.tone[1] & ~gateMask);
  reg(tone3, radio.tone[2] & ~gateMask);
  fence();
  if (!cfg.listenOnly) {
    // Only transmitting roles prime the warm TX path. A passive monitor never
    // enables the TX clock, including during its initialization.
    if (!prepareTx(radio)) s.fault = true;
    setScale(cfg.power);
    setTxClock(1);
    if (!restoreRx(radio, false)) s.fault = true;
  }
  const uint32_t estimator = (radio.iq & (iqMask & ~iqDone)) | (31u << 2) | 1u;
  Rx rx = {};
  rx.nextIq = nowCycles();
  s.nextUart = rx.nextIq;
  s.lastHostData = rx.nextIq;
  s.listener = cfg.listenOnly;
  rx.polarity = 1;
  s.nextTx = rx.nextIq + 160 * 30000u;
  s.nextPeer = s.nextTx + half;
  s.timeline = s.locked = cfg.master;
  uint32_t watchdog = rx.nextIq;
  while (!s.stop && !s.fault) {
    pump(s);
    const uint32_t now = nowCycles();
    // If the host closes without STOP, do not leave an autonomous TX session
    // stuck forever. Passive listeners intentionally do not require PCM data.
    if (!cfg.listenOnly && static_cast<uint32_t>(now - s.lastHostData) > cpuHz * 2u) {
      s.stop = true;
      break;
    }
    if (!before(now, watchdog + 160 * 1000u)) {
      ESP.wdtFeed();
      watchdog = now;
    }
    if (!cfg.master && s.locked && !before(now, s.lastLock + 160 * 600000u)) {
      s.locked = false;
      rx.haveCenter = false;
      rx.haveAudioCenter = false;
    }
    if (!cfg.listenOnly && s.timeline && !before(now, s.nextTx - 1440)) {
      if (s.locked && s.txHead - s.txTail >= cfg.data) {
        if (!sendSlot(s, cfg, radio, base)) s.fault = true;
        rx.nextIq = nowCycles() + 40;
        rx.iqRemainder = 0;
        rx.previous = false;
        rx.histCount = 0;
      } else if (!s.locked) {
        // Keep the logical serial clock moving through RF loss after B has
        // joined the timeline. No unsynchronized transmission takes place.
        s.txTail += min(s.txHead - s.txTail, static_cast<uint32_t>(cfg.data));
      }
      s.nextTx += period;
      continue;
    }
    if (s.timeline && !before(now, rx.audio ? rx.audioDeadline : s.nextPeer + active + 1800)) {
      skipPeer(s, rx, cfg, period);
      continue;
    }
    if (!cfg.listenOnly && s.timeline && s.locked && !rx.audio && !before(now, s.nextPeer + duration(markerSamples) - 620)) {
      // Timing continues across a damaged marker. Use the center of the
      // same scheduled header rather than slipping all following samples.
      if (!rx.haveCenter && rx.histCount >= 3) {
        const int16_t center = history(rx, 1);
        rx.center = center + (static_cast<int16_t>(history(rx, 0) - center) +
                              static_cast<int16_t>(history(rx, 2) - center)) / 3;
      }
      beginAudio(rx, cfg, s.nextPeer, false);
    }
    if (!before(now + 130, rx.nextIq)) {
      spinUntil(rx.nextIq);
      processIq(s, rx, cfg, half, period, estimator);
    }
  }
  reg(tone1, base);
  if (!restoreRx(radio, true)) s.fault = true;
  reg(tone1, radio.tone[0] & ~gateMask);
  reg(tone2, radio.tone[1] & ~gateMask);
  reg(tone3, radio.tone[2] & ~gateMask);
  reg(iqCtrl, radio.iq);
  reg(macIntEnable, radio.mac);
  const uint32_t deadline = nowCycles() + 160 * 25000u;
  while ((s.readFrame != s.writeFrame || ((reg(uartStatus) >> 16) & 0xffu)) &&
         before(nowCycles(), deadline)) pump(s);
  const uint32_t uartConfig = reg(uartConf0);
  reg(uartConf0, uartConfig | (1u << 17));
  reg(uartConf0, uartConfig & ~(1u << 17));
  reg(uartIntClear, 0x1ff);
  reg(uartIntEnable, uartMask);
  xt_wsr_ps(ps);
}

static bool tune(uint32_t khz) {
  if (khz == tuned) return true;
  const uint8_t channel = constrain((static_cast<int32_t>(khz) - 2407000 + 2500) / 5000, 1, 13);
  const int mhz = 2407 + channel * 5;
  const int32_t delta = static_cast<int32_t>(khz) - mhz * 1000;
  if (!wifi_set_channel(channel)) return false;
  delay(40);
  if (wifi_get_channel() != channel) return false;
  set_rf_freq_offset(chip6_phy_init_ctrl[1], mhz, (delta * 1024 + (delta < 0 ? -500 : 500)) / 1000);
  tuned = khz;
  return true;
}

static void setReceiveGain(uint8_t rf, uint8_t vga, uint8_t bbGain) {
  using Call = void(*)(void);
  if (rf == 7) {
    if (reg(pbusCmd) & 1u) reinterpret_cast<Call>(0x40007648u)();
    return;
  }
  // Manual RF/VGA controls from the AM receiver. RF 7 selects automatic gain.
  constexpr uint8_t rfTable[] = {0x00, 0x40, 0x60, 0x70, 0x78, 0x7c, 0x7f};
  using Force = void(*)(uint32_t, uint32_t, uint32_t);
  const auto force = reinterpret_cast<Force>(0x4000747cu);
  reinterpret_cast<Call>(0x4000737cu)();
  // Complete the 15-bit PHY gain word. Bits 10..14 live in selector 2,
  // which must be captured after forcing gain so restoreRx keeps the value.
  if (bbGain < 32) {
    const uint32_t gainBits = ((bbGain & 1u) << 1) | ((bbGain >> 1) << 3);
    force(2, 1, (readPbus(2, 1) & 0x185u) | gainBits);
  }
  force(3, 1, rfTable[rf]);
  force(3, 2, (readPbus(3, 2) & 0x1c7u) | (static_cast<uint32_t>(vga) << 3));
}

static void command(char* line) {
  if (strcmp(line, "STATUS") == 0) {
    Serial.printf("DUPLEX 1 %lu %u\n", static_cast<unsigned long>(baud), phyReady);
    return;
  }
  const char* verb = strtok(line, " ");
  const char* role = strtok(nullptr, " ");
  if (!verb || strcmp(verb, "START") || !role || role[1] || (role[0] != 'A' && role[0] != 'B' && role[0] != 'L')) {
    Serial.println("ERR START A|B|L [dataSamples [guardSamples [loKHz [apwr [rfGain [vga [ask [ifK [bbGain]]]]]]]]]");
    return;
  }
  unsigned long fields[] = {12, 4, 2442312, 63, 0, 0, 0, 0, 16};
  for (uint8_t n = 0;; ++n) {
    const char* token = strtok(nullptr, " ");
    if (!token) break;
    char* end;
    const auto value = strtoul(token, &end, 10);
    if (n >= 9 || *end) { Serial.println("ERR PARAM"); return; }
    fields[n] = value;
  }
  if (!phyReady || fields[0] < 12 || fields[0] > 480 || fields[1] < 4 || fields[1] > 240 ||
      fields[2] < 2400000 || fields[2] > 2483500 || fields[3] > 191 || fields[4] > 7 ||
      fields[5] > 7 || fields[6] > 127 || fields[7] > 1 || fields[8] > 32) {
    Serial.println("ERR RANGE_OR_PHY");
    return;
  }
  // Listen-only transport uses pairs of equally sized half-rate samples.
  if (role[0] == 'L' && (fields[0] & 1u)) { Serial.println("ERR LISTEN_EVEN_DATA"); return; }
  const Config cfg = {role[0] == 'A', role[0] == 'L', static_cast<uint16_t>(fields[0]), static_cast<uint16_t>(fields[1]),
                      static_cast<uint32_t>(fields[2]), static_cast<uint8_t>(fields[3]),
                      static_cast<uint8_t>(fields[4]), static_cast<uint8_t>(fields[5]),
                      static_cast<uint8_t>(fields[6]), static_cast<uint8_t>(fields[7]),
                      static_cast<uint8_t>(fields[8])};
  const uint32_t logical = (activeHz * cfg.data) / (2 * (cfg.data + markerSamples + cfg.guard));
  if (logical * packetBytes * uartPacedBits >= baud * payload) {
    Serial.println("ERR UART_CAPACITY");
    return;
  }
  // Place the IQ passband 78 kHz from the on-air carrier when using low IF.
  if (!tune(cfg.lo - 78u * cfg.ifK)) { Serial.println("ERR TUNE"); return; }
  if (cfg.rfGain != 7) setReceiveGain(cfg.rfGain, cfg.vga, cfg.bbGain);
  Radio radio = {{reg(tone1), reg(tone2), reg(tone3)}, reg(pbusCmd), reg(rxCtrl),
                 reg(iqCtrl) & ~iqDone, reg(macIntEnable), {}};
  for (uint8_t i = 0; i < 4; ++i) radio.pbus[i] = readPbus(pbusSelectors[i], 1) & 0x1ff;
  char ready[64];
  Stream state = {};
  state.ready = ready;
  state.readyLength = snprintf(ready, sizeof(ready), "READY %lu %u %u %u\n",
      static_cast<unsigned long>(logical), cfg.data, cfg.guard, markerSamples);
  Serial.flush();
  stream(cfg, radio, state);
  if (cfg.rfGain != 7) setReceiveGain(7, 0, 0);
  Serial.printf("\nEND %lu %lu %lu %lu %lu %lu %lu %lu %u %lu %lu %lu %lu %lu %u\n",
      static_cast<unsigned long>(state.txTail), static_cast<unsigned long>(state.slotsTx),
      static_cast<unsigned long>(state.slotsRx), static_cast<unsigned long>(state.misses),
      static_cast<unsigned long>(state.lockCount), static_cast<unsigned long>(state.uartErrors),
      static_cast<unsigned long>(state.rxDrops), static_cast<unsigned long>(state.txOverflow), state.fault,
      static_cast<unsigned long>(state.markers), static_cast<unsigned long>(state.iqTimeouts),
      static_cast<unsigned long>(state.iqLate), static_cast<unsigned long>(state.iqLateMax),
      static_cast<unsigned long>(state.iqRails), radio.pbus[0]);
}
}

void setup() {
  Serial.begin(baud, SERIAL_8N2);
  Serial.setDebugOutput(false);
  prepareTables();
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  phyReady = ESP.getCpuFreqMHz() == 160 && WiFi.mode(WIFI_STA);
  if (phyReady) {
    wifi_station_set_auto_connect(0);
    wifi_station_set_reconnect_policy(false);
    wifi_station_disconnect();
    phyReady = wifi_set_sleep_type(NONE_SLEEP_T);
    delay(100);
  }
  Serial.printf("DUPLEX 1 %lu %u\n", static_cast<unsigned long>(baud), phyReady);
}

void loop() {
  static char line[100];
  static uint8_t used = 0;
  static bool overflow = false;
  while (Serial.available()) {
    const char c = Serial.read();
    if (static_cast<uint8_t>(c) == 0x80) {
      // An idle STOP also clears stale input from an interrupted session.
      used = 0;
      overflow = false;
    } else if (c == '\n') {
      line[used] = 0;
      if (!overflow && used) command(line);
      used = 0;
      overflow = false;
    } else if (c != '\r') {
      if (used + 1 < sizeof(line)) line[used++] = c;
      else overflow = true;
    }
  }
}

