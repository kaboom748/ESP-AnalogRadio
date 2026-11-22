# SSBTestv1

Transmits a mono audio clip as **upper sideband (USB)** or **lower sideband (LSB)**. The browser encodes the signal as an amplitude and a phase increment per sample, then streams it over USB serial at **2,000,000 baud, 8N1**.

| Board | Firmware | Browser player | RF bands |
| --- | --- | --- | --- |
| ESP8266 | [B2_SSB_LIVE_SERIAL_HIFI.ino](B2_SSB_LIVE_SERIAL_HIFI.ino) | [SSB_LIVE_WEBSERIAL_HIFI.html](SSB_LIVE_WEBSERIAL_HIFI.html) | 2.4 GHz |
| ESP32-C5 | [C5 port and setup](ESP32-C5v1/README.md) | [C5 SSB player](ESP32-C5v1/SSB_LIVE_WEBSERIAL_HIFI.html) | 2.4 and 5 GHz |

## ESP8266 quick start

1. Use Arduino IDE with **Arduino ESP8266 3.1.2**, select your board, and set the CPU to **160 MHz**. Open the sketch in a folder named `B2_SSB_LIVE_SERIAL_HIFI`, upload it, and close Serial Monitor. [ESP8266 Arduino installation](https://arduino-esp8266.readthedocs.io/en/3.1.2/installing.html)
2. Serve the repository following the [Web Serial setup](../README.md) and open the ESP8266 player in desktop Chrome or Edge. Click **Connecter le port** and select the USB-to-UART bridge.
3. Select **USB** or **LSB**, then load an audio file or choose a built-in tone, two-tone, sweep, or silence test. Use **200 Hz–2.8 kHz** for a first speech test. The **12 kHz** and **14 kHz** audio profiles require **32 kSa/s** and a suitable receiver passband.
4. Click **Préencoder l’extrait** and **Diffuser vers ESP8266**. Tune the receiver to the suppressed-carrier reference and select the same sideband. The nominal reference is **2442.312500 MHz**. The **2442.390625 MHz** CW marker sits **78.125 kHz above** it. Tune against a known audio tone to account for clock offset.

> [!IMPORTANT]
> The ESP8266 starts CW at boot and keeps it running between clips. The player defaults to a **60-second** CW warm-up before every clip, adjustable from 1 to 900 seconds. To stop RF, use **ARRÊT RF** or remove power. Prepare the RF test setup before powering or resetting the board. See the [repository RF test notes](../README.md).

## Serial format

Send `0xA8` with a little-endian 32-bit sample count, a sample-rate index (0 for 24, 1 for 32 kSa/s), APWR, the sideband (0 for USB, 1 for LSB), and a little-endian 16-bit warm-up duration in seconds. After status 239, send four bytes per sample. The first word is unsigned Q8.8 amplitude and the second is signed Q16 frequency increment, both little-endian. Flow control uses status 235 for pause and 236 for resume.

An earlier ESP8266/HackRF two-tone USB test at 32 kSa/s measured **15.00 dB tone SINAD** after phase tracking and **−24.11 dBc integrated opposite sideband**. A separate 30-second LSB transport test at the same rate reported no transport errors.

**Related projects** · [SSB Receiver HQ for ESP8266](../SSB-RX-YEAHHHHHv1/README.md) · [C5 SSB transmitter](ESP32-C5v1/README.md)
