<img width="2880" height="2780" alt="téléchargement" src="https://github.com/user-attachments/assets/1de7ccf8-c4c1-461e-9847-af2a8e1d18a4" />
<img width="1536" height="1024" alt="Image ChatGPT 23 nov  2026, 15_38_18" src="https://github.com/user-attachments/assets/131f0755-a14a-451e-b935-923929555ae5" />
# CB FM Stereo V15 PCM12

CB_FM_STEREO-Universal-EQv2.html - EQUALIZER

<img width="523" height="629" alt="Capture d’écran 2026-11-23 160825" src="https://github.com/user-attachments/assets/95b09988-1da8-4667-9750-38a46c5bde54" />

Use the standalone [CB_FM_STEREO-Universal.html](CB_FM_STEREO-Universal.html) for all four board builds. It opens USB serial at **1,500,000 baud, 8N1**, checks for **CB_FM_STEREO protocol 4**, and negotiates the supported TX and RX formats separately. The same page uses MPX in both directions with compatible [V14](../CB_V14/README.md) or [V13](../CB_V13/README.md) firmware that does not offer PCM12. The 2 Mbaud historical releases use their own pages.

| Board | Firmware | TX over USB | RX over USB | RF bands |
| --- | --- | --- | --- | --- |
| ESP8266 | [Hybrid ESP8266](CB_FM_STEREO_MPXRX_PCM12TX_ESP8266/README.md) | PCM12 stereo | MPX3 | 2.4 GHz |
| ESP32 Classic | [Classic PCM12](PCM12_ESP32/README.md) | PCM12 stereo | PCM12 stereo | 2.4 GHz |
| ESP32-C6 | [C6 PCM12](PCM12_C6/README.md) | PCM12 stereo | PCM12 stereo | 2.4 GHz |
| ESP32-C5 | [C5 PCM12](PCM12_ESP32C5/README.md) | PCM12 stereo | PCM12 stereo | 2.4 or 5 GHz |

## Setup

Flash the matching sketch using the board instructions above. The repository contains the source for these builds. For Arduino IDE or Arduino CLI, copy the entire sketch directory if the table below shows a different Arduino folder name. Rename that copy to match the `.ino` basename. Keep `src/` and `build_opt.h` next to the sketch where supplied.

| Folder in this repository | Arduino sketch folder name |
| --- | --- |
| `CB_FM_STEREO_MPXRX_PCM12TX_ESP8266` | `CB_FM_STEREO_MPXRX_PCM12TX_ESP8266` |
| `PCM12_ESP32` | `CB_FM_STEREO_PCM12_ESP32` |
| `PCM12_C6` | `CB_FM_STEREO_PCM12_C6` |
| `PCM12_ESP32C5` | `CB_FM_STEREO_PCM12_C5` |

Open the universal page on a desktop browser with Web Serial. Close the serial monitor before selecting the board in **PORT**. Follow the [radio check](../README.md#first-radio-check) to connect your microphone, match the actual tuning frequency across both stations, and verify PTT in each direction.

## Audio and serial data

- **PCM12 TX** sends signed, packed 12-bit stereo PCM at **36 kSa/s per channel**. The firmware interpolates to **120 kSa/s**, builds the stereo multiplex and inserts the pilot and RDS. TX credits control the PCM frame pacing on the compatible firmware.
- **PCM12 RX** sends stereo PCM at **40 kSa/s per channel**, plus a low-rate RDS baseband stream for the browser. HQ, ROBUST, and MONO share the same PCM12 audio stream rate.
- **MPX RX** sends 8-bit multiplex in MPX3 frames at **120 kSa/s** in HQ, **96 kSa/s** in ROBUST, or **48 kSa/s** in MONO. The ESP8266 uses this path to leave stereo and RDS decoding to the browser.
- **Legacy MPX TX** sends the browser-generated signed 8-bit multiplex at **120 kSa/s** to the firmware's `TX` path. It is the fallback for compatible MPX-only firmware.

For a PCM session, the first check after an error such as `TX_PCM_QUEUE` is that both files are from the same compatible generation. Release PTT, close other applications using the same serial port, and repeat with the browser in the foreground. For a squelch check, set **SQ** to zero, confirm that received audio is present, then use the noise measurement or raise **SQ** until the idle channel mutes.

The TX mixer retains four fractional bits in its internal 16-bit multiplex representation. Those bits reduce rounding through interpolation and modulation. The USB source remains PCM12, and the RF noise floor sets the effective resolution of the link.

## Radio bench results

The values below measure the residual after FM demodulation in a **300 Hz–15 kHz** analysis band with a **1 kHz** test tone removed. They include noise and distortion from the board, the HackRF Pro receive chain, and local RF conditions. Lower is better for this test.

| Board | Measured RMS FM residual | What stood out |
| --- | ---: | --- |
| ESP8266 | **70.4 Hz** at 2,442.3125 MHz and **69.9 Hz** at 2,472.3125 MHz | Lowest measured 2.4 GHz residual. The hybrid design kept the browser responsible for RX decoding. Some sustained synthetic TX stress signals still ran out of samples. |
| ESP32-C6 | **87.5–90.3 Hz** at 2,442.3125 MHz and **88.2 Hz** at 2,472.3125 MHz | The lower-residual 2.4 GHz option with PCM12 in both directions. Its high-level TX test ran 30.3 seconds without an underrun. |
| ESP32 Classic | **106.7–109.0 Hz** at 2,442.3125 MHz | Uses both LX6 cores to share audio preparation and RF/serial work. Full PCM12 and two-way stereo were tested. |
| ESP32-C5 | **165.9 Hz** at **5,745 MHz** | The only dual-band board. Both bands were tested. The 5 GHz residual was measured separately after variable interference on the 2.4 GHz channel. |

The 2.4 GHz comparisons used `APWR 20`. The C5 5 GHz run used `APWR 63` and a different RF channel. The reported results describe the tested firmware and bench configuration. Receiver sensitivity and range would require calibrated, equal-condition measurements across the four boards.
