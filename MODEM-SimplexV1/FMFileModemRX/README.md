# FMFileModemRX — ESP8266 receiver

Open [FMFileModemRX.ino](FMFileModemRX.ino) as an Arduino sketch and build it for ESP8266 at 160 MHz using ESP8266 Arduino core 3.1.2. Pair it with the [RX browser page](../FM_FILE_MODEM_RX.html) at **1,500,000 baud, 8N2**. The firmware sends indexed, framed 120 kSa/s MPX8 in HQ mode. The page uses sample positions and invalid-sample markers during decoding, then checks the reconstructed file's SHA-256.

Start reception before the transmitter sends the file. See the [modem guide](../README.md) for tuning, serial settings for the other station, and profile selection.
