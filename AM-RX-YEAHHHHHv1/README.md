# AM Receiver HQ

Receives AM on the ESP radio and sends demodulated **mono PCM16 at 48 kSa/s** to a browser. The player sends the same audio to both output channels. This project is the receiver for [AMSerialHQv1](../AMSerialHQv1/README.md).

| Board | Firmware | Browser player | RF bands | UART |
| --- | --- | --- | --- | --- |
| ESP8266 | [AMReceiverHQ.ino](AMReceiverHQ.ino) | [AM_RECEIVER_HQ.html](AM_RECEIVER_HQ.html) | 2.4 GHz | 1,500,000 baud, 8N2 |
| ESP32-C5 | [C5 port and setup](ESP32-C5v1/README.md) | [C5 AM receiver page](ESP32-C5v1/AM_RECEIVER_HQ.html) | 2.4 and 5 GHz | 2,000,000 baud, 8N1 |

Both builds send `AM01` audio frames. Use the matching HTML for each board's serial settings and gain controls.

## ESP8266 quick start

1. Install **Arduino ESP8266 3.1.2**, select your board, and set the CPU to **160 MHz**. Open the sketch in a folder named `AMReceiverHQ`, upload it, and close Serial Monitor. [ESP8266 Arduino installation](https://arduino-esp8266.readthedocs.io/en/3.1.2/installing.html)
2. Serve the repository following the [Web Serial setup](../README.md) and open the ESP8266 receiver page in desktop Chrome or Edge.
3. Click **Ouvrir le port**, enter the AM carrier in MHz, and click **Recevoir**. The page's **2442.287 MHz** default came from an earlier bench. Tune to your transmitter's actual signal. The firmware accepts tuning requests from **2400 to 2483.5 MHz**.
4. Under **Gain radio**, choose automatic gain or set the RF and VGA indices before starting. If the audio clips, lower gain and restart reception. Playback volume acts separately.

## Signal and frame format

The firmware measures I/Q at **96 kSa/s**, shifts the **24 kHz** low IF to baseband, filters the channel, and detects the AM envelope. It sends PCM audio in indexed frames with **64 little-endian signed samples** and a CRC16. The sample value `0x8000` marks an unavailable sample. `STATUS` identifies the device, `START <kHz> HQ` begins reception, and any serial byte stops an active capture.

The browser shows CRC errors, missing samples, and audio buffer statistics. For offline playback, it accepts **raw signed little-endian mono PCM16 at 48 kSa/s**. The AM demodulator runs on the ESP, so this page plays its PCM directly.
