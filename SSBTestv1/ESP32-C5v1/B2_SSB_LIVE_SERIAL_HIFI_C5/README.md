# ESP32-C5 SSB firmware

Arduino sketch for dual-band USB and LSB transmission on the **ESP32-C5**. Keep [B2_SSB_LIVE_SERIAL_HIFI_C5.ino](B2_SSB_LIVE_SERIAL_HIFI_C5.ino), [radio.cpp](radio.cpp), and [radio.h](radio.h) together. The sketch receives the polar audio stream over UART and reconstructs I/Q. The radio files apply the selected C5 RF profile.

Use the [C5 build and operating instructions](../README.md) and the matching [Web Serial player](../SSB_LIVE_WEBSERIAL_HIFI.html).
