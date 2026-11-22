# V15 PCM12 for ESP32 Classic

Run this 2.4 GHz firmware with the [V15 universal HTML](../CB_FM_STEREO-Universal.html). The shared UART is **1.5 Mbaud, 8N1**. The ESP receives **12-bit stereo PCM at 36 kSa/s per channel**, builds the **120 kSa/s** stereo multiplex, and returns **12-bit stereo PCM at 40 kSa/s per channel** plus RDS baseband during reception.

## Build and upload

Copy this folder as `CB_FM_STEREO_PCM12_ESP32` so its name matches [CB_FM_STEREO_PCM12_ESP32.ino](CB_FM_STEREO_PCM12_ESP32.ino). Keep the entire `src/` directory and `build_opt.h` in that copy. Open the sketch with **Arduino-ESP32 3.3.12**. Select **ESP32 Dev Module**, **240 MHz**, matching flash and partitions. The reference ESP32-D0WDQ6 board uses 4 MB of DIO flash at 40 MHz and a USB-to-UART bridge.

Compile from the renamed sketch folder, then upload to the detected board port:

```sh
arduino-cli compile --fqbn "esp32:esp32:esp32:CPUFreq=240,FlashMode=dio,FlashFreq=40" .
```

## What was measured

The dual-core LX6 implementation separates the multiplex preparation from serial/RF scheduling. On the 2.4 GHz bench it delivered **106.7–109.0 Hz RMS** residual and exchanged stereo with the C5. Continuous receive produced 6,666 consecutive frames in a 20-second run.

See the [V15 test conditions and board comparison](../README.md#radio-bench-results).
