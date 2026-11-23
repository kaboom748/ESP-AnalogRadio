# V15 PCM12 dual-carrier test for ESP32 Classic

Run this 2.4 GHz firmware with the [V15 universal HTML](../CB_FM_STEREO-Universal.html). The shared UART is **1.5 Mbaud, 8N1**. The ESP receives **12-bit stereo PCM at 36 kSa/s per channel**, builds the **120 kSa/s** stereo multiplex, and returns **12-bit stereo PCM at 40 kSa/s per channel** plus RDS baseband during reception.

Press PTT to transmit the same stereo multiplex and RDS on two simultaneous carriers. The selected frequency is the first carrier's nominal centre. The second is **781,250 Hz (781.25 kHz)** above it. Nominal full-scale deviation is **±50 kHz** for the first carrier and **±100 kHz** for the second. This uses the offset and modulation ratio in the [C5 reference source](https://github.com/kaboom748/ESP-AnalogRadio/blob/9cf608ed4816dacd284e4c965ca2efd52179c801/CB-FMStereov1/CB_V15_PCM12/TESTONLY-C5-DUALCARRIER/src/c5_bands.h#L13).

Both NCOs follow the same modulation schedule. The second NCO doubles each frequency excursion around its own centre. The shared RF path applies up to 6 dB of extra digital attenuation per carrier to limit the combined peaks. The advertised upper TX limit leaves room for the second carrier centre. Build and RF verification of this variant are pending.

## Build and upload

Copy this folder as `CB_FM_STEREO_PCM12_ESP32` so its name matches [CB_FM_STEREO_PCM12_ESP32.ino](CB_FM_STEREO_PCM12_ESP32.ino). Keep the entire `src/` directory and `build_opt.h` in that copy. Open the sketch with **Arduino-ESP32 3.3.12**. Select **ESP32 Dev Module**, **240 MHz**, matching flash and partitions. The reference ESP32-D0WDQ6 board uses 4 MB of DIO flash at 40 MHz and a USB-to-UART bridge.

Compile from the renamed sketch folder, then upload to the detected board port:

```sh
arduino-cli compile --fqbn "esp32:esp32:esp32:CPUFreq=240,FlashMode=dio,FlashFreq=40" .
```

## Original single-carrier bench results

The dual-core LX6 implementation separates the multiplex preparation from serial/RF scheduling. On the 2.4 GHz bench it delivered **106.7–109.0 Hz RMS** residual and exchanged stereo with the C5. Continuous receive produced 6,666 consecutive frames in a 20-second run.

See the [original V15 test conditions and board comparison](https://github.com/kaboom748/ESP-AnalogRadio/blob/9cf608ed4816dacd284e4c965ca2efd52179c801/CB-FMStereov1/CB_V15_PCM12/README.md#radio-bench-results).
