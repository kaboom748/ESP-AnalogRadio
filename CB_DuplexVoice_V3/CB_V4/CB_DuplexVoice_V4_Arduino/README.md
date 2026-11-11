# CB Duplex Voice V4 — Arduino firmware

The RF timing, PCM8 pulse generation, DPX1 packet framing, marker timing, APWR range checks (0..191), and role logic are **identical to V3**. Only the filename/folder is adjusted for Arduino IDE.

All amplitude companding and TDD-notch processing for V4 are on the **browser HTML/JavaScript** side. If V3 firmware is already working on your ESP8266 boards, **you do not need to reflash**.

If you do flash the sketch, use the original ESP8266 configuration at 160 MHz, USB serial 460800 baud 8N2. V4 has not been RF-tested or compiled against a physical board in this environment.

This is an experimental, uncalibrated transmission system. Use legal, attenuated or shielded RF testing arrangements and confirm applicable 2.4 GHz radio rules.
