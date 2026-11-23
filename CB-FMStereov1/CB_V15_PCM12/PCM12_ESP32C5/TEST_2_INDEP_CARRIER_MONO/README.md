# V15 PCM12 dual-mono FM for ESP32-C5

The [V15 universal HTML](../CB_FM_STEREO-Universal.html) sends 12-bit left and right audio at 36 kSa/s per channel over a 1.5 Mbaud 8N1 UART. In PCM12 transmit mode, the C5 resamples each channel separately to 120 kSa/s and sends it as mono FM. Both carriers use the same selected RF band, either 2.4 GHz or 5 GHz.

| Carrier | Audio | Nominal centre | Peak deviation |
| --- | --- | --- | --- |
| 1 | Right | Selected frequency | ±100 kHz |
| 2 | Left | Selected frequency + 1.5625 MHz | ±100 kHz |

The spacing is exactly 40 fine NCO steps of 39,062.5 Hz. Each channel has its own pulse widths and phase-error feedback. The firmware maps the existing HTML's PCM12 peak ceiling to ±100 kHz and clips interpolation overshoot at that level. Actual deviation depends on audio level. The firmware ignores the RDS envelope bytes in PCM12 TX. Each carrier carries mono audio without a stereo pilot.

Use a stereo source with different audio in each channel, then press PTT. To hear each channel separately, tune a receiver to its respective centre and select wideband mono FM. The shared APWR setting and dual-tone attenuation follow the original two-carrier implementation, with up to 6 dB of digital backoff per carrier. TX limits reserve room for the upper centre and LO rounding.

Reception uses the original V15 PCM12 and MPX decoders. The legacy MPX TX command still sends the same input multiplex on both carriers with its original ±50/±100 kHz scaling and the new spacing.

## Build and upload

Open [CB_FM_STEREO_PCM12_C5.ino](CB_FM_STEREO_PCM12_C5.ino) with **Arduino-ESP32 3.3.12**. Keep the `src/` directory and `build_opt.h` in the sketch folder. Select **ESP32C5 Dev Module**, **240 MHz**, DIO flash and the partition layout matching the module. Disable USB CDC on boot and use the USB-to-UART bridge. UART0 uses GPIO11 TX and GPIO12 RX on the reference build.

Example compile command for a 4 MB module.

```sh
arduino-cli compile --fqbn "esp32:esp32:esp32c5:CPUFreq=240,FlashMode=dio,FlashFreq=80,FlashSize=4M,PartitionScheme=default,PSRAM=disabled,CDCOnBoot=default" .
```

This variant is based on the [V15 C5 two-carrier test](https://github.com/kaboom748/ESP-AnalogRadio/tree/2d071ab7fc549d0f06caf74d025bb1a5637d5ed9/CB-FMStereov1/CB_V15_PCM12/TESTONLY-C5-DUALCARRIER). Bench measurements for dual-mono transmission are pending.

This revision reserves a continuous window after both RF pulses for the PCM12 producer and UART. The firmware reads UART bytes in small bursts and gives due flow-control credits priority over interpolation when the UART FIFO has room.
