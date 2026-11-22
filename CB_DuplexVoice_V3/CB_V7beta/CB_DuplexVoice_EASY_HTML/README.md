# CB Duplex Voice V7 beta browser app

Open [index.html](index.html) or the [standalone page](CB_DuplexVoice_V6_STANDALONE.html) in desktop Chrome or Edge. The standalone page keeps the V6 filename and contains the beta interface. Keep the companion files with `index.html`, or use the standalone file when distributing a single page. If local worklet loading is blocked, use `http://localhost`.

Use the [V7 beta firmware](../) for the connected ESP8266, ESP32 Classic, ESP32-C5 or ESP32-C6. Select its external USB–UART port. The app uses **921,600 baud, 8N2** and reads DPX1 PCM8 audio, DPT1 telemetry and a firmware hardware ID.

The EASY panel handles **PORT**, **ANSWER**, **CALL**, **HANG UP**, DTMF and audio devices. The **Current interface** tab exposes timing, carrier tuning, gain, live IQ and UART statistics, and test-tone CSV/WAV exports. IQ and audio readings are relative to full scale. The signal bars describe link synchronization quality. Use matching channel, timing and on-air companding on all stations. Follow the [V7 beta setup](../) for the default 44/32 timing and channel.
