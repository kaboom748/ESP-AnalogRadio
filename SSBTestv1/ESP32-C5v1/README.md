# SSBTestv1 for ESP32-C5

The C5 port transmits **USB and LSB on both 2.4 and 5 GHz**. The browser calculates the audio's polar representation, then the firmware reconstructs I/Q and updates the radio at 72 or 96 kSa/s from the corresponding 24 or 32 kSa/s serial stream.

## Build and transmit

1. Open [B2_SSB_LIVE_SERIAL_HIFI_C5.ino](B2_SSB_LIVE_SERIAL_HIFI_C5/B2_SSB_LIVE_SERIAL_HIFI_C5.ino) with **Arduino ESP32 3.3.12**. Select **ESP32C5 Dev Module**, **240 MHz**, **DIO 80 MHz**, PSRAM disabled, and **USB CDC On Boot disabled** for the USB-to-UART bridge. Set the flash size and partition scheme for your module. The documented bench used an **8 MB** flash layout.
2. Upload, close Serial Monitor, then open [SSB_LIVE_WEBSERIAL_HIFI.html](SSB_LIVE_WEBSERIAL_HIFI.html) in desktop Chrome or Edge using the [local Web Serial server](../../README.md).
3. Connect the bridge, choose the RF band and sideband, and select an audio clip or test source. Pre-encode the selection and transmit. The receiver should use the same sideband and tune near the nominal reference. A known tone helps adjust for oscillator offset.

| RF profile | Nominal suppressed-carrier reference | Nominal CW marker |
| --- | ---: | ---: |
| 2.4 GHz, channel 7 | 2442.312500 MHz | 2442.390625 MHz |
| 5 GHz, channel 36 | 5180.312500 MHz | 5180.390625 MHz |

The firmware boots with RF off on the 2.4 GHz profile. Switching profiles also turns RF off. Each clip runs after its configured 1–900 second warm-up. The firmware turns RF off at the end unless you activated continuous CW manually. See the [repository RF test notes](../../README.md).

## Transport and bench check

The port uses **2,000,000 baud, 8N1** and retains the [original four-byte polar stream](../README.md#serial-format). `0xAB 0x00` selects the 2.4 GHz profile and `0xAB 0x01` selects 5 GHz. APWR is an attenuation index on the C5. A larger code applies more attenuation.

The HackRF bench checked both sidebands in each band using 700 Hz and 1700 Hz tones at 32 kSa/s. The desired tones were spaced by approximately 1 kHz throughout the captures. Individual mirror-tone rejection ranged from **41.8 to 54.7 dB**. A separate 1 kHz check at 24 kSa/s gave **52.2 to 57.1 dB** of mirror rejection. The bench streamed audio encoded by the matching player using a serial test driver.

**Related projects** · [Original ESP8266 transmitter](../README.md) · [ESP8266 SSB receiver for 2.4 GHz](../../SSB-RX-YEAHHHHHv1/README.md)
