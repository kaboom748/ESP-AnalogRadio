/*
  FMStereoRX for ESP8266EX at 160 MHz, Arduino ESP8266 core 3.1.2.
  RX PHY tuning and IQ estimator setup adapted from SP8ESA, with permission
  under the MIT License below.
  https://github.com/SP8ESA/ESP8266_2.4GHz_SSB_TRX

  MIT License

  Copyright (c) 2026 SP8ESA

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
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
constexpr uint32_t cpuHz = 160000000;
#ifndef FMS_RX_BAUD
#define FMS_RX_BAUD 1500000
#endif
constexpr uint32_t baud = FMS_RX_BAUD;
constexpr uint32_t iqCtrl = 0x6000057Cu;
constexpr uint32_t iqSumI = 0x600005DCu;
constexpr uint32_t iqSumQ = 0x600005E0u;
constexpr uint32_t iqMask = 0xFFFA0001u;
constexpr uint32_t iqDone = 0x80000000u;
constexpr uint8_t frameCount = 8;
constexpr uint16_t payloadSize = 128;
constexpr uint16_t frameSize = 4 + 4 + payloadSize + 2;
constexpr uint32_t deviationFullScaleHz = 75000;

const int16_t robustTaps[] = {0, 22, 7, -66, -40, 140, 127, -239, -314, 353, 681, -459, -1496, 535, 5128, 7626};
const int16_t monoTaps[] = {3, -2, -21, -57, -106, -151, -167, -120, 21, 275, 637, 1074, 1528, 1927, 2201, 2300};

struct Profile {
  const char* name;
  uint32_t iqRate;
  uint32_t audioRate;
  const int16_t* taps;
  uint8_t halfTaps;
  uint8_t decimation;
};

const Profile profiles[] = {
  {"HQ", 240000, 120000, nullptr, 9, 2},
  {"ROBUST", 192000, 96000, robustTaps, sizeof(robustTaps) / sizeof(int16_t) - 1, 2},
  {"MONO", 192000, 48000, monoTaps, sizeof(monoTaps) / sizeof(int16_t) - 1, 4},
};

struct Stats {
  uint32_t captured;
  uint32_t skipped;
  uint32_t timeouts;
  uint32_t samples;
  uint32_t sent;
  uint32_t invalid;
  uint32_t queueDrops;
  uint32_t maxLate;
};

uint8_t frames[frameCount][frameSize];
uint16_t crcTable[256];
uint16_t angles[257];
uint32_t reciprocals[256];
int32_t filterHistory[256];

static inline __attribute__((always_inline)) uint32_t cycles() {
  uint32_t result;
  asm volatile("rsr %0, ccount" : "=a"(result));
  return result;
}

static inline __attribute__((always_inline)) uint32_t readRegister(uint32_t address) {
  return *reinterpret_cast<volatile uint32_t*>(address);
}

static inline __attribute__((always_inline)) void writeRegister(uint32_t address, uint32_t value) {
  *reinterpret_cast<volatile uint32_t*>(address) = value;
}

static inline __attribute__((always_inline)) uint16_t updateCrc(uint16_t crc, uint8_t byte) {
  return (crc << 8) ^ crcTable[(crc >> 8) ^ byte];
}

static inline __attribute__((always_inline)) void pushHistory(
    int32_t value, uint8_t& index, uint8_t& count, uint8_t taps) {
  index = (index + 1) & 127;
  filterHistory[index] = value;
  filterHistory[index + 128] = value;
  if (count < taps) ++count;
}

// 19-tap halfband at 240 kS/s. Except for the center, every second tap is
// zero. Writing the five symmetric pairs directly avoids loop overhead.
static inline __attribute__((always_inline)) int32_t filterHq(const int32_t* newest) {
  return 78 * (newest[0] + newest[-18])
       - 265 * (newest[-2] + newest[-16])
       + 640 * (newest[-4] + newest[-14])
       - 1467 * (newest[-6] + newest[-12])
       + 5110 * (newest[-8] + newest[-10])
       + 8192 * newest[-9];
}

// 0x80 is reserved for an unavailable MPX sample. It is carried in the same
// timeline as ordinary samples, even when a frame straddles a radio gap.
static inline __attribute__((always_inline)) void appendFrame(
    uint32_t index, uint8_t sample, uint8_t readFrame, uint8_t& writeFrame,
    uint16_t& fill, uint16_t& crc, Stats& stats) {
  if (sample == 0x80) ++stats.invalid;
  const uint8_t next = (writeFrame + 1) & (frameCount - 1);
  if (!fill && next == readFrame) {
    ++stats.queueDrops;
    return;
  }
  auto* frame = frames[writeFrame];
  if (!fill) {
    frame[0] = 'M';
    frame[1] = 'P';
    frame[2] = 'X';
    frame[3] = '1';
    crc = 0xffffu;
    for (uint8_t byte = 0; byte < 4; ++byte) {
      frame[4 + byte] = index >> (8 * byte);
      crc = updateCrc(crc, frame[4 + byte]);
    }
  }
  frame[8 + fill] = sample;
  crc = updateCrc(crc, sample);
  if (++fill == payloadSize) {
    frame[8 + payloadSize] = crc;
    frame[9 + payloadSize] = crc >> 8;
    writeFrame = next;
    fill = 0;
  }
}

static void prepareTables() {
  for (uint32_t i = 0; i < 256; ++i) {
    uint16_t crc = i << 8;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc << 1) ^ ((crc & 0x8000u) ? 0x1021u : 0u);
    }
    crcTable[i] = crc;
    reciprocals[i] = (1u << 24) / (256 + i);
  }
  for (uint32_t i = 0; i <= 256; ++i) {
    angles[i] = static_cast<uint16_t>(atanf(i * (1.0f / 256.0f)) * (32768.0f / PI) + 0.5f);
  }
}

// Conjugate-product phase. The lookup tables avoid floating point and division
// on the acquisition path. One turn equals 65536 phase units.
static inline __attribute__((always_inline)) int32_t discriminate(int32_t i, int32_t q, int32_t pi, int32_t pq) {
  const int32_t dot = i * pi + q * pq;
  const int32_t cross = q * pi - i * pq;
  const uint32_t a = dot < 0 ? -dot : dot;
  const uint32_t b = cross < 0 ? -cross : cross;
  const uint32_t large = a >= b ? a : b;
  if (!large) return 0;
  const uint32_t small = a >= b ? b : a;
  const uint32_t shift = __builtin_clz(large);
  const uint32_t normalized = large << shift;
  const uint32_t ratio = (((small << shift) >> 16) * reciprocals[(normalized >> 23) - 256]) >> 23;
  int32_t angle = angles[ratio > 256 ? 256 : ratio];
  if (b > a) angle = 16384 - angle;
  if (dot < 0) angle = 32768 - angle;
  return cross < 0 ? -angle : angle;
}

static uint32_t tune(uint32_t khz) {
  const uint8_t channel = constrain((static_cast<int32_t>(khz) - 2407000 + 2500) / 5000, 1, 13);
  const int mhz = 2407 + 5 * channel;
  const int32_t delta = static_cast<int32_t>(khz) - mhz * 1000;
  const int32_t off = (delta * 1024 + (delta < 0 ? -500 : 500)) / 1000;
  if (!wifi_set_channel(channel)) return 0;
  delay(40);
  if (wifi_get_channel() != channel) return 0;
  set_rf_freq_offset(chip6_phy_init_ctrl[1], mhz, off);
  return mhz * 1000000LL + off * 1000000LL / 1024;
}

static bool startPhy() {
  WiFi.persistent(false);
  if (!WiFi.mode(WIFI_STA)) return false;
  wifi_station_set_auto_connect(0);
  wifi_station_set_reconnect_policy(false);
  wifi_station_disconnect();
  if (!wifi_set_sleep_type(NONE_SLEEP_T)) return false;
  delay(100);
  return true;
}

// All hot functions run from IRAM or inline into this loop. The estimator
// captures the next I/Q window while the CPU processes the previous one.
static void IRAM_ATTR runReceiver(const Profile& profile, Stats& stats) {
  const uint32_t period = cpuHz / profile.iqRate;
  const uint32_t remainder = cpuHz % profile.iqRate;
  const int32_t scale = (128u * profile.iqRate + deviationFullScaleHz / 2) / deviationFullScaleHz;
  const uint32_t cadence = profile.iqRate;
  const uint8_t decimation = profile.decimation;
  const uint8_t half = profile.halfTaps;
  const uint8_t taps = half * 2 + 1;
  const int16_t* coefficients = profile.taps;
  const bool halfband = coefficients == nullptr;
  uint32_t fractional = 0;
  uint32_t deadline = cycles() + 160;
  uint32_t sampleIndex = 0;
  uint32_t priorIndex = 0;
  uint8_t phase = 0;
  uint8_t writeFrame = 0, readFrame = 0;
  uint16_t fill = 0, send = 0, crc = 0;
  uint8_t history = 0, historyValid = 0;
  int32_t i = 0, q = 0, oldI = 0, oldQ = 0;
  int32_t dcI = 0, dcQ = 0, frequencyDc = 0;
  int32_t lastMpx = 0;
  bool haveSlot = false, badGroup = false;
  bool previous = false, previousOutput = false, phaseValid = false;
  uint32_t watchdog = 0;
  uint32_t maxLate = 0;

  writeRegister(iqCtrl, readRegister(iqCtrl) | 1u);
  const uint32_t estimator = (readRegister(iqCtrl) & (iqMask & ~iqDone)) | (31u << 2) | 1u;

  while (!((USS(0) >> USRXC) & 0xff)) {
    while (static_cast<int32_t>(cycles() - deadline) < 0) {}
    uint32_t now = cycles();
    if (static_cast<int32_t>(now - deadline) > static_cast<int32_t>(period / 3)) {
      if (haveSlot) pushHistory(lastMpx, history, historyValid, taps);
      if (previousOutput) {
        appendFrame(priorIndex, 0x80, readFrame, writeFrame, fill, crc, stats);
        previousOutput = false;
      }
      do {
        ++stats.skipped;
        pushHistory(lastMpx, history, historyValid, taps);
        if (++phase == decimation) {
          phase = 0;
          appendFrame(sampleIndex++, 0x80, readFrame, writeFrame, fill, crc, stats);
        }
        deadline += period;
        fractional += remainder;
        if (fractional >= cadence) {
          fractional -= cadence;
          ++deadline;
        }
      } while (static_cast<int32_t>(now - deadline) >= 0);
      haveSlot = false;
      badGroup = true;
      phaseValid = false;
      continue;
    }
    uint32_t late = now - deadline;
    if (late > maxLate) maxLate = late;

    writeRegister(iqCtrl, estimator | 2u);
    if (haveSlot) {
      if (phase == 1) {
        badGroup = false;
      }
      if (previous) {
        dcI += (i * 65536 - dcI) >> 12;
        dcQ += (q * 65536 - dcQ) >> 12;
        const int32_t ci = i - (dcI >> 16);
        const int32_t cq = q - (dcQ >> 16);
        if (phaseValid) {
          const int32_t value = discriminate(ci, cq, oldI, oldQ);
          frequencyDc += (value * 4096 - frequencyDc) >> 13;
          lastMpx = value - (frequencyDc >> 12);
        } else {
          badGroup = true;
        }
        oldI = ci;
        oldQ = cq;
      } else {
        badGroup = true;
      }
      phaseValid = previous;
      // The FIR sees a held sample through a radio gap; only the affected
      // decimation groups are tagged invalid on the serial timeline.
      pushHistory(lastMpx, history, historyValid, taps);
      if (previousOutput) {
        uint8_t output = 0x80;
        if (!badGroup && historyValid == taps) {
          const int32_t* newest = &filterHistory[history + 128];
          int32_t filtered;
          if (halfband) {
            filtered = filterHq(newest);
          } else {
            const int32_t* oldest = newest - (taps - 1);
            filtered = 0;
            for (uint8_t tap = 0; tap < half; ++tap) {
              filtered += coefficients[tap] * (*oldest++ + *newest--);
            }
            filtered += coefficients[half] * *newest;
          }
          int32_t pcm = ((filtered >> 14) * scale + 32768) >> 16;
          pcm = pcm < -127 ? -127 : pcm > 127 ? 127 : pcm;
          output = static_cast<uint8_t>(pcm);
        }
        appendFrame(priorIndex, output, readFrame, writeFrame, fill, crc, stats);
      }
    }

    if (readFrame != writeFrame && ((USS(0) >> USTXC) & 0xff) < 120) {
      USF(0) = frames[readFrame][send++];
      if (send == frameSize) {
        send = 0;
        readFrame = (readFrame + 1) & (frameCount - 1);
        stats.sent += payloadSize;
      }
    }

    if (++watchdog == 4096) {
      ESP.wdtFeed();
      watchdog = 0;
    }

    while (!(readRegister(iqCtrl) & iqDone) && cycles() - now < period * 3) {}
    const bool acquired = readRegister(iqCtrl) & iqDone;
    if (acquired) {
      // The sums include six fractional bits from the estimator. Keep four
      // fractional bits after averaging 32 ADC samples.
      i = static_cast<int32_t>(readRegister(iqSumI)) >> 7;
      q = static_cast<int32_t>(readRegister(iqSumQ)) >> 7;
      i = i < -15000 ? -15000 : i > 15000 ? 15000 : i;
      q = q < -15000 ? -15000 : q > 15000 ? 15000 : q;
    } else {
      ++stats.timeouts;
    }
    writeRegister(iqCtrl, estimator);
    haveSlot = true;
    previous = acquired;
    previousOutput = ++phase == decimation;
    if (previousOutput) {
      phase = 0;
      priorIndex = sampleIndex++;
    }
    deadline += period;
    fractional += remainder;
    if (fractional >= cadence) {
      fractional -= cadence;
      ++deadline;
    }
  }

  writeRegister(iqCtrl, (readRegister(iqCtrl) & iqMask & ~1u) | 0x1000u);
  // Complete all queued frames before the ASCII end report.
  const uint32_t flushStart = cycles();
  while (readFrame != writeFrame && cycles() - flushStart < cpuHz / 20) {
    if (((USS(0) >> USTXC) & 0xff) < 120) {
      USF(0) = frames[readFrame][send++];
      if (send == frameSize) {
        send = 0;
        readFrame = (readFrame + 1) & (frameCount - 1);
        stats.sent += payloadSize;
      }
    }
  }
  stats.samples = sampleIndex;
  stats.captured = sampleIndex * decimation + phase - stats.skipped - stats.timeouts;
  stats.maxLate = maxLate;
}

static bool phyReady = false;

static void command(char* line) {
  char* argument = strtok(line, " \t");
  if (!argument) return;
  if (strcmp(argument, "STATUS") == 0) {
    Serial.printf("OK STATUS FMStereoRX 1 %u %u %u\n", ESP.getCpuFreqMHz(), baud, deviationFullScaleHz);
    return;
  }
  if (strcmp(argument, "STOP") == 0) {
    Serial.println("OK STOP");
    return;
  }
  if (strcmp(argument, "START") != 0) {
    Serial.println("ERR COMMAND");
    return;
  }

  char* frequency = strtok(nullptr, " \t");
  char* mode = strtok(nullptr, " \t");
  char* end = nullptr;
  unsigned long khz = frequency ? strtoul(frequency, &end, 10) : 0;
  if (!frequency || *end || khz < 2400000 || khz > 2483500 || strtok(nullptr, " \t")) {
    Serial.println("ERR FREQUENCY");
    return;
  }
  const Profile* profile = nullptr;
  for (const auto& entry : profiles) {
    if (strcmp(mode ? mode : "HQ", entry.name) == 0) profile = &entry;
  }
  if (!profile) {
    Serial.println("ERR MODE");
    return;
  }
  if (ESP.getCpuFreqMHz() != 160) {
    Serial.println("ERR CPU160");
    return;
  }
  if (!phyReady) {
    Serial.println("ERR PHY");
    return;
  }
  const uint32_t lo = tune(khz);
  if (!lo) {
    Serial.println("ERR TUNE");
    return;
  }

  Stats stats = {};
  Serial.printf("OK START %lu %lu %s\n", static_cast<unsigned long>(lo),
                static_cast<unsigned long>(profile->audioRate), profile->name);
  Serial.flush();
  const uint32_t processorStatus = xt_rsil(15);
  runReceiver(*profile, stats);
  xt_wsr_ps(processorStatus);
  Serial.flush();
  delay(10);
  while (Serial.available()) Serial.read();
  Serial.printf("\nEND samples=%lu sent=%lu iq=%lu skipped_iq=%lu timeouts=%lu invalid=%lu queue_drops=%lu max_late=%lu\n",
                static_cast<unsigned long>(stats.samples), static_cast<unsigned long>(stats.sent),
                static_cast<unsigned long>(stats.captured), static_cast<unsigned long>(stats.skipped),
                static_cast<unsigned long>(stats.timeouts), static_cast<unsigned long>(stats.invalid),
                static_cast<unsigned long>(stats.queueDrops),
                static_cast<unsigned long>(stats.maxLate));
}
}  // namespace

void setup() {
  Serial.begin(baud, SERIAL_8N2);
  Serial.setDebugOutput(false);
  prepareTables();
  phyReady = startPhy();
}

void loop() {
  static char line[48];
  static uint8_t length = 0;
  static bool overflow = false;
  while (Serial.available()) {
    const char c = Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      if (overflow) {
        Serial.println("ERR COMMAND");
      } else {
        line[length] = '\0';
        command(line);
      }
      length = 0;
      overflow = false;
    } else if (length < sizeof(line) - 1) {
      line[length++] = c;
    } else {
      overflow = true;
    }
  }
}

