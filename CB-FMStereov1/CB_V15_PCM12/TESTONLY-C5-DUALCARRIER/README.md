<img width="1074" height="717" alt="Capture d’écran 2026-11-22 185144" src="https://github.com/user-attachments/assets/6e34ebde-4ee8-4fa6-952b-1a46d8eed6ed" />
Carrier 1: ±50 kHz peak deviation
Carrier 2: ±100 kHz peak deviation

# V15 PCM12 dual-carrier test for ESP32-C5

Run this 2.4 or 5 GHz firmware with the [V15 universal HTML](../CB_FM_STEREO-Universal.html). The shared UART is **1.5 Mbaud, 8N1**. The ESP receives **12-bit stereo PCM at 36 kSa/s per channel**, builds the **120 kSa/s** stereo multiplex, and returns **12-bit stereo PCM at 40 kSa/s per channel** plus RDS baseband during reception.

Press PTT in the universal page to transmit the same MPX, including the stereo pilot and RDS, on both C5 oscillators simultaneously. The second centre is **781,25 kHz above the first**. Both oscillators follow the same modulation update. This applies to PCM12 and legacy MPX transmission.

The offset is six 39,062.5 Hz NCO steps. At nominal full deviation (±50 kHz) and a 60 kHz MPX edge, Carson's rule estimates about 220 kHz per signal. The five-step offset is below that estimate. Six steps give a nominal 14,375 Hz gap between those estimated bandwidths. Measure adjacent-channel leakage and intermodulation on the RF bench to confirm separation.

Both oscillators use the same power setting with up to 6 dB of extra digital attenuation per carrier, limited by the existing NCO attenuation range. The first carrier retains the V15 LO rounding to the nearest kHz. The reported TX upper limits leave room for the second centre in each band. In RX, the normal V15 tuning range applies.

## Build and upload

Copy this folder as `CB_FM_STEREO_PCM12_C5` so its name matches [CB_FM_STEREO_PCM12_C5.ino](CB_FM_STEREO_PCM12_C5.ino). Keep the entire `src/` directory and `build_opt.h` in that copy. Open the sketch with **Arduino-ESP32 3.3.12**. Select **ESP32C5 Dev Module**, **240 MHz**, matching DIO flash size and partition layout, USB CDC on boot disabled. Use the USB-to-UART bridge. UART0 is GPIO11 TX and GPIO12 RX on the reference build. The V15 reference build uses **4 MB DIO flash at 80 MHz**, the default partition map and PSRAM disabled.

Compile from the renamed sketch folder, then upload to the detected board port:

```sh
arduino-cli compile --fqbn "esp32:esp32:esp32c5:CPUFreq=240,FlashMode=dio,FlashFreq=80,FlashSize=4M,PartitionScheme=default,PSRAM=disabled,CDCOnBoot=default" .
```

## Original V15 single-carrier measurements

The C5 passed PCM12 TX and RX in both bands, with one band active at a time. The firmware reports tuning limits of **2,401–2,482.5 MHz** and **5,180–5,885 MHz**. The controlled 5,745 MHz test gave **165.9 Hz RMS** residual, down from **191.5 Hz RMS** for the recompiled reference with the previous modulation timing. Crowded 2.4 GHz captures were too variable for the same comparison.

See the [original V15 test conditions and board comparison](https://github.com/kaboom748/ESP-AnalogRadio/tree/main/CB-FMStereov1/CB_V15_PCM12#radio-bench-results). RF measurements for the two-carrier variant are pending.
