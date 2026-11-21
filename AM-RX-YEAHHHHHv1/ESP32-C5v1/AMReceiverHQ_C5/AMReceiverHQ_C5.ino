// SPDX-License-Identifier: GPL-3.0-only
// ESP32-C5 port of kaboom748/ESP-AnalogRadio AMReceiverHQ.
// Keeps the upstream 63-tap AM channel filter and AM01 PCM16 wire format.
#include <Arduino.h>
#include <driver/gptimer.h>
#include <hal/uart_ll.h>
#include <inttypes.h>
#include "rx_radio.h"

#if ARDUINO_USB_CDC_ON_BOOT
#error "Select USB CDC On Boot: Disabled for the UART0 USB bridge."
#endif

namespace {
constexpr uint32_t cpuHz = 240000000;
constexpr uint32_t baud = 2000000;
constexpr uint32_t iqRate = 96000;
constexpr uint32_t audioRate = 48000;
constexpr uint32_t timerHz = 40000000;
constexpr uint32_t ringSize = 1024;
constexpr int16_t unavailable = INT16_MIN;
constexpr uint32_t frameSamples = 64;
constexpr uint32_t frameBytes = 138;
constexpr unsigned framesQueued = 8;
constexpr uint32_t carrierFloor = 256;  // Q4 ADC, equivalent to the original Q5 minimum.
constexpr int16_t channelTaps[] = {
  -1, 2, 5, -1, -11, -5, 18, 19, -19, -42, 5, 67, 29, -83, -88, 72,
  163, -13, -233, -106, 265, 288, -213, -518, 21, 764, 399, -986, -1308, 1139, 5063, 7000,
};
constexpr unsigned tapPairs = sizeof(channelTaps) / sizeof(*channelTaps) - 1;
constexpr unsigned filterLength = 2 * tapPairs + 1;

struct Slot {
  uint32_t index;
  int16_t i, q;
};
struct CaptureStats {
  uint32_t captured, skipped, timeouts, ringDrops, maxLate;
};
struct AudioStats {
  uint32_t samples, sent, invalid, queueDrops, clippedIq, clippedPcm, peak;
};

gptimer_handle_t timer;
bool streaming = false;
int gainIndex = -1;
uint64_t programmedCarrierHz = 0;
Slot iqRing[ringSize];
uint32_t writeIndex, readIndex, timingFault;
uint32_t slotIndex, deadlineFraction;
CaptureStats captureStats{};
AudioStats audioStats{};

struct Demodulator {
  int16_t iHistory[128], qHistory[128];
  uint32_t position, filled, expected;
  int32_t iDc, qDc, envelopeDc, carrier;
  int32_t inverseCarrier;
  uint32_t gainCountdown;
  int16_t heldI, heldQ;
  bool incompletePair;
};
Demodulator demod{};
uint8_t frame[frameBytes];
uint8_t transmitQueue[framesQueued][frameBytes];
uint16_t crcTable[256], frameCrc;
uint32_t frameSize, lastIqProgress;
unsigned transmitHead, transmitTail, transmitOffset;
uint32_t uartInterrupts;

inline __attribute__((always_inline)) uint32_t load(const uint32_t &word) {
  return __atomic_load_n(&word, __ATOMIC_ACQUIRE);
}
inline __attribute__((always_inline)) void publish(uint32_t &word, uint32_t value) {
  __atomic_store_n(&word, value, __ATOMIC_RELEASE);
}

inline __attribute__((always_inline)) uint64_t nextDeadline(uint64_t previous) {
  previous += timerHz / iqRate;
  deadlineFraction += timerHz % iqRate;
  if (deadlineFraction >= iqRate) {
    deadlineFraction -= iqRate;
    ++previous;
  }
  return previous;
}

bool IRAM_ATTR captureTick(gptimer_handle_t handle, const gptimer_alarm_event_data_t *event, void *) {
  const uint64_t now = event->count_value;
  const uint32_t latency = now > event->alarm_value ? uint32_t(now - event->alarm_value) : 0;
  if (latency > captureStats.maxLate) captureStats.maxLate = latency;
  uint64_t next = nextDeadline(event->alarm_value);
  unsigned skipped = 0;
  while (next <= now + 80) {
    if (++skipped >= 16) {
      publish(timingFault, 1);
      return false;
    }
    next = nextDeadline(next);
  }
  slotIndex += skipped;
  captureStats.skipped += skipped;
  gptimer_alarm_config_t alarm{};
  alarm.alarm_count = next;
  if (gptimer_set_alarm_action(handle, &alarm) != ESP_OK) {
    publish(timingFault, 1);
    return false;
  }
  Slot slot{slotIndex++, unavailable, 0};
  if (rxSample(&slot.i, &slot.q)) ++captureStats.captured;
  else {
    slot.i = unavailable;
    ++captureStats.timeouts;
  }
  const uint32_t write = load(writeIndex);
  if (write - load(readIndex) < ringSize) {
    iqRing[write & (ringSize - 1)] = slot;
    publish(writeIndex, write + 1);
  } else {
    ++captureStats.ringDrops;
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
  callbacks.on_alarm = captureTick;
  uint32_t resolution = 0;
  return gptimer_get_resolution(timer, &resolution) == ESP_OK && resolution == timerHz &&
         gptimer_register_event_callbacks(timer, &callbacks, nullptr) == ESP_OK &&
         gptimer_enable(timer) == ESP_OK;
}

void initCrc() {
  for (unsigned i = 0; i < 256; ++i) {
    uint16_t crc = i << 8;
    for (unsigned bit = 0; bit < 8; ++bit) {
      crc = (crc << 1) ^ ((crc & 0x8000) ? 0x1021 : 0);
    }
    crcTable[i] = crc;
  }
}

inline void frameByte(uint8_t byte) {
  frame[frameSize++] = byte;
  frameCrc = (frameCrc << 8) ^ crcTable[(frameCrc >> 8) ^ byte];
}

void audioFrame(int16_t value) {
  if (value == unavailable) ++audioStats.invalid;
  if (!frameSize) {
    memcpy(frame, "AM01", 4);
    frameSize = 4;
    frameCrc = 0xffff;
    for (unsigned byte = 0; byte < 4; ++byte) frameByte(audioStats.samples >> (8 * byte));
  }
  ++audioStats.samples;
  frameByte(uint16_t(value));
  frameByte(uint16_t(value) >> 8);
  if (frameSize == frameBytes - 2) {
    frame[frameBytes - 2] = frameCrc;
    frame[frameBytes - 1] = frameCrc >> 8;
    const unsigned next = (transmitHead + 1) % framesQueued;
    if (next != transmitTail) {
      memcpy(transmitQueue[transmitHead], frame, frameBytes);
      transmitHead = next;
    } else {
      audioStats.queueDrops += frameSamples;
    }
    frameSize = 0;
  }
}

void pumpAudio() {
  if (transmitHead == transmitTail) return;
  const uint32_t room = uart_ll_get_txfifo_len(&UART0);
  const uint32_t count = min(room, frameBytes - transmitOffset);
  if (!count) return;
  uart_ll_write_txfifo(&UART0, transmitQueue[transmitTail] + transmitOffset, count);
  transmitOffset += count;
  if (transmitOffset == frameBytes) {
    transmitOffset = 0;
    transmitTail = (transmitTail + 1) % framesQueued;
    audioStats.sent += frameSamples;
  }
}

uint32_t magnitude(uint32_t squared) {
  if (!squared) return 0;
  uint32_t bit = 1u << ((31 - __builtin_clz(squared)) & ~1u);
  uint32_t root = 0;
  while (bit) {
    const uint32_t trial = root + bit;
    root >>= 1;
    if (squared >= trial) {
      squared -= trial;
      root += bit;
    }
    bit >>= 2;
  }
  return root;
}

int16_t demodulate() {
  const int16_t *latestI = demod.iHistory + demod.position + 64;
  const int16_t *latestQ = demod.qHistory + demod.position + 64;
  const int16_t *earliestI = latestI - (filterLength - 1);
  const int16_t *earliestQ = latestQ - (filterLength - 1);
  int32_t filteredI = 0;
  int32_t filteredQ = 0;
  for (unsigned tap = 0; tap < tapPairs; ++tap) {
    const int32_t weight = channelTaps[tap];
    filteredI += weight * (*earliestI++ + *latestI--);
    filteredQ += weight * (*earliestQ++ + *latestQ--);
  }
  const int32_t i = constrain((filteredI + channelTaps[tapPairs] * *latestI) >> 14, -30000, 30000);
  const int32_t q = constrain((filteredQ + channelTaps[tapPairs] * *latestQ) >> 14, -30000, 30000);
  const uint32_t envelope = magnitude(i * i + q * q);
  if (envelope > audioStats.peak) audioStats.peak = envelope;
  const int32_t value = int32_t(envelope) * 4096;
  if (!demod.inverseCarrier) {
    demod.envelopeDc = demod.carrier = value;
    demod.inverseCarrier = (32767 << 4) / max(carrierFloor, envelope);
  }
  demod.envelopeDc += (value - demod.envelopeDc + 512) >> 10;
  demod.carrier += (value - demod.carrier + 4096) >> 13;
  if (++demod.gainCountdown == 128) {
    demod.gainCountdown = 0;
    demod.inverseCarrier = (32767 << 4) / max(carrierFloor, uint32_t(demod.carrier >> 12));
  }
  const int32_t sample = ((16 * int32_t(envelope) - (demod.envelopeDc >> 8)) *
                           demod.inverseCarrier + 128) >> 8;
  if (sample > 32767 || sample < -32767) {
    ++audioStats.clippedPcm;
    return sample > 0 ? 32767 : -32767;
  }
  return sample;
}

void saveToFilter(int16_t i, int16_t q) {
  const uint32_t position = demod.position = (demod.position + 1) & 63;
  demod.iHistory[position] = demod.iHistory[position + 64] = i;
  demod.qHistory[position] = demod.qHistory[position + 64] = q;
  if (demod.filled < filterLength) ++demod.filled;
}

void missingSlot(uint32_t index) {
  // Hold the last mixed sample through a missing slot. Flag its audio pair
  // without discarding the filter history for all the following samples.
  saveToFilter(demod.heldI, demod.heldQ);
  if (index & 1) {
    audioFrame(unavailable);
    demod.incompletePair = false;
  } else demod.incompletePair = true;
}

void processSlot(const Slot &slot) {
  while (demod.expected != slot.index) missingSlot(demod.expected++);
  ++demod.expected;
  if (slot.i == unavailable) {
    missingSlot(slot.index);
    return;
  }
  if (abs(slot.i) >= 8100 || abs(slot.q) >= 8100) ++audioStats.clippedIq;
  demod.iDc += (4096 * int32_t(slot.i) - demod.iDc) >> 10;
  demod.qDc += (4096 * int32_t(slot.q) - demod.qDc) >> 10;
  const int32_t ci = slot.i - (demod.iDc >> 12);
  const int32_t cq = slot.q - (demod.qDc >> 12);
  int16_t i, q;
  // Mix the 24 kHz low IF to zero using exp(-j*pi/2*n).
  switch (slot.index & 3) {
    case 0: i = ci; q = cq; break;
    case 1: i = cq; q = -ci; break;
    case 2: i = -ci; q = -cq; break;
    default: i = -cq; q = ci; break;
  }
  demod.heldI = i;
  demod.heldQ = q;
  saveToFilter(i, q);
  if (slot.index & 1) {
    audioFrame(!demod.incompletePair && demod.filled == filterLength ? demodulate() : unavailable);
    demod.incompletePair = false;
  }
}

void stopStream(bool error) {
  gptimer_stop(timer);
  streaming = false;
  const uint32_t drainStart = millis();
  while ((transmitHead != transmitTail || !uart_ll_is_tx_idle(&UART0)) &&
         millis() - drainStart < 50) pumpAudio();
  uart_ll_rxfifo_rst(&UART0);
  uart_ll_clr_intsts_mask(&UART0, UINT32_MAX);
  uart_ll_ena_intr_mask(&UART0, uartInterrupts);
  rxStop();
  while (Serial.available()) Serial.read();
  Serial.printf("\nEND samples=%" PRIu32 " sent=%" PRIu32 " iq=%" PRIu32
                " skipped_iq=%" PRIu32 " timeouts=%" PRIu32 " invalid=%" PRIu32
                " queue_drops=%" PRIu32 " max_late=%" PRIu32 " lo_hz=%" PRIu64
                " carrier=%" PRIu32 " peak=%" PRIu32 " clipped_pcm=%" PRIu32
                " clipped_iq=%" PRIu32 " ring_drops=%" PRIu32 " clock_fault=%u\n",
                audioStats.samples, audioStats.sent, captureStats.captured, captureStats.skipped,
                captureStats.timeouts, audioStats.invalid, audioStats.queueDrops,
                captureStats.maxLate * (cpuHz / timerHz), programmedCarrierHz - iqRate / 4,
                uint32_t(demod.carrier >> 12), audioStats.peak, audioStats.clippedPcm,
                audioStats.clippedIq, captureStats.ringDrops, unsigned(error));
}

void receiveAudio() {
  if (uart_ll_get_rxfifo_len(&UART0)) {
    stopStream(false);
    return;
  }
  uint32_t read = load(readIndex);
  const uint32_t initialRead = read;
  const uint32_t write = load(writeIndex);
  for (unsigned i = 0; i < 32 && read != write; ++i) {
    const Slot slot = iqRing[read & (ringSize - 1)];
    publish(readIndex, ++read);
    processSlot(slot);
  }
  pumpAudio();
  if (read != initialRead) lastIqProgress = millis();
  if (load(timingFault) || millis() - lastIqProgress >= 80) stopStream(true);
  else if (read == initialRead) delay(1);
}

bool allowed(uint32_t khz) {
  return (khz >= 2412000 && khz <= 2472000) ||
         (khz >= 5180000 && khz <= 5885000);
}

void startStream(uint32_t khz) {
  if (!allowed(khz)) {
    Serial.println("ERR FREQUENCY");
    return;
  }
  if (!timer || getCpuFrequencyMhz() != 240) {
    Serial.println("ERR TIMER");
    return;
  }
  if (!rxStart(khz, gainIndex, &programmedCarrierHz)) {
    Serial.println("ERR TUNE");
    return;
  }
  demod = {};
  captureStats = {};
  audioStats = {};
  slotIndex = deadlineFraction = frameSize = 0;
  transmitHead = transmitTail = transmitOffset = 0;
  publish(writeIndex, 0);
  publish(readIndex, 0);
  publish(timingFault, 0);
  gptimer_alarm_config_t alarm{};
  alarm.alarm_count = nextDeadline(0);
  if (gptimer_set_raw_count(timer, 0) != ESP_OK ||
      gptimer_set_alarm_action(timer, &alarm) != ESP_OK) {
    rxStop();
    Serial.println("ERR TIMER");
    return;
  }
  Serial.printf("OK START %" PRIu64 " %" PRIu32 " HQ\n", programmedCarrierHz, audioRate);
  Serial.flush();
  uartInterrupts = uart_ll_get_intr_ena_status(&UART0);
  uart_ll_disable_intr_mask(&UART0, UINT32_MAX);
  if (gptimer_start(timer) != ESP_OK) {
    stopStream(true);
    return;
  }
  lastIqProgress = millis();
  streaming = true;
}

void command(const char *line) {
  if (!strcmp(line, "STATUS")) {
    Serial.println("OK STATUS AMReceiverHQ-C5 1 240 2000000 32767");
    return;
  }
  if (!strcmp(line, "STOP") || !strcmp(line, "!")) {
    Serial.println("OK STOP");
    return;
  }
  if (!strcmp(line, "GAIN AUTO")) {
    gainIndex = -1;
    Serial.println("OK GAIN AUTO");
    return;
  }
  if (!strncmp(line, "GAIN ", 5)) {
    int requested = 0;
    char extra;
    if (sscanf(line + 5, "%d %c", &requested, &extra) == 1 && requested >= 0 && requested <= 55) {
      gainIndex = requested;
      Serial.printf("OK GAIN %d\n", gainIndex);
    } else Serial.println("ERR GAIN");
    return;
  }
  if (!strncmp(line, "START ", 6)) {
    unsigned long khz = 0;
    char mode[3], extra;
    if (sscanf(line + 6, "%lu %2s %c", &khz, mode, &extra) == 2 && !strcmp(mode, "HQ")) {
      startStream(khz);
    } else Serial.println("ERR START");
    return;
  }
  Serial.println("ERR COMMAND");
}
}  // namespace

void setup() {
  Serial.setTxBufferSize(4096);
  Serial.begin(baud, SERIAL_8N1);
  Serial.setDebugOutput(false);
  initCrc();
  if (!initTimer()) timer = nullptr;
}

void loop() {
  if (streaming) {
    receiveAudio();
    return;
  }
  static char line[64];
  static unsigned length = 0;
  static bool discarded = false;
  while (!streaming && Serial.available()) {
    const int c = Serial.read();
    if (c == '\n') {
      if (discarded) Serial.println("ERR COMMAND");
      else if (length) {
        line[length] = 0;
        command(line);
      }
      length = 0;
      discarded = false;
    } else if (c != '\r' && c != -1) {
      if (c < 32 || c > 126 || length + 1 >= sizeof(line)) discarded = true;
      else if (!discarded) line[length++] = c;
    }
  }
  if (!streaming) delay(1);
}
