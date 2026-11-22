# FMStereoRX — standalone stereo FM receiver

This receive-only ESP8266 project demodulates analog FM in the **2.4 GHz** band. The ESP streams signed, framed MPX8 over USB serial. The browser recovers mono or stereo audio and can decode RDS from the HQ stream.

- [Arduino firmware](FMStereoRX.ino)
- [Receiver page](FM_STEREO_RXv3.html)

| Receive profile | MPX sample rate | Browser decode |
| --- | ---: | --- |
| MONO | 48 kSa/s | Mono audio |
| ROBUST | 96 kSa/s | Stereo with a reduced programme audio passband |
| HQ | 120 kSa/s | Full-band stereo and optional RDS/RBDS decoding |

## Start receiving

1. Use an ESP8266 at 160 MHz and Arduino ESP8266 core 3.1.2. Put `FMStereoRX.ino` in a folder named `FMStereoRX` and upload. Close the serial monitor.
2. Open the local HTML in a desktop browser with Web Serial. Select **Ouvrir le port** and choose the USB serial adapter. The checked-in firmware defaults to **1,500,000 baud, 8N2**.
3. Select the transmitter's preset or enter its measured frequency in MHz. Set the profile and match the de-emphasis to the transmitter. Select **Recevoir** and monitor pilot lock and transport counters.
4. For RDS, pair with the [V3 stereo file transmitter](../FMStereoHQv3/README.md) with RDS enabled, use HQ and select the desired RDS/RBDS display profile. The page also accepts a saved raw signed MPX8 file for offline playback at its original sample rate.

If stereo drops to mono, check the pilot and tuning. The page distinguishes missing serial frames from invalid RF acquisition samples. For a combined transmit and receive station, see [C.B. FM Stereo](../CB-FMStereov1/README.md).
