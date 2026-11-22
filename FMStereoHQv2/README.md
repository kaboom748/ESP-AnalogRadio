# FMStereoHQv2 — selectable-channel stereo FM transmitter

Version 2 adds three 2.4 GHz presets to the original ESP8266 stereo file transmitter. Pair the firmware and HTML from this directory so the browser can read and set the channel.

- [Arduino firmware](B2_FM_LIVE_SERIAL_STEREO_120K_MPX8.ino)
- [Web Serial player](FM_LIVE_WEBSERIAL_STEREO.html)

| Preset | Nominal programme frequency |
| --- | ---: |
| CH 1 | 2412.3125 MHz |
| CH 7 (default) | 2442.3125 MHz |
| CH 11 | 2462.3125 MHz |

The transport remains **2,000,000 baud, 8N1** and **120 kSa/s signed MPX8**. The browser creates the stereo multiplex with a 19 kHz pilot and the ESP drives the RF modulation. Tune to the actual carrier measured at your receiver. The idle CW marker is above the programme centre.

## Run a clip

Build for ESP8266 at 160 MHz with Arduino ESP8266 core 3.1.2. Keep the sketch in a same-named directory. Open the matching local HTML in a desktop Web Serial browser, connect the ESP, select a channel, then trim and pre-encode an audio file. Use a 2.4 GHz-capable SDR with stereo WFM decoding or the [standalone stereo receiver](../FMStereoRX-YEAHHHHHv1/README.md). Use **ARRÊT RF** to stop RF. The board starts CW when powered and returns to it between clips.

[Version 1](../FMStereoHQv1/README.md) uses a fixed RF channel. [Version 3](../FMStereoHQv3/README.md) retains the V2 firmware and adds a browser encoder for RDS.
