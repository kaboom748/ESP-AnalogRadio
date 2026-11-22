# CB Duplex Voice V6 browser app

Open [index.html](index.html) or the [self-contained HTML](CB_DuplexVoice_V6_STANDALONE.html) in desktop Chrome or Edge. The V6 app works with this release's [ESP8266 sketch](../CB_DuplexVoice_V6_Arduino/) and [ESP32-C5 sketch](../CB_DuplexVoice_V6_C5/). Each uses **921,600 baud, 8N2**. If a browser blocks the local audio worklet, serve this directory from `http://localhost`.

Use the same RF frequency, TDD timing and on-air companding on all radios. At the default **20/8**, each voice is carried at 30 ksample/s. Passive reception forwards both full-rate PCM slot streams in DPX1 frames over the UART and the browser plays their 30 ksample/s mix. The Music / Raw profile still band-limits audio to around 5 kHz. The UART capacity check includes framing, pacing and a 4% reserve.

**PORT** connects a station in passive mode. **ANSWER** starts the Master, **CALL** joins as the Slave and **HANG UP** returns to passive listening. The local TDD filter can be changed independently on each station. See the [V6 guide](../) for the starting settings.

[Software test notes](V6_TEST_RESULTS.txt) cover the original V6 browser, DSP and serial simulation. [V4 test notes](V4_TEST_RESULTS.txt) cover the earlier release. Watch UART errors, dropped receive samples and DPX1 frame errors when validating a physical board and serial bridge.
