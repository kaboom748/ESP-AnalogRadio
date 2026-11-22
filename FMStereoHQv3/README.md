# FMStereoHQv3 — stereo FM transmitter with optional RDS

Version 3 adds an optional **57 kHz RDS subcarrier** to the ESP8266 stereo file transmitter. The HTML encodes the RDS groups and audio into the same multiplex. The sketch in this folder matches the V2 radio firmware.

- [Arduino firmware](B2_FM_LIVE_SERIAL_STEREO_120K_MPX8.ino)
- [RDS-capable Web Serial player](FM_LIVE_WEBSERIAL_STEREO_RDS.html)

## Setup

1. Build the sketch for an ESP8266 at 160 MHz with Arduino ESP8266 core 3.1.2. Place the sketch in a folder matching its filename stem.
2. Open the local player in desktop Chrome or a Web Serial-capable Chromium browser. Connect the port at **2,000,000 baud, 8N1**. You can choose CH 1, 7 or 11, with nominal programme centres of **2412.3125, 2442.3125 or 2462.3125 MHz**.
3. Load a stereo file and enable **Ajouter le RDS au multiplex** to transmit metadata. Enter the short station name (PS), programme identifier (PI), programme type (PTY) and RadioText as needed, then pre-encode the clip. The serial stream is signed MPX8 at **120 kSa/s**.
4. For an RDS check, use an SDR with RDS decoding or the [standalone ESP8266 receiver in HQ mode](../FMStereoRX-YEAHHHHHv1/README.md). Use a clip long enough for the metadata groups to repeat. Keep the receive passband wide enough to pass the RDS subcarrier.
5. Select **ARRÊT RF** after transmitting. The transmitter starts CW at boot and between clips.

Use [V2](../FMStereoHQv2/README.md) for the older player or [C.B. FM Stereo V15](../CB-FMStereov1/CB_V15_PCM12/README.md) for live push-to-talk.
