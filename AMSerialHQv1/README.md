# AMSerialHQv1

Transmits a mono AM audio clip using the ESP's radio. The browser decodes a local audio file, downmixes it to mono, and converts it to amplitude samples for the firmware. Audio travels over USB serial at **2,000,000 baud, 8N1**. The radio operates without joining a Wi-Fi network.

| Board | Firmware | Browser player | RF bands |
| --- | --- | --- | --- |
| ESP8266 | [B2_AM_LIVE_SERIAL_HIFI_OV2.ino](B2_AM_LIVE_SERIAL_HIFI_OV2.ino) | [AM_LIVE_WEBSERIAL_HIFI.html](AM_LIVE_WEBSERIAL_HIFI.html) | 2.4 GHz |
| ESP32-C5 | [C5 port and setup](ESP32-C5v1/README.md) | [C5 AM player](ESP32-C5v1/AM_LIVE_WEBSERIAL_HIFI.html) | 2.4 and 5 GHz |

## ESP8266 quick start

1. Use Arduino IDE with **Arduino ESP8266 3.1.2**, select your board, and set its CPU to **160 MHz**. Open the sketch in a folder named `B2_AM_LIVE_SERIAL_HIFI_OV2`. Use a USB-to-UART bridge that sustains the runtime baud rate and close Serial Monitor after uploading. [ESP8266 Arduino installation](https://arduino-esp8266.readthedocs.io/en/3.1.2/installing.html)
2. Serve the repository locally as described in the [Web Serial setup](../README.md) and open this project's player in desktop Chrome or Edge. Click **Connecter le port** and select the bridge.
3. Load an audio file, select a short clip, and click **Préencoder l’extrait**. Start with **48 kSa/s**, **60% AM depth**, and the **14 kHz** low-pass target. Click **Diffuser vers ESP8266**.
4. Set the receiving SDR to AM near **2442.312500 MHz**. Locate the actual carrier on the waterfall. The nominal CW marker is **2442.625000 MHz**, with a one-second warm-up before each clip.

> [!IMPORTANT]
> The ESP8266 sketch starts CW as soon as the PHY is ready and keeps it running between clips. Use **ARRÊT RF** or remove power to stop it. Prepare the RF test setup before powering or resetting the board. See the [repository RF test notes](../README.md).

## Serial format

- START uses `0xA7`, then a little-endian 32-bit sample count, a rate index (0 for 32, 1 for 48, 2 for 64 kSa/s), and an APWR byte (0–191).
- After status 239, stream one unsigned little-endian Q8.8 ASK value per sample. The firmware updates the RF amplitude twice per sample. Status 235 pauses the host and 236 resumes it.
- APWR is a device control code. The player estimates modulation depth from its envelope mapping. Verify the actual depth on your setup if it matters to the experiment.

An earlier ESP8266/HackRF bench run at 48 kSa/s and APWR 63 measured **−21.35 dB THD+N** at 997 Hz after AM envelope detection. A separate 10-second clip transferred **960,000 bytes** with zero reported serial errors. See the [C5 port](ESP32-C5v1/README.md) for dual-band measurements.

**Related project** · [AM Receiver HQ](../AM-RX-YEAHHHHHv1/README.md) for direct reception on an ESP8266 or ESP32-C5.
