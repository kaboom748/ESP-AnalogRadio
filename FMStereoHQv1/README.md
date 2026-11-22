# FMStereoHQv1 — fixed-channel stereo FM transmitter

The original ESP8266 stereo file player sends a browser-generated FM stereo multiplex over USB serial. Use it for fixed-channel reproduction or comparison with later builds.

- [Arduino firmware](B2_FM_LIVE_SERIAL_STEREO_120K_MPX8.ino)
- [Web Serial player](FM_LIVE_WEBSERIAL_STEREO.html)

The browser combines the left-plus-right baseband, a 19 kHz pilot, and left-minus-right audio on a suppressed 38 kHz subcarrier. It streams signed MPX8 at **120 kSa/s** (120 kB/s) over **2,000,000 baud, 8N1**. The ESP8266 defaults to a nominal **2442.312500 MHz** programme frequency. Idle and warm-up CW use **2442.625000 MHz**.

## Run a stereo clip

1. Copy the sketch to a folder matching its filename stem. In Arduino IDE select the ESP8266 board, Arduino ESP8266 core 3.1.2 and a 160 MHz CPU, then upload and close Serial Monitor.
2. Open the local HTML with a desktop Web Serial browser. Connect the board, choose a known stereo file and pre-encode a short selection. Start with the default ±50 kHz deviation profile and match receiver de-emphasis to the page's pre-emphasis setting.
3. Monitor the programme on an SDR with WFM stereo decoding or use the [standalone ESP8266 stereo receiver](../FMStereoRX-YEAHHHHHv1/README.md). Pass enough bandwidth for the stereo multiplex and use separate left and right test tones to check separation.
4. Use **ARRÊT RF** to stop the carrier. The firmware starts CW at boot and leaves CW on between clips.

An earlier HackRF test measured **30.90 dB L-to-R separation** at 997 Hz and **25.51 dB R-to-L separation** at 1499 Hz with a ±50 kHz profile and APWR 63. The pilot measured 18,999.94 Hz. These measurements are tied to that setup.

[Version 2](../FMStereoHQv2/README.md) adds frequency presets. [Version 3](../FMStereoHQv3/README.md) adds optional RDS in the browser.
