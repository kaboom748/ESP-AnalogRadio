# CB Duplex Voice V6 firmware for ESP8266

Open [CB_DuplexVoice_V6_Arduino.ino](CB_DuplexVoice_V6_Arduino.ino) in Arduino IDE. Select your ESP8266 board and set the CPU to **160 MHz**. Flash each ESP8266 that will use the [V6 browser app](../CB_DuplexVoice_EASY_HTML/), including passive listeners.

The app opens the USB serial bridge at **921,600 baud, 8N2** and expects `DUPLEX 1 921600 1` from a ready radio. This firmware works at 2.4 GHz.

## What changed from V5

- `START L` forwards all samples from both RF time slots. The browser demultiplexes and mixes the two voices.
- Missing slots are filled to preserve the sample index. The receive packet queue has 16 frames, up from eight.
- The UART capacity calculation accounts for two passive streams, DPX1 framing, paced transmission and a 4% reserve.
- The active Master and Slave keep the same RF timing and logical PCM format. DPX1 remains 146 bytes per frame with a 128-byte audio payload.

The older 460,800-baud browser versions require their matching firmware. See the [V6 guide](../) for the settings shared across all stations.
