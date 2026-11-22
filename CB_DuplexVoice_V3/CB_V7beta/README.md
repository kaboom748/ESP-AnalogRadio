# CB Duplex Voice V7 beta

This release brings one EASY browser app to all four ESP targets. The Arduino folders still use V6 names because the audio transport remains signed PCM8 over DPX1 at **921,600 baud, 8N2**. Choose a sketch from this **V7beta directory** to get the hardware identification and live telemetry used by its browser app.

| Target | Firmware | Arduino configuration | Browser UART0 |
|---|---|---|---|
| ESP8266 | [CB_DuplexVoice_V6_ESP8266.ino](CB_DuplexVoice_V6_ESP8266/CB_DuplexVoice_V6_ESP8266.ino) | Your ESP8266 board, 160 MHz | Board's USB–UART bridge |
| ESP32 Classic | [CB_DuplexVoice_V6_ESP32.ino](CB_DuplexVoice_V6_ESP32/CB_DuplexVoice_V6_ESP32.ino) | ESP32 Dev Module, Arduino-ESP32 3.3.12, 240 MHz, DIO flash at 40 MHz | TX GPIO1 / RX GPIO3 |
| ESP32-C5 | [CB_DuplexVoice_V6_C5.ino](CB_DuplexVoice_V6_C5/CB_DuplexVoice_V6_C5.ino) | ESP32C5 Dev Module, Arduino-ESP32 3.3.12, 240 MHz, USB CDC On Boot disabled | TX GPIO11 / RX GPIO12 |
| ESP32-C6 | [CB_DuplexVoice_V6_C6.ino](CB_DuplexVoice_V6_C6/CB_DuplexVoice_V6_C6.ino) | ESP32C6 Dev Module, Arduino-ESP32 3.3.12, fixed 160 MHz | TX GPIO16 / RX GPIO17 |

The RF link uses the board's radio on **2.4 GHz**. The accepted tuning range in this release is 2,400,000–2,483,500 kHz for every target. For the ESP32 variants, connect the browser to the USB–UART bridge wired to the pins listed above. Close any Arduino Serial Monitor using that port.

## First call

1. Flash the matching V7 beta sketch on each station, then open the [V7 beta browser app](CB_DuplexVoice_EASY_HTML/) in desktop Chrome or Edge.
2. Start with channel **07 (2,442,312 kHz)**, the **Cross-platform 44/32** timing preset and **Medium µ-law** on all stations. Leave automatic carrier tuning enabled for the initial test. The 44/32 preset carries 30,000 logical samples per second per voice and each station transmits about 682 bursts per second.
3. Click **PORT** on all stations. Click **ANSWER** on the Master and **CALL** on the second station after detection. Use **HANG UP** to return to passive listening.

In **Current interface → Live calibration**, check board identification, confirmed slots, receive IQ headroom and UART errors. The beta adds DPT1 telemetry alongside the DPX1 audio. The earlier V6 software test notes shipped here describe their original V6 scope.
