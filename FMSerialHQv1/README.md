# FMSerialHQv1 — mono wideband FM transmitter

This ESP8266 transmitter plays a local audio clip as analog wideband FM (WFM) around 2.4 GHz. The browser prepares the instantaneous frequency deviation and streams it over USB serial.

- [Arduino firmware](B2_FM_LIVE_SERIAL_48K_WFM_B2_K4_TUNE1450_UART_FLOW8K_2000000.ino)
- [Web Serial player](FM_LIVE_WEBSERIAL_WFM16_PROFIL_MESURE.html)

| Setting | Value |
| --- | --- |
| Reference build | ESP8266 at 160 MHz, Arduino ESP8266 core 3.1.2 |
| Serial link | 2,000,000 baud, 8N1 |
| Host payload | WFM2 at 48 kSa/s, signed 16-bit little-endian deviation at 4 Hz per count, 96 kB/s |
| Nominal frequencies | 2442.312500 MHz for audio and 2442.625000 MHz for idle CW |
| Nominal peak deviation | ±25 kHz (default), ±50 kHz or ±75 kHz |
| Pre-emphasis | Off (default), 50 µs or 75 µs |

## Run a clip

1. Put the sketch in a folder matching its filename stem, select your ESP8266 board and 160 MHz CPU option, and upload. Close the serial monitor.
2. Open the local HTML in desktop Chrome using the [repository setup instructions](../README.md#start-with-cb-fm-stereo-v15). Connect the serial port, select an audio file, trim it and pre-encode the selection. The player can preview the encoded deviation as audio.
3. Set a 2.4 GHz-capable SDR to WFM mono. Centre it on the measured programme frequency and match its de-emphasis to the encoder. Start with the default ±25 kHz deviation profile and 60% modulation setting.
4. Start streaming. The firmware starts idle CW at boot and returns to it after playback. Click **ARRÊT RF** in the connected page to stop RF, or remove power if the serial port is unavailable.

## Reference measurement

An earlier HackRF bench run at about 2442.2854 MHz measured **27.49 dB tone SNR**, **27.37 dB SINAD**, and **0.73% THD**. The 1 kHz fixture used the ±25 kHz profile, 60% level, and APWR 63. Recovered deviation was 7.51 kHz against 7.50 kHz expected. The HackRF captured at 4 MS/s with a ±55 kHz analysis IF. These measurements characterize that setup.

For file-based stereo, see [FMStereoHQv1](../FMStereoHQv1/README.md), [v2](../FMStereoHQv2/README.md), or [v3 with RDS](../FMStereoHQv3/README.md).
