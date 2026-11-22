# AMReceiverHQ_C5 firmware

Arduino sketch for the dual-band **ESP32-C5** AM receiver. Keep [AMReceiverHQ_C5.ino](AMReceiverHQ_C5.ino), [rx_radio.cpp](rx_radio.cpp), and [rx_radio.h](rx_radio.h) together. The radio files handle C5 tuning, gain, and I/Q acquisition. The sketch filters and demodulates the signal and sends PCM16 audio.

Use the [C5 build and reception instructions](../README.md) and the matching [Web Serial receiver page](../AM_RECEIVER_HQ.html).
