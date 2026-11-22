# V15 PCM12 for ESP32-C5

Run this 2.4 or 5 GHz firmware with the [V15 universal HTML](../CB_FM_STEREO-Universal.html). The shared UART is **1.5 Mbaud, 8N1**. The ESP receives **12-bit stereo PCM at 36 kSa/s per channel**, builds the **120 kSa/s** stereo multiplex, and returns **12-bit stereo PCM at 40 kSa/s per channel** plus RDS baseband during reception.

## Build and upload

Copy this folder as `CB_FM_STEREO_PCM12_C5` so its name matches [CB_FM_STEREO_PCM12_C5.ino](CB_FM_STEREO_PCM12_C5.ino). Keep the entire `src/` directory and `build_opt.h` in that copy. Open the sketch with **Arduino-ESP32 3.3.12**. Select **ESP32C5 Dev Module**, **240 MHz**, matching DIO flash size and partition layout, USB CDC on boot disabled. Use the USB-to-UART bridge. UART0 is GPIO11 TX and GPIO12 RX on the reference build. The V15 reference build uses **4 MB DIO flash at 80 MHz**, the default partition map and PSRAM disabled.

Compile from the renamed sketch folder, then upload to the detected board port:

```sh
arduino-cli compile --fqbn "esp32:esp32:esp32c5:CPUFreq=240,FlashMode=dio,FlashFreq=80,FlashSize=4M,PartitionScheme=default,PSRAM=disabled,CDCOnBoot=default" .
```

## What was measured

The C5 passed PCM12 TX and RX in both bands, with one band active at a time. The firmware reports tuning limits of **2,401–2,482.5 MHz** and **5,180–5,885 MHz**. The controlled 5,745 MHz test gave **165.9 Hz RMS** residual, down from **191.5 Hz RMS** for the recompiled reference with the previous modulation timing. Crowded 2.4 GHz captures were too variable for the same comparison.

See the [V15 test conditions and board comparison](../README.md#radio-bench-results).
