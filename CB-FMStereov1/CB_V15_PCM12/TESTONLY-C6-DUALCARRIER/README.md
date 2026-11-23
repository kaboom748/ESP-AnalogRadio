# V15 PCM12 dual-carrier variant for ESP32-C6

Run this 2.4 GHz firmware with the [V15 universal HTML](../CB_FM_STEREO-Universal.html). The shared UART is **1.5 Mbaud, 8N1**. The ESP receives **12-bit stereo PCM at 36 kSa/s per channel**, builds the **120 kSa/s** stereo multiplex, and returns **12-bit stereo PCM at 40 kSa/s per channel** plus RDS baseband during reception.

Press PTT to transmit the same stereo multiplex and RDS on both carriers. The selected frequency is the first carrier's nominal centre. Its full-scale deviation is **±50 kHz**. The second carrier is **781,250 Hz (781.25 kHz)** higher and has **±100 kHz** nominal full-scale deviation. These settings follow the [C5 offset](https://github.com/kaboom748/ESP-AnalogRadio/blob/6974e276f3d2f67d8dbcd170363630ac79b5bae3/CB-FMStereov1/CB_V15_PCM12/TESTONLY-C5-DUALCARRIER/src/c5_bands.h#L13) and [modulation ratio](https://github.com/kaboom748/ESP-AnalogRadio/blob/6974e276f3d2f67d8dbcd170363630ac79b5bae3/CB-FMStereov1/CB_V15_PCM12/TESTONLY-C5-DUALCARRIER/src/c5_tx_radio.cpp#L165-L170).

The firmware precalculates the register values for each modulation step. Both NCOs follow the same pulse schedule, with more cycles reserved for the extra register write. Each NCO uses 6 dB of additional digital attenuation for combined peak headroom. The TX range reserves room for the second centre at the upper end. This variant has not been compiled or tested on hardware.

## Build and upload

Open [CB_FM_STEREO_PCM12_C6.ino](CB_FM_STEREO_PCM12_C6.ino) with **Arduino-ESP32 3.3.12**. Keep the sketch folder, its `src/` directory and `build_opt.h` together. Select **ESP32C6 Dev Module**, **160 MHz**, with the flash size and partition layout matching the module. The reference board uses 4 MB flash and connects through its USB-to-UART bridge.

Compile from the renamed sketch folder, then upload to the detected board port:

```sh
arduino-cli compile --fqbn "esp32:esp32:esp32c6" .
```

## Original single-carrier bench results

The C6 delivered **87.5–90.3 Hz RMS** residual at 2,442.3125 MHz and **88.2 Hz RMS** at 2,472.3125 MHz. A 30.3-second high-level TX run had no RF deadline misses or active underruns. Two-way stereo was verified with the Classic.

See the [original V15 test conditions and board comparison](https://github.com/kaboom748/ESP-AnalogRadio/blob/6974e276f3d2f67d8dbcd170363630ac79b5bae3/CB-FMStereov1/CB_V15_PCM12/README.md#radio-bench-results).
