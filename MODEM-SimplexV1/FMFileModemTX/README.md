# FMFileModemTX — ESP8266 transmitter

Open [FMFileModemTX.ino](FMFileModemTX.ino) as an Arduino sketch and build it for ESP8266 at 160 MHz using ESP8266 Arduino core 3.1.2. Pair it with the [TX browser page](../FM_FILE_MODEM_TX.html) at **2,000,000 baud, 8N1**. The page supplies the encoded waveform as signed MPX8 at 120 kSa/s. The firmware sends FM and reports local UART and RF status.

The firmware uses idle CW and returns briefly to CW between transmit segments. Monitor it during startup and idle. See the [modem guide](../README.md) for matching profiles, receiver tuning, and download verification.
