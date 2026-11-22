# CB Duplex Voice V6

V6 increases the Web Serial link to **921,600 baud, 8N2** and forwards both RF slots at full rate in passive mode. It keeps the EASY front panel, DTMF, signed PCM8 and DPX1 framing.

| Board | Open in Arduino IDE | Settings |
|---|---|---|
| ESP8266 | [CB_DuplexVoice_V6_Arduino.ino](CB_DuplexVoice_V6_Arduino/CB_DuplexVoice_V6_Arduino.ino) | ESP8266 board matching your module, CPU at 160 MHz |
| ESP32-C5 | [CB_DuplexVoice_V6_C5.ino](CB_DuplexVoice_V6_C5/CB_DuplexVoice_V6_C5.ino) | ESP32C5 Dev Module, Arduino-ESP32 3.3.12, 240 MHz, USB CDC On Boot disabled, UART0 TX GPIO11 / RX GPIO12 |

Open the [V6 browser app](CB_DuplexVoice_EASY_HTML/) with either firmware. Match the frequency, TDD timing and companding on all stations. Start with the defaults of channel 07 (2,442,312 kHz), **20/8** and **Medium µ-law**. Connect with **PORT**, then use **ANSWER** on one station and **CALL** on the other. Use the 2.4 GHz band on both targets.

With the 20/8 preset, each voice has 30,000 logical samples per second. A passive listener receives both slot streams over USB–UART and mixes them into one 30 ksample/s playback stream. V5 used 460,800 baud and downsampled passive reception, so its browser app and firmware are a separate pair.

See the [original software test notes](CB_DuplexVoice_EASY_HTML/V6_TEST_RESULTS.txt) or the [French release notes](README_CB_V6_FR.md). Use [V7 beta](../CB_V7beta/) for ESP32 Classic or ESP32-C6.
