/*
 * CB_V6 TDD/protocol/DSP port to ESP32-C6 160 MHz, Arduino-ESP32 3.3.12.
 * Upstream: kaboom748/ESP-AnalogRadio, commit fa5c43cf1013dcaeb5a16ba9eb6ba409684e48f0.
 * Both boards run this sketch. Host supplies signed 8-bit PCM over full-duplex
 * UART. START A is the slot clock source; START B follows markers heard over RF.
 * START L is passive receive-only monitoring: forward every RF audio sample
 * from both slots over the 921600-baud UART (full rate per direction).
 * V2: incoming PCM is gated by genuine RF sync; predicted missing slots
 * remain silent, rather than demodulating the sender's own RF after TX.
 * The C6 backend provides native buffered IQ and warm timed RF switching.
 */
#include <Arduino.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "driver/uart.h"
#include "hal/uart_ll.h"
#include "soc/uart_struct.h"
#include "soc/uart_reg.h"
#include "riscv/rv_utils.h"
#include "esp_cpu.h"
#include "esp_rom_sys.h"
#include "c6_cb_radio.h"
#include "esp_system.h"

#ifndef CB_C6_TRACE
#define CB_C6_TRACE 0
#endif

// Own UART0 directly during both command and real-time operation. No USB CDC
// or interrupt-driven serial buffer can steal PCM/STOP bytes from the stream.
class CbUart {
public:
  bool begin() {
    uart_config_t cfg = {};
    cfg.baud_rate=921600; cfg.data_bits=UART_DATA_8_BITS;
    cfg.parity=UART_PARITY_DISABLE; cfg.stop_bits=UART_STOP_BITS_2;
    cfg.flow_ctrl=UART_HW_FLOWCTRL_DISABLE; cfg.source_clk=UART_SCLK_XTAL;
    if (uart_param_config(UART_NUM_0,&cfg)!=ESP_OK ||
        uart_set_pin(UART_NUM_0,16,17,UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE)!=ESP_OK) return false;
    uart_ll_disable_intr_mask(&UART0,UINT32_MAX);
    uart_ll_clr_intsts_mask(&UART0,UINT32_MAX);
    uart_ll_rxfifo_rst(&UART0);
    return true;
  }
  int available() { return uart_ll_get_rxfifo_len(&UART0); }
  int read() { uint8_t c=0; uart_ll_read_rxfifo(&UART0,&c,1);return c; }
  void flush() { while(!uart_ll_is_tx_idle(&UART0)) {} }
  void printf(const char *format,...) {
    char buf[512];va_list ap;va_start(ap,format);
    int n=vsnprintf(buf,sizeof(buf),format,ap);va_end(ap);
    if(n<0)return;if(n>=int(sizeof(buf)))n=sizeof(buf)-1;
    for(int i=0;i<n;++i) {
      while(!uart_ll_get_txfifo_len(&UART0)) {}
      const uint8_t c=buf[i];uart_ll_write_txfifo(&UART0,&c,1);
    }
  }
  void println(const char *line) { printf("%s\r\n",line); }
  void write(const void *data,size_t size) {
    const uint8_t *p=static_cast<const uint8_t*>(data);
    for(size_t n=0;n<size;++n) {
      while(!uart_ll_get_txfifo_len(&UART0)) {}
      uart_ll_write_txfifo(&UART0,p+n,1);
    }
  }
};
static CbUart uartLink;
namespace {
constexpr uint32_t cpuHz = 160000000, baud = 921600, activeHz = 120000;
constexpr uint16_t markerSamples = 12, txCapacity = 8192, payload = 128;
constexpr uint16_t packetBytes = 16 + payload + 2;
constexpr uint8_t telemetryPayload = 52, telemetryBytes = telemetryPayload + 2;
constexpr uint32_t telemetryInterval = cpuHz / 4;
constexpr int32_t iqFullScale = 8192, iqNearRail = 8176;
// Two complete passive RX channels double UART traffic. Retain approximately
// the former time-span of queued frames by doubling the packet ring to 16.
constexpr uint8_t rxFrames = 16;
// 8N2 uses eleven bits per octet. Keep one additional bit of idle time.
constexpr uint32_t uartPacedBits = 12;
constexpr uint32_t uartByteCycles = (uartPacedBits * cpuHz + baud - 1u) / baud;
constexpr uint32_t iqPeriod = cpuHz / (2 * activeHz);
constexpr int32_t pcmPeriod = cpuHz / activeHz;
constexpr uint32_t iqCtrl=CbIq::control1, iqSumI=CbIq::sumI, iqSumQ=CbIq::sumQ;
constexpr uint32_t iqCtl0=CbIq::control0, iqStatus=CbIq::status, iqDone=CbIq::done;
constexpr uint32_t uartErrorMask=UART_RXFIFO_OVF_INT_RAW_M|UART_FRM_ERR_INT_RAW_M|UART_PARITY_ERR_INT_RAW_M;
constexpr uint32_t cyclesPerMicrosecond = cpuHz / 1000000u;
// IDF's C6 transmitter wake performs two 20 us PA delays. The maximum
// observed complete switch was 56.65 us; the default eight-sample guard
// is 66.67 us. Start warming within that guard before the marker begins.
constexpr uint32_t txLead = 63u * cyclesPerMicrosecond;
constexpr uint16_t minimumTxGuard = 8;
struct Config {
  bool master, listenOnly;
  uint16_t data, guard;
  uint32_t lo;
  uint8_t power, rfGain, vga, ask, ifK, bbGain;
};
struct Radio { uint32_t iq, ctl0; uint16_t pbus[4]; };
struct Rx {
  uint32_t nextIq, iqRemainder, audioDeadline;
  int16_t phaseHistory[32], iHistory[32], qHistory[32];
  uint32_t timeHistory[32];
  uint8_t histIndex, histCount;
  int32_t oldI, oldQ, dcI, dcQ;
  int16_t audioCenter, calI, calQ;
  int16_t slotCenter, peerAudioCenter[2];
  bool peerHaveAudioCenter[2];
  uint32_t calTime;
  uint16_t samples, validSamples;
  bool previous, audio, haveAudioCenter, calibrate;
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
  uint32_t confirmedIqRails, confirmedIqValid, scanIqRails, scanIqValid;
  uint32_t nextTelemetry;
  uint32_t telemetry[telemetryPayload / sizeof(uint32_t)];
  uint16_t confirmedIqPeak, telemetryCrc;
  uint8_t telemetryOffset;
  uint32_t nextTx, nextPeer, lastLock, nextUart, lastHostData;
  uint16_t partial, packetOffset, crc;
  uint8_t readFrame, writeFrame;
  uint16_t readyLength, readySent;
  const char* ready;
};
bool phyReady = false;
uint32_t tuned = 0;
DRAM_ATTR uint8_t txRing[txCapacity];
DRAM_ATTR uint8_t rxPackets[rxFrames][packetBytes];
uint16_t angles[257], crcTable[256];
uint32_t reciprocals[256];
int16_t pulseWidths[256];
// Optional bench capture; production reserves no trace buffer or UART command.
#if CB_C6_TRACE
struct TraceIq {uint32_t timestamp;int16_t i,q,turn;uint8_t flags,history;};
static_assert(sizeof(TraceIq)==12,"trace layout");
DRAM_ATTR TraceIq traceIq[8192];
uint32_t traceCount=0;
bool traceArmed=false;
#endif

static inline __attribute__((always_inline)) uint32_t nowCycles() {
  return esp_cpu_get_cycle_count();
}
static inline __attribute__((always_inline)) uint32_t reg(uint32_t address) {
  return *reinterpret_cast<volatile uint32_t*>(address);
}
static inline __attribute__((always_inline)) void reg(uint32_t address, uint32_t value) {
  *reinterpret_cast<volatile uint32_t*>(address) = value;
}
static inline __attribute__((always_inline)) void fence() {
  asm volatile("fence iorw,iorw" ::: "memory");
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

static void IRAM_ATTR snapshotTelemetry(Stream& s, uint32_t now) {
  // The ESP is little-endian. Store an aligned snapshot and calculate the
  // checksum one byte at a time as spare UART capacity sends the frame.
  s.telemetry[0] = 0x31545044u;  // DPT1
  s.telemetry[1] = s.rxIndex;
  s.telemetry[2] = s.slotsTx;
  s.telemetry[3] = s.slotsRx;
  s.telemetry[4] = s.misses;
  s.telemetry[5] = s.uartErrors;
  s.telemetry[6] = s.rxDrops;
  s.telemetry[7] = s.txOverflow;
  s.telemetry[8] = s.confirmedIqRails;
  s.telemetry[9] = s.confirmedIqValid;
  s.telemetry[10] = s.scanIqRails;
  s.telemetry[11] = s.scanIqValid;
  const bool otherActive = s.listener && s.lastOtherMarker &&
      static_cast<uint32_t>(now - s.lastOtherMarker) < cpuHz / 2;
  const uint32_t flags = (s.locked ? 1u : 0u) | (s.peerSeen ? 2u : 0u) |
      (otherActive ? 4u : 0u) | (s.listener ? 8u : 0u);
  const uint32_t peak = min(static_cast<int32_t>(s.confirmedIqPeak), iqFullScale);
  s.telemetry[12] = (flags << 16) | (peak * 1000u / iqFullScale);
  s.confirmedIqPeak = 0;
  s.telemetryCrc = 0xffff;
  s.telemetryOffset = 0;
  s.nextTelemetry = now + telemetryInterval;
}

static void IRAM_ATTR pump(Stream& s) {
  if (uart_ll_get_rxfifo_len(&UART0)) {
    uint8_t byte;uart_ll_read_rxfifo(&UART0,&byte,1);
    if (byte==0x80) s.stop=true;
    else if(s.txHead-s.txTail<txCapacity) {
      txRing[s.txHead++&(txCapacity-1)]=byte;++s.received;s.lastHostData=nowCycles();
    } else ++s.txOverflow;
  }
  const uint32_t errors=uart_ll_get_intraw_mask(&UART0)&uartErrorMask;
  if(errors) {++s.uartErrors;uart_ll_clr_intsts_mask(&UART0,errors);}
  if(uart_ll_get_txfifo_len(&UART0)<=8 || before(nowCycles(),s.nextUart))return;
  if (s.readySent != s.readyLength) {
    const uint8_t byte=s.ready[s.readySent++];uart_ll_write_txfifo(&UART0,&byte,1);
  } else if (!s.packetOffset && s.telemetryOffset < telemetryBytes) {
    const uint8_t offset = s.telemetryOffset++;
    uint8_t byte;
    if (offset < telemetryPayload) {
      byte = reinterpret_cast<const uint8_t*>(s.telemetry)[offset];
      if (offset >= 4) s.telemetryCrc = (s.telemetryCrc << 8) ^ crcTable[(s.telemetryCrc >> 8) ^ byte];
    } else {
      byte = s.telemetryCrc >> (8u * (offset - telemetryPayload));
    }
    uart_ll_write_txfifo(&UART0, &byte, 1);
  } else if (s.readFrame != s.writeFrame) {
    const uint8_t byte=rxPackets[s.readFrame][s.packetOffset++];uart_ll_write_txfifo(&UART0,&byte,1);
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
  const int32_t raw = static_cast<int32_t>(reg(address)) >> CbIq::sumShift;
  return raw < -15000 ? -15000 : raw > 15000 ? 15000 : raw;
}

static inline __attribute__((always_inline)) void stepIq(Rx& rx) {
  // At 160 MHz the IQ scan and audio endpoints both use fractional periods.
  rx.nextIq += rx.audio ? 1333u : 666u;
  rx.iqRemainder += rx.audio ? 1u : 2u;
  if (rx.iqRemainder >= 3u) {
    rx.iqRemainder -= 3u;
    ++rx.nextIq;
  }
}

static void IRAM_ATTR skipPeer(Stream& s, Rx& rx, const Config& cfg, uint32_t period) {
  s.peerSeen = false;
  const bool partial = rx.audio;
  // Keep the full samples-per-slot layout even when a marker or slot is lost.
  // The browser uses cfg.data consecutive samples to identify each channel.
  const uint16_t emitted = partial && rx.samples != UINT16_MAX ? rx.samples : 0;
  const uint16_t target = cfg.data;
  for (uint16_t i = emitted; i < target; ++i) {
    // Keep the exact clock position and distinguish a missing sample from silence.
    outputSample(s, 0x80);
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
  for (uint8_t offset = 0; markerConfirmed && offset < min(rx.histCount, static_cast<uint8_t>(6)); ++offset) {
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
      magnitude(static_cast<int16_t>(history(rx, 14) - low)) > 6800 ||
      magnitude(static_cast<int16_t>(history(rx, 15) - low)) > 6800 ||
      magnitude(static_cast<int16_t>(history(rx, 16) - low)) > 6800) return false;
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
  while (!(reg(iqStatus) & iqDone) && before(nowCycles(), iqTimeout)) pump(s);
  const bool done = reg(iqStatus) & iqDone;
  if (!done) ++s.iqTimeouts;
  int32_t i = 0, q = 0, rawPeak = 0;
  if (done) {
    i = iqValue(iqSumI);
    q = iqValue(iqSumQ);
    // Native ADC10 full scale, expressed with four fractional mean bits.
    rawPeak = max(magnitude(i), magnitude(q));
    if (rx.audio && rawPeak >= iqNearRail) ++s.iqRails;
  }
  reg(iqCtrl, estimator);
  stepIq(rx);
  if (!rx.audio && before(rx.nextIq, nowCycles())) {
    rx.nextIq = nowCycles() + 40;
    rx.iqRemainder = 0;
  }
  // DC tracks noise between RF slots and is frozen during a valid waveform.
  // Native C6 DC offsets can exceed the ESP8266's entire 170-code gate.
  // Acquire that offset while scanning, then freeze it during actual audio.
  if (done && !rx.audio) {
    rx.dcI += (i * 256 - rx.dcI) >> 7;
    rx.dcQ += (q * 256 - rx.dcQ) >> 7;
  }
  i -= rx.dcI >> 8;
  q -= rx.dcQ >> 8;
  const int32_t absI = magnitude(i), absQ = magnitude(q);
  const bool valid = done && (!rx.audio || lateness <= 360) && absI + absQ >= 36;
  if (valid) {
    if (!rx.audio) {
      ++s.scanIqValid;
      s.scanIqRails += rawPeak >= iqNearRail;
    } else if (rx.markerConfirmed) {
      ++s.confirmedIqValid;
      s.confirmedIqRails += rawPeak >= iqNearRail;
      if (rawPeak > s.confirmedIqPeak) s.confirmedIqPeak = rawPeak;
    }
  }
  const bool hasPhase = rx.previous && valid;
  const int16_t turn = hasPhase ? phase(i, q, rx.oldI, rx.oldQ) : 0;
#if CB_C6_TRACE
  if(traceArmed && traceCount<8192) {
    traceIq[traceCount++]={timestamp,static_cast<int16_t>(i),static_cast<int16_t>(q),turn,
      static_cast<uint8_t>((valid?1:0)|(hasPhase?2:0)|(rx.audio?4:0)|(rx.markerConfirmed?8:0)),rx.histCount};
  }
#endif
  rx.previous = valid;
  rx.oldI = i;
  rx.oldQ = q;
  if (rx.audio) {
    if (rx.samples == UINT16_MAX) {
      constexpr uint32_t centerTolerance = 4096;
      const int32_t hint = 2 * int32_t(rx.slotCenter);
      rx.haveAudioCenter = rx.haveAudioCenter &&
          magnitude(static_cast<int16_t>(rx.audioCenter - hint)) <= centerTolerance;
      if (valid && rx.calibrate) {
        const int32_t elapsed = timestamp - rx.calTime;
        // Use the scan's rough estimate to unwrap the variable-length
        // calibration interval. Keep the calibrated 120 kHz result modulo 2π.
        // Anchor phase unwrapping to this actual RF header. A previous
        // audio estimate can select the wrong turn after a gain/LO change.
        const int32_t expected = (hint * elapsed) / pcmPeriod;
        const int16_t observed = phase(i, q, rx.calI, rx.calQ);
        const int32_t unwrapped = expected + static_cast<int16_t>(observed - expected);
        const int16_t baseline = (unwrapped * pcmPeriod) / elapsed;
        // The two-point estimate can unwrap to a wrong turn. Only retain a
        // center that agrees with this slot's measured marker, modulo a turn.
        if (magnitude(static_cast<int16_t>(baseline - hint)) <= centerTolerance) {
          if (rx.haveAudioCenter)
            rx.audioCenter += static_cast<int16_t>(baseline - rx.audioCenter) / 8;
          else rx.audioCenter = baseline;
          rx.haveAudioCenter = true;
          const unsigned peer = cfg.listenOnly ? s.listenSlot : 0;
          rx.peerAudioCenter[peer] = rx.audioCenter;
          rx.peerHaveAudioCenter[peer] = true;
        }
      }
      rx.samples = 0;
      return;
    }
    // All roles, including passive listening, forward every received sample.
    // Consecutive cfg.data samples correspond to alternating Master/Slave slots.
    if (!rx.markerConfirmed) {
      // An unconfirmed slot preserves time without supplying a PCM value.
      outputSample(s, 0x80);
    } else if (!hasPhase) {
      outputSample(s, 0x80);
    } else {
      const int16_t swing = static_cast<int16_t>(turn - (rx.haveAudioCenter ? rx.audioCenter : 2 * rx.slotCenter));
      const int32_t pcm = (rx.polarity * swing * 38) >> 12;
      outputSample(s, static_cast<uint8_t>(pcm > 127 ? 127 : pcm < -127 ? -127 : pcm));
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
  rx.polarity = polarity;
  rx.slotCenter=center;
  const unsigned peer=cfg.listenOnly?s.listenSlot:0;
  rx.audioCenter=rx.peerAudioCenter[peer];
  rx.haveAudioCenter=rx.peerHaveAudioCenter[peer];
  beginAudio(rx, cfg, start, true);
}

static bool IRAM_ATTR sendSlot(Stream& s, const Config& cfg, const Radio&, uint32_t) {
  if(!cbRadioTxBegin())return false;
  const uint32_t gate=s.nextTx;
  waitUntil(gate,s);if(!s.stop)cbRadioTone(0);
  waitUntil(gate+duration(4),s);if(!s.stop)cbRadioTone(1);
  waitUntil(gate+duration(8),s);if(!s.stop)cbRadioTone(0);
  const uint32_t origin=gate+duration(markerSamples);
  int32_t residual=0;
  for(uint16_t n=0;n<cfg.data && !s.stop;++n) {
    const uint8_t sample=txRing[s.txTail++&(txCapacity-1)];
    int32_t wanted=pulseWidths[sample]+residual;
    int32_t width=min(magnitude(wanted),int32_t(520));
    const uint32_t end=origin+duration(n+1);
    if(width>=int32_t(80)) {
      const uint32_t start=end-667-width/2;
      waitUntil(start-120,s);if(s.stop)break;
      spinUntil(start);
      width=min(width,static_cast<int32_t>(end-nowCycles())-int32_t(200));
      if(width>=int32_t(80)) {
        cbRadioTone(wanted>0?1:-1);fence();
        const uint32_t started=nowCycles();
        spinUntil(started+width-32);
        cbRadioTone(0);fence();
        const int32_t elapsed=nowCycles()-started;
        wanted+=wanted>0?-elapsed:elapsed;
      }
    }
    residual=wanted>int32_t(700)?700:wanted<-int32_t(700)?-int32_t(700):wanted;
    waitUntil(end,s);
  }
  cbRadioTxEnd();++s.slotsTx;return cbRadioHealthy();
}

static void IRAM_ATTR stream(const Config& cfg, const Radio& radio, Stream& s) {
  const uint32_t half = duration(cfg.data + markerSamples + cfg.guard);
  const uint32_t period = 2 * half;
  const uint32_t active = duration(cfg.data + markerSamples);
  const uint32_t base=0;
  const uint32_t ps=RV_CLEAR_CSR(mstatus,MSTATUS_MIE);
  uart_ll_clr_intsts_mask(&UART0,UINT32_MAX);
  reg(iqCtrl,radio.iq&~2u);fence();
  reg(iqCtrl,radio.iq&~3u);fence();
  reg(iqCtl0,(radio.ctl0&0xf3ffffffu)|0x04000000u);
  const uint32_t estimator=(radio.iq&~(0x00180000u|0x0001fffcu|3u))|0x00100000u|((CbIq::windowSamples-1u)<<2)|1u;
  reg(iqCtrl,estimator);fence();
  Rx rx = {};
  rx.nextIq = nowCycles();
  s.nextUart = rx.nextIq;
  s.nextTelemetry = rx.nextIq + telemetryInterval;
  s.telemetryOffset = telemetryBytes;
  s.lastHostData = rx.nextIq;
  s.listener = cfg.listenOnly;
  rx.polarity = 1;
  s.nextTx = rx.nextIq + cyclesPerMicrosecond * 30000u;
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
    if (!before(now, watchdog + cyclesPerMicrosecond * 1000u)) {
      // Watchdogs are disabled once by the Arduino radio runtime.
      watchdog = now;
    }
    if (!cfg.master && s.locked && !before(now, s.lastLock + cyclesPerMicrosecond * 600000u)) {
      s.locked = false;
      rx.haveAudioCenter = false;
      rx.peerHaveAudioCenter[0]=rx.peerHaveAudioCenter[1]=false;
    }
    if (!cfg.listenOnly && s.timeline && !rx.audio && !before(now, s.nextTx - txLead)) {
      if (s.locked && s.txHead - s.txTail >= cfg.data) {
        if (!sendSlot(s, cfg, radio, base)) s.fault = true;
        reg(iqCtrl,estimator&~3u);fence();
        reg(iqCtl0,(radio.ctl0&0xf3ffffffu)|0x04000000u);
        reg(iqCtrl,estimator);fence();
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
    // Take snapshots outside confirmed audio and away from the TX prekey.
    // Once queued, a snapshot can be sent between any two whole DPX1 frames.
    if (!rx.audio && s.telemetryOffset == telemetryBytes && !before(now, s.nextTelemetry) &&
        (cfg.listenOnly || !s.timeline || before(now + 250, s.nextTx - txLead))) {
      snapshotTelemetry(s, now);
      continue;
    }
    // Scan for genuine RF until the slot deadline. skipPeer() produces
    // the same indexed silence when a predicted slot has no valid marker.
    if (!before(now + 130, rx.nextIq)) {
      spinUntil(rx.nextIq);
      processIq(s, rx, cfg, half, period, estimator);
    }
  }
  cbRadioTxEnd();
  reg(iqCtrl,estimator&~3u);fence();
  reg(iqCtl0,radio.ctl0);reg(iqCtrl,radio.iq);fence();
  const uint32_t deadline=nowCycles()+cyclesPerMicrosecond * 25000u;
  while((s.readFrame!=s.writeFrame || s.telemetryOffset<telemetryBytes ||
         !uart_ll_is_tx_idle(&UART0)) && before(nowCycles(),deadline))pump(s);
  uart_ll_rxfifo_rst(&UART0);uart_ll_clr_intsts_mask(&UART0,UINT32_MAX);
  RV_SET_CSR(mstatus,ps&MSTATUS_MIE);
}

static void command(char* line) {
  if(strcmp(line,"HWINFO")==0) {
    uartLink.println("OK HWINFO CB_V6 ESP32-C6");return;
  }
  if(strcmp(line,"RESET?")==0) {
    uartLink.printf("OK RESET %u\n",unsigned(esp_reset_reason()));return;
  }
  if(strcmp(line,"GAINSTATE")==0) {
    uartLink.printf("GAINSTATE %u %u %u\n",cbRadioCurrentGain(),cbRadioMaxGain(),cbRadioAutoGain());return;
  }
  if(strcmp(line,"POWER")==0) {
    CbRadioTxPower hw={};cbRadioGetTxPower(hw);
    uartLink.printf("POWER %u %u %u %u %u %d %u %u\n",
      hw.requestedQuarterDb,hw.baselineQuarterDb,hw.ncoAttenuation,hw.row,
      hw.stage,int(hw.gain.digital),hw.gain.rf,hw.gain.bb);return;
  }
  if(strcmp(line,"TXTABLE")==0) {
    CbRadioTxGainRow hw={};
    for(unsigned n=0;cbRadioGetTxGainRow(n,hw);++n)
      uartLink.printf("TXTABLE %u %d %u %u\n",n,int(hw.digital),hw.rf,hw.bb);
    uartLink.println("TXTABLE END");return;
  }
  if(strcmp(line,"BUS")==0) {
    CbRadioBusWord hw={};
    for(unsigned n=0;cbRadioGetBusWord(n,hw);++n)
      uartLink.printf("BUS %u %u %u %u\n",hw.selector,hw.bank,hw.rx,hw.tx);
    uartLink.println("BUS END");return;
  }
#if CB_C6_TRACE
  if(strncmp(line,"GAIN ",5)==0) {
    const int gain=atoi(line+5);cbRadioSetGainOverride(gain);
    uartLink.printf("GAIN %d\n",gain);return;
  }
  if(strcmp(line,"TRACE")==0) {traceCount=0;traceArmed=true;uartLink.println("OK TRACE");return;}
  if(strcmp(line,"DUMP")==0) {
    traceArmed=false;uartLink.printf("TRACE %lu 12\n",(unsigned long)traceCount);
    uartLink.write(traceIq,traceCount*sizeof(TraceIq));uartLink.println("\nTRACE_END");return;
  }
#endif
  if(strcmp(line,"PHY")==0) {
    CbRadioStats hw={};cbRadioGetStats(hw);
    uartLink.printf("PHY %lu %lu %lu %lu %lu %lu %lu\n",
      (unsigned long)hw.maxTxBeginCycles,(unsigned long)hw.maxTxEndCycles,
      (unsigned long)hw.busTimeouts,(unsigned long)hw.ncoMode,
      (unsigned long)hw.ncoStep,(unsigned long)hw.switchWords,
      (unsigned long)hw.attenuationClipped);return;
  }
  if (strcmp(line, "STATUS") == 0) {
    uartLink.printf("DUPLEX 1 %lu %u\n", static_cast<unsigned long>(baud), phyReady);
    return;
  }
  const char* verb = strtok(line, " ");
  const char* role = strtok(nullptr, " ");
  if (!verb || strcmp(verb, "START") || !role || role[1] || (role[0] != 'A' && role[0] != 'B' && role[0] != 'L')) {
    uartLink.println("ERR START A|B|L [dataSamples [guardSamples [loKHz [apwr [rfGain [vga [ask [ifK [bbGain]]]]]]]]]");
    return;
  }
  unsigned long fields[] = {12, 4, 2442312, 63, 7, 0, 0, 0, 16};
  for (uint8_t n = 0;; ++n) {
    const char* token = strtok(nullptr, " ");
    if (!token) break;
    char* end;
    const auto value = strtoul(token, &end, 10);
    if (n >= 9 || *end) { uartLink.println("ERR PARAM"); return; }
    fields[n] = value;
  }
  if (!phyReady || fields[0] < 12 || fields[0] > 480 || fields[1] < 4 || fields[1] > 240 ||
      fields[2] < 2400000 || fields[2] > 2483500 || fields[3] > 191 || fields[4] > 7 ||
      fields[5] > 7 || fields[6] > 127 || fields[7] > 1 || fields[8] > 32) {
    uartLink.println("ERR RANGE_OR_PHY");
    return;
  }
  // Keep an even slot length for the full-rate passive two-channel mixer.
  if (role[0] == 'L' && (fields[0] & 1u)) { uartLink.println("ERR LISTEN_EVEN_DATA"); return; }
  if (role[0] != 'L' && fields[1] < minimumTxGuard) {
    uartLink.println("ERR TDD_GUARD_ESP32_C6_MIN_8");
    return;
  }
  const Config cfg = {role[0] == 'A', role[0] == 'L', static_cast<uint16_t>(fields[0]), static_cast<uint16_t>(fields[1]),
                      static_cast<uint32_t>(fields[2]), static_cast<uint8_t>(fields[3]),
                      static_cast<uint8_t>(fields[4]), static_cast<uint8_t>(fields[5]),
                      static_cast<uint8_t>(fields[6]), static_cast<uint8_t>(fields[7]),
                      static_cast<uint8_t>(fields[8])};
  const uint32_t logical = (activeHz * cfg.data) / (2 * (cfg.data + markerSamples + cfg.guard));
  // START L now forwards TWO full-rate channels, not two half-rate channels.
  // Account for DPX1 overhead and 12 paced UART bits/byte; require >=4%
  // headroom for both active and passive roles. Avoid integer overflow.
  const uint32_t streams = cfg.listenOnly ? 2u : 1u;
  if (static_cast<uint64_t>(logical) * streams * packetBytes * uartPacedBits * 100u >=
      static_cast<uint64_t>(baud) * payload * 96u) {
    uartLink.println("ERR UART_CAPACITY");
    return;
  }
  const CbRadioConfig hw={cfg.lo,cfg.power,cfg.rfGain,cfg.vga,cfg.ask,cfg.ifK,cfg.bbGain,cfg.listenOnly};
  if(!cbRadioConfigure(hw)) {uartLink.println("ERR TUNE");return;}
  Radio radio={reg(iqCtrl),reg(iqCtl0),{}};
  char ready[64];
  Stream state = {};
  state.ready = ready;
  state.readyLength = snprintf(ready, sizeof(ready), "READY %lu %u %u %u\n",
      static_cast<unsigned long>(logical), cfg.data, cfg.guard, markerSamples);
  uartLink.flush();
  stream(cfg, radio, state);
  cbRadioRelease();
  uartLink.printf("\nEND %lu %lu %lu %lu %lu %lu %lu %lu %u %lu %lu %lu %lu %lu %u\n",
      static_cast<unsigned long>(state.txTail), static_cast<unsigned long>(state.slotsTx),
      static_cast<unsigned long>(state.slotsRx), static_cast<unsigned long>(state.misses),
      static_cast<unsigned long>(state.lockCount), static_cast<unsigned long>(state.uartErrors),
      static_cast<unsigned long>(state.rxDrops), static_cast<unsigned long>(state.txOverflow), state.fault,
      static_cast<unsigned long>(state.markers), static_cast<unsigned long>(state.iqTimeouts),
      static_cast<unsigned long>(state.iqLate), static_cast<unsigned long>(state.iqLateMax),
      static_cast<unsigned long>(state.iqRails), radio.pbus[0]);
}
}

static void beginFirmware() {
  if(!uartLink.begin())for(;;)delay(1000);
  prepareTables();phyReady=cbRadioInit();
  uartLink.printf("DUPLEX 1 %lu %u\n",static_cast<unsigned long>(baud),phyReady);
}

static void commandLoop() {
  static char line[100];
  static uint8_t used = 0;
  static bool overflow = false;
  while (uartLink.available()) {
    const char c = uartLink.read();
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


extern "C" void cb_duplex_run() {
  beginFirmware();for(;;) {commandLoop();delay(1);}
}
