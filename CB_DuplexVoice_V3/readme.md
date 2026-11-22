# CB Duplex Voice

CB Duplex Voice is a mono, two-way FM voice link built around one ESP radio per station. The Master and Slave alternate transmit and receive slots on a shared 2.4 GHz carrier. The browser buffers and reconstructs the audio so both people can speak hands-free. An optional third station can monitor both voices in passive mode.

The browser uses Web Serial for audio and control. Over the air, the radios exchange custom analog FM bursts with timing markers. This project has its own signed PCM8 transport and browser app. See [CB FM Stereo](../CB-FMStereov1/) for the separate stereo, RDS and push-to-talk project.

## Choose a version

| Release | Firmware targets | USB serial | When to use it |
|---|---|---|---|
| [V7 beta](CB_V7beta/) | ESP8266, ESP32 Classic, ESP32-C5, ESP32-C6 | 921,600 baud, 8N2 | Start here for a new installation or mixed-board testing. Adds board identification, live calibration and automatic carrier tuning. |
| [V6](CB_V6/) | ESP8266, ESP32-C5 | 921,600 baud, 8N2 | Earlier EASY app with full-rate passive reception. |
| [V5](CB_V5/) | ESP8266 | 460,800 baud, 8N2 | Original EASY front panel and detailed historical manual. |
| [V4](CB_V4/) | ESP8266 | 460,800 baud, 8N2 | Original advanced interface, companding and TDD notch filter. |

Use the browser app and Arduino sketch from the same release. Keep all stations on the same RF frequency, TDD timing and on-air amplitude encoding. **Every Duplex Voice version in this repository is 2.4 GHz only**, including the C5 builds.

## Make a call

1. In Arduino IDE, open the sketch from the chosen version and target-board folder. See that version's guide for the board menu, CPU clock and serial interface.
2. Open the matching HTML in desktop Chrome or Edge. If the browser blocks local audio worklets, serve the HTML directory on `http://localhost`. Grant microphone access and use headphones for the first call.
3. In the EASY app, select the same channel, timing and companding on all stations, then click **PORT** to connect each USB–UART bridge.
4. Click **ANSWER** on the Master. After the second station detects it, click **CALL** to join as the Slave. **HANG UP** returns a station to passive listening.

A session has one Master and one active Slave. Additional stations can listen. The **Current interface** tab provides audio and serial diagnostics. Use the repository's [radio comparison](../README.md) for the separate FM stereo bench results.
