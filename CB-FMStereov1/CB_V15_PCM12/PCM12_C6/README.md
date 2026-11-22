# V15 PCM12 for ESP32-C6

Run this 2.4 GHz firmware with the [V15 universal HTML](../CB_FM_STEREO-Universal.html). The shared UART is **1.5 Mbaud, 8N1**. The ESP receives **12-bit stereo PCM at 36 kSa/s per channel**, builds the **120 kSa/s** stereo multiplex, and returns **12-bit stereo PCM at 40 kSa/s per channel** plus RDS baseband during reception.

## Build and upload

Copy this folder as `CB_FM_STEREO_PCM12_C6` so its name matches [CB_FM_STEREO_PCM12_C6.ino](CB_FM_STEREO_PCM12_C6.ino). Keep the entire `src/` directory and `build_opt.h` in that copy. Open the sketch with **Arduino-ESP32 3.3.12**. Select **ESP32C6 Dev Module**, **160 MHz**, with the flash size and partition layout matching the module. The reference board uses 4 MB flash and connects through its USB-to-UART bridge.

Compile from the renamed sketch folder, then upload to the detected board port:

```sh
arduino-cli compile --fqbn "esp32:esp32:esp32c6" .
```

## What was measured

The C6 delivered **87.5–90.3 Hz RMS** residual at 2,442.3125 MHz and **88.2 Hz RMS** at 2,472.3125 MHz. A 30.3-second high-level TX run had no RF deadline misses or active underruns. Two-way stereo was verified with the Classic.

See the [V15 test conditions and board comparison](../README.md#radio-bench-results).
