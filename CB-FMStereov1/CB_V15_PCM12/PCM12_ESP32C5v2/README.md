# ESP32-C5 PCM12 V2 — Dual-carrier FM stereo at ±75 kHz

This variant of the V15 PCM12 firmware transmits the same FM stereo signal through both ESP32-C5 numerically controlled oscillators (NCOs). It works with the V15 universal HTML interface over a 1.5 Mbaud UART connection.

## Dual-carrier transmission

Both NCOs use the same center frequency, the same FM modulation and the same programmed gain. Their outputs overlap on the same RF channel and carry one stereo program. The received RF level depends on how the two signals add in phase.

## FM deviation

The nominal peak TX deviation is **±75 kHz per NCO** at full-scale multiplex, up from ±50 kHz in the original project. This setting applies to PCM12 and legacy MPX transmission.

The stereo multiplex still runs at **120 kSa/s**, with a **19 kHz stereo pilot**, a **38 kHz stereo difference subcarrier** and **57 kHz RDS**. The higher deviation increases the contribution of each multiplex component by the same factor. The existing C5 receiver maps ±75 kHz to full scale. To check for clipping during reception, send `DIAG` and look for `pcm_clipped` in the `OK DIAG RXRF` line.

## Build and upload

Open `CB_FM_STEREO_PCM12_C5/CB_FM_STEREO_PCM12_C5.ino` with Arduino-ESP32 **3.3.12**. Keep `src/` and `build_opt.h` beside the sketch. Select **ESP32C5 Dev Module**, **240 MHz** and **USB CDC on boot disabled**. Match the flash size and partition layout to your board. The V15 reference configuration uses **4 MB DIO flash at 80 MHz** with the default partition layout.

Use the board's USB-to-UART bridge. The reference UART0 pins are GPIO11 for TX and GPIO12 for RX.

## Verification

The source review checked that the ±75 kHz calculation fits in signed 32-bit arithmetic and that the modulator stays within the NCO table. This revision has not been compiled, flashed or RF-tested.
