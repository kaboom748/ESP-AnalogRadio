/*
  C.B. FM stéréo pour deux ESP8266EX à 160 MHz, Arduino ESP8266 core 3.1.2.
  Base TX : ESP-AnalogRadio / FMStereoHQv3. Base RX : FMStereoRX.
  Le réglage PHY et l'estimateur I/Q réutilisent les travaux de SP8ESA.
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
#include "config.h"
#include "hardware_esp8266.h"
#include "uart.h"
#include "rf.h"
#include "fm_tx.h"
#include "iq_rx.h"
#include "radio_phy.h"
#include "tx_control.h"
#include "serial_commands.h"
}  // namespace

void setup() {
  WiFi.persistent(false);
  wifi_station_set_auto_connect(0);
  stopPhy();
  uartInit();
  prepareTables();
  prepareMpxLevels();
}

void loop() {
  if (transmitting) {
    static uint32_t lastWatchdogAt = 0;
    pollTx();
    const uint32_t now = millis();
    if (now - lastWatchdogAt >= 10) {
      ESP.wdtFeed();
      lastWatchdogAt = now;
    }
    return;
  }

  static char line[48];
  static uint8_t length = 0;
  static bool invalid = false;
  static uint32_t lastInputAt = 0;
  while (Serial.available() > 0) {
    const int byte = Serial.read();
    if (byte == cmdPtt && !length && !invalid) {
      length = 0;
      invalid = false;
      if (ESP.getCpuFreqMHz() == 160) beginTx(currentApwr);
      else Serial.println("ERR CPU160");
      return;
    }
    lastInputAt = millis();
    if (byte == '\r') continue;
    if (byte == '\n') {
      if (invalid) {
        Serial.println("ERR COMMAND");
      } else {
        line[length] = '\0';
        command(line);
      }
      length = 0;
      invalid = false;
      // TX a basculé l'UART en flux : ne plus lire Serial ni relancer listen().
      if (transmitting) return;
    } else if ((byte == '\t' || (byte >= 32 && byte <= 126)) && length < sizeof(line) - 1) {
      line[length++] = byte;
    } else {
      invalid = true;
    }
  }

  if (length || invalid) {
    if (millis() - lastInputAt < 120) return;
    length = 0;
    invalid = false;
  }
  if (receiveRequested) listen();
}
