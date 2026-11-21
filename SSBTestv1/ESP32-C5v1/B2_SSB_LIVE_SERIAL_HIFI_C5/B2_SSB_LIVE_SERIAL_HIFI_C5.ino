// SPDX-License-Identifier: GPL-3.0-only
// ESP32-C5 port of kaboom748/ESP-AnalogRadio SSBTestv1.
// A8 keeps the upstream four-byte polar representation for each audio sample.
#include <Arduino.h>
#include <math.h>
#include <driver/gptimer.h>
#include <hal/uart_ll.h>
#include "radio.h"

#if ARDUINO_USB_CDC_ON_BOOT
#error "Select USB CDC On Boot: Disabled to use UART0 with the USB-UART bridge."
#endif

namespace {
constexpr uint32_t baud = 2000000;
constexpr uint32_t rates[] = {24000, 32000};
constexpr uint32_t oversampling = 3;
constexpr uint32_t originalStepHz = 78125;
constexpr uint32_t sineSize = 1024;
constexpr uint32_t timerHz = 40000000;
constexpr uint32_t ringSamples = 8192;
constexpr uint32_t ringMask = ringSamples - 1;
constexpr uint32_t prefill = 4096;
constexpr uint32_t highWater = 6656;
constexpr uint32_t lowWater = 4608;
constexpr uint32_t timeoutMs = 1000;
constexpr uint32_t resumeRepeatMs = 250;
constexpr uint32_t flowRepeatMs = 12;
constexpr uint32_t recoveryQuietMs = 25;
constexpr uint32_t playbackStallMs = 50;
constexpr uint32_t maxSamples = 60000000;
constexpr uint16_t maxAsk = 127u * 256u;
constexpr uint16_t gateOff = 0x8000u;

enum Command : uint8_t {
  Ping = 0xA0, Off = 0xA2, Cw = 0xA3, Diagnostics = 0xA6, Start = 0xA8,
  Identity = 0xA9, CancelPreheat = 0xAA, SelectBand = 0xAB,
};
enum Status : uint8_t {
  Pause = 235, Resume = 236, Preheat = 237, BandReply = 238, StreamReady = 239,
  Ready = 240, Play = 241, Finish = 242, Timeout = 243, Underrun = 244,
  Overflow = 245, RfFault = 246, BadCommand = 247, RfOff = 248,
  DiagnosticReply = 249, IdentityReply = 250,
};

enum class State { Idle, Header, Band, Warmup, Stream, Recover };

struct Counters {
  uint32_t played, lateTicks, maxLateCycles, skipped;
  uint32_t overruns, underruns, rxPeak, lastError;
};

State state = State::Idle;
gptimer_handle_t timer;
bool timerRunning;
bool cwRequested;
bool waitingCw;
bool paused;
bool lsb;
uint32_t warmupMs;
uint8_t apwr = 63;
uint8_t pendingStatus;
uint8_t header[9];
size_t headerUsed;
uint32_t stateSince, lastSerialByte, lastFlow, repeatResumeUntil, lastTelemetry, lastFifo;
uint32_t wanted, received;
uint32_t lastPlayed, lastPlaybackProgress;
struct PolarSample { uint16_t ask; int16_t delta; };
static_assert(sizeof(PolarSample) == 4);
constexpr size_t sampleBytes = sizeof(PolarSample);
uint32_t wireSample;
PolarSample ring[ringSamples];
uint32_t head, tail;
uint32_t uartError;
Counters stats{};

uint32_t renderRate, wholeTicks, fractionalTicks, remainder, emittedTicks;
uint32_t phase, phasePerDelta, ifPhaseStep, phaseStep;
unsigned level, nextLevel, peakLevel;
int16_t sine[sineSize];
uint16_t amplitude[128];
uint32_t playbackResult;

// One producer and one consumer use monotonically increasing sample indices.
inline __attribute__((always_inline)) uint32_t load(const uint32_t &word) {
  return __atomic_load_n(&word, __ATOMIC_ACQUIRE);
}
inline __attribute__((always_inline)) void publish(uint32_t &word, uint32_t value) {
  __atomic_store_n(&word, value, __ATOMIC_RELEASE);
}

bool tryStatus(uint8_t value) {
  return Serial.availableForWrite() > 0 && Serial.write(value) == 1;
}

void sendStatus(uint8_t value) {
  pendingStatus = tryStatus(value) ? 0 : value;
}

uint32_t bufferedSamples() {
  return load(head) - load(tail);
}

inline __attribute__((always_inline)) uint64_t nextDeadline(uint64_t previous) {
  remainder += fractionalTicks;
  previous += wholeTicks;
  if (remainder >= renderRate) {
    remainder -= renderRate;
    ++previous;
  }
  return previous;
}

inline __attribute__((always_inline)) void endPlayback(uint8_t result) {
  radioIQ(0, 0);
  publish(playbackResult, result);
}

inline __attribute__((always_inline)) unsigned linearLevel(uint16_t ask) {
  if (ask == gateOff) return 0;
  const unsigned index = ask >> 8;
  const unsigned fraction = ask & 255u;
  const unsigned first = amplitude[index];
  if (fraction == 0) return first;
  return first - ((first - amplitude[index + 1]) * fraction + 128u) / 256u;
}

inline __attribute__((always_inline)) bool renderTick(bool output) {
  const uint32_t part = emittedTicks % oversampling;
  if (part == 0) {
    const uint32_t read = load(tail);
    const uint32_t ready = load(head) - read;
    if (!ready) return false;
    const PolarSample sample = ring[read & ringMask];
    level = linearLevel(sample.ask);
    nextLevel = ready > 1 ? linearLevel(ring[(read + 1) & ringMask].ask) : level;
    const int32_t delta = lsb ? -int32_t(sample.delta) : int32_t(sample.delta);
    phaseStep = ifPhaseStep + uint32_t(delta) * phasePerDelta;
    publish(tail, read + 1);
    ++stats.played;
  }
  if (output) {
    const unsigned blended = (level * (oversampling - part) + nextLevel * part) / oversampling;
    const int32_t gain = (blended * peakLevel + 32768u) >> 16;
    const uint32_t angle = phase >> (32 - 10);
    const int16_t i = int16_t((gain * sine[(angle + sineSize / 4) & (sineSize - 1)]) / 32768);
    const int16_t q = int16_t((gain * sine[angle]) / 32768);
    radioIQ(i, q);
  }
  phase += phaseStep;
  ++emittedTicks;
  return true;
}

bool IRAM_ATTR onAlarm(gptimer_handle_t handle, const gptimer_alarm_event_data_t *event, void *) {
  const uint32_t totalTicks = oversampling * wanted;
  if (emittedTicks == totalTicks) {
    endPlayback(Finish);
    return false;
  }

  const uint64_t now = event->count_value;
  const uint32_t late = now > event->alarm_value ? uint32_t(now - event->alarm_value) : 0;
  const uint32_t lateCycles = late * (240000000 / timerHz);
  if (lateCycles > stats.maxLateCycles) stats.maxLateCycles = lateCycles;
  if (lateCycles > 480) ++stats.lateTicks;

  uint64_t deadline = nextDeadline(event->alarm_value);
  unsigned due = 1;
  while (deadline <= now + 90 && emittedTicks + due < totalTicks) {
    if (due >= 8) {
      ++stats.overruns;
      endPlayback(RfFault);
      return false;
    }
    deadline = nextDeadline(deadline);
    ++due;
  }
  if (deadline <= now + 90) deadline = now + wholeTicks;

  gptimer_alarm_config_t alarm{};
  alarm.alarm_count = deadline;
  if (gptimer_set_alarm_action(handle, &alarm) != ESP_OK) {
    ++stats.overruns;
    endPlayback(RfFault);
    return false;
  }
  stats.skipped += due - 1;
  for (unsigned tick = 1; tick <= due; ++tick) {
    if (!renderTick(tick == due)) {
      ++stats.underruns;
      endPlayback(Underrun);
      return false;
    }
  }
  return false;
}

bool initTimer() {
  gptimer_config_t config{};
  config.clk_src = GPTIMER_CLK_SRC_PLL_F80M;
  config.direction = GPTIMER_COUNT_UP;
  config.resolution_hz = timerHz;
  config.intr_priority = 2;
  if (gptimer_new_timer(&config, &timer) != ESP_OK) return false;
  gptimer_event_callbacks_t callbacks{};
  callbacks.on_alarm = onAlarm;
  uint32_t resolution = 0;
  return gptimer_get_resolution(timer, &resolution) == ESP_OK && resolution == timerHz &&
         gptimer_register_event_callbacks(timer, &callbacks, nullptr) == ESP_OK &&
         gptimer_enable(timer) == ESP_OK;
}

bool startPlayback() {
  if (!radioSSB()) return false;
  gptimer_alarm_config_t alarm{};
  alarm.alarm_count = nextDeadline(0);
  if (gptimer_set_raw_count(timer, 0) != ESP_OK ||
      gptimer_set_alarm_action(timer, &alarm) != ESP_OK ||
      gptimer_start(timer) != ESP_OK) return false;
  lastPlayed = 0;
  lastPlaybackProgress = millis();
  timerRunning = true;
  return true;
}

void finishStream(uint8_t result) {
  if (timerRunning) {
    gptimer_stop(timer);
    timerRunning = false;
  }
  const bool returnToCw = (result == Finish || result == Ready) && cwRequested;
  waitingCw = returnToCw && radioCW(apwr);
  if (!waitingCw) {
    radioOff();
    if (returnToCw) result = RfFault;
    else if (result == Ready) result = RfOff;
  }
  const bool completed = result == Finish || result == Ready || result == RfOff;
  stats.lastError = completed ? 0u : result;
  state = completed ? State::Idle : State::Recover;
  if (!completed) cwRequested = false;
  stateSince = millis();
  sendStatus(result);
}

uint32_t fromLE(const uint8_t *bytes) {
  return uint32_t(bytes[0]) | (uint32_t(bytes[1]) << 8) |
         (uint32_t(bytes[2]) << 16) | (uint32_t(bytes[3]) << 24);
}

void sendDiagnostics() {
  const uint32_t words[] = {stats.played, stats.lateTicks, stats.maxLateCycles,
                            stats.skipped, stats.overruns, stats.underruns,
                            stats.rxPeak, stats.lastError};
  uint8_t reply[1 + sizeof(words)] = {DiagnosticReply};
  for (size_t i = 0; i < sizeof(words) / sizeof(*words); ++i) {
    for (unsigned byte = 0; byte < 4; ++byte) {
      reply[1 + 4 * i + byte] = uint8_t(words[i] >> (8 * byte));
    }
  }
  Serial.write(reply, sizeof(reply));
}

void prepareStream() {
  const uint32_t total = fromLE(header);
  const uint8_t rateIndex = header[4];
  const uint32_t warmupSeconds = uint32_t(header[7]) | (uint32_t(header[8]) << 8);
  if (total < 256 || total > maxSamples || rateIndex >= sizeof(rates) / sizeof(*rates) ||
      header[5] > 191 || header[6] > 1 || warmupSeconds < 1 || warmupSeconds > 900) {
    state = State::Idle;
    sendStatus(BadCommand);
    return;
  }
  stats = {};
  if (!timer) {
    finishStream(RfFault);
    return;
  }
  apwr = header[5];
  wanted = total;
  received = 0;
  publish(head, 0);
  publish(tail, 0);
  publish(uartError, 0);
  publish(playbackResult, 0);
  lsb = header[6] == 1;
  warmupMs = warmupSeconds * 1000;
  renderRate = oversampling * rates[rateIndex];
  wholeTicks = timerHz / renderRate;
  fractionalTicks = timerHz % renderRate;
  const uint64_t cyclesQ32 = uint64_t(1) << 32;
  phasePerDelta = (uint64_t(originalStepHz) * 65536u + renderRate / 2) / renderRate;
  ifPhaseStep = (cyclesQ32 * ssbDigitalIFHz + renderRate / 2) / renderRate;
  const unsigned backoff = (unsigned(apwr) * 48u + 95u) / 191u;
  peakLevel = unsigned(lroundf(360.0f * powf(10.0f, -float(backoff) / 80.0f)));
  remainder = 0;
  emittedTicks = 0;
  phase = 0;
  wireSample = 0;
  paused = false;
  repeatResumeUntil = 0;
  if (!radioCW(apwr)) {
    finishStream(RfFault);
    return;
  }
  waitingCw = true;
  stateSince = millis();
  state = State::Warmup;
  sendStatus(Preheat);
}

void command(uint8_t byte) {
  switch (byte) {
    case Ping:
      sendStatus(waitingCw ? Ready : RfOff);
      break;
    case Off:
      cwRequested = false;
      waitingCw = false;
      radioOff();
      sendStatus(RfOff);
      break;
    case Cw:
      cwRequested = timer && radioCW(apwr);
      waitingCw = cwRequested;
      if (!waitingCw) radioOff();
      sendStatus(waitingCw ? Ready : RfFault);
      break;
    case Identity:
      sendStatus(timer ? IdentityReply : RfFault);
      break;
    case Diagnostics:
      sendDiagnostics();
      break;
    case Start:
      headerUsed = 0;
      stateSince = millis();
      state = State::Header;
      break;
    case SelectBand:
      stateSince = millis();
      state = State::Band;
      break;
    default:
      sendStatus(BadCommand);
  }
}

void selectBand(uint8_t band) {
  state = State::Idle;
  if (band > 1) {
    sendStatus(BadCommand);
    return;
  }
  cwRequested = false;
  waitingCw = false;
  radioOff();
  radioSetBand(band == 1);
  const uint8_t reply[] = {BandReply, band};
  Serial.write(reply, sizeof(reply));
}

void updateFlow(uint32_t now) {
  const uint32_t used = bufferedSamples();
  const bool wantPause = received < sampleBytes * wanted && (paused ? used > lowWater : used >= highWater);
  if (wantPause != paused) {
    paused = wantPause;
    lastFlow = now - flowRepeatMs;
    repeatResumeUntil = paused ? 0 : now + resumeRepeatMs;
  }
  if ((paused || (repeatResumeUntil && int32_t(repeatResumeUntil - now) > 0)) &&
      now - lastFlow >= flowRepeatMs && tryStatus(paused ? Pause : Resume)) {
    lastFlow = now;
  }
  if (now - lastTelemetry >= 100 && tryStatus((used * 100 + ringSamples / 2) / ringSamples)) {
    lastTelemetry = now;
  }
  if (now - lastFifo >= 250 && tryStatus(202 + min<uint32_t>(32, (stats.rxPeak + 3) / 4))) {
    lastFifo = now;
  }
}

void stream() {
  if (load(uartError)) {
    ++stats.overruns;
    finishStream(Overflow);
    return;
  }
  if (const uint32_t result = load(playbackResult)) {
    finishStream(result);
    return;
  }
  const size_t remaining = sampleBytes * wanted - received;
  const size_t room = sampleBytes * (ringSamples - bufferedSamples()) - (received % sampleBytes);
  uint8_t bytes[256];
  const size_t count = min(min(sizeof(bytes), remaining), min(room, size_t(Serial.available())));
  if (count) {
    const size_t available = Serial.read(bytes, count);
    uint32_t write = load(head);
    for (size_t i = 0; i < available; ++i, ++received) {
      const unsigned part = received % sampleBytes;
      if (part == 0) wireSample = 0;
      wireSample |= uint32_t(bytes[i]) << (8u * part);
      if (part == sampleBytes - 1) {
        const uint16_t ask = uint16_t(wireSample);
        if (ask > maxAsk && ask != gateOff) {
          finishStream(BadCommand);
          return;
        }
        ring[write++ & ringMask] = {ask, int16_t(wireSample >> 16)};
      }
    }
    publish(head, write);
    lastSerialByte = millis();
  }

  const uint32_t fifo = uart_ll_get_rxfifo_len(&UART0);
  stats.rxPeak = max(stats.rxPeak, fifo);
  if (!timerRunning && (bufferedSamples() >= prefill || received == sampleBytes * wanted)) {
    if (!startPlayback()) {
      finishStream(RfFault);
      return;
    }
    waitingCw = false;
    sendStatus(Play);
  }
  const uint32_t now = millis();
  if (timerRunning) {
    const uint32_t played = load(tail);
    if (played != lastPlayed) {
      lastPlayed = played;
      lastPlaybackProgress = now;
    } else if (now - lastPlaybackProgress >= playbackStallMs) {
      ++stats.overruns;
      finishStream(RfFault);
      return;
    }
  }
  updateFlow(now);
  if (!paused && received < sampleBytes * wanted && now - lastSerialByte >= timeoutMs) finishStream(Timeout);
}
}  // namespace

void setup() {
  Serial.setRxBufferSize(4096);
  Serial.begin(baud, SERIAL_8N1);
  Serial.setRxFIFOFull(64);
  Serial.onReceiveError([](hardwareSerial_error_t error) {
    if (error != UART_BREAK_ERROR) publish(uartError, uint32_t(error));
  });
  for (uint32_t n = 0; n < sineSize; ++n) {
    sine[n] = int16_t(lroundf(32767.0f * sinf(2.0f * PI * n / sineSize)));
  }
  for (unsigned ask = 0; ask < 128; ++ask) {
    amplitude[ask] = uint16_t(lroundf(65535.0f * powf(10.0f, -0.2429f * ask / 20.0f)));
  }
  if (getCpuFrequencyMhz() != 240 || !initTimer()) {
    timer = nullptr;
    sendStatus(RfFault);
  } else {
    sendStatus(RfOff);
  }
}

void loop() {
  if (pendingStatus && tryStatus(pendingStatus)) pendingStatus = 0;
  const uint32_t now = millis();
  switch (state) {
    case State::Idle:
      if (Serial.available()) command(Serial.read());
      break;
    case State::Header:
      while (headerUsed < sizeof(header) && Serial.available()) header[headerUsed++] = Serial.read();
      if (headerUsed == sizeof(header)) prepareStream();
      else if (now - stateSince >= timeoutMs) finishStream(Timeout);
      break;
    case State::Band:
      if (Serial.available()) selectBand(Serial.read());
      else if (now - stateSince >= timeoutMs) finishStream(Timeout);
      break;
    case State::Warmup:
      if (Serial.available()) {
        const int byte = Serial.read();
        if (byte == Off) {
          cwRequested = false;
          finishStream(RfOff);
        } else if (byte == CancelPreheat) finishStream(Ready);
        else if (byte == Ping) sendStatus(Preheat);
        else finishStream(BadCommand);
      } else if (now - stateSince >= warmupMs) {
        state = State::Stream;
        lastSerialByte = now;
        lastTelemetry = now;
        lastFifo = now;
        sendStatus(StreamReady);
      }
      break;
    case State::Stream:
      stream();
      break;
    case State::Recover:
      if (Serial.available()) {
        uint8_t discard[128];
        Serial.read(discard, min(sizeof(discard), size_t(Serial.available())));
        stateSince = now;
      } else if (now - stateSince >= recoveryQuietMs) {
        state = State::Idle;
      }
      break;
  }
  if (Serial.available() == 0 || (state == State::Stream && bufferedSamples() >= highWater)) delay(1);
}
