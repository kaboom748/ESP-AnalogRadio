# MODEM-SimplexV1 — one-way FM file modem

Two ESP8266 boards carry a file over an analog FM link at 2.4 GHz. The TX browser packetizes and modulates the data. The RX browser reconstructs it from the demodulated waveform and verifies the file before offering a download. The RF link is simplex and has no return-channel acknowledgements.

| Station | Firmware | Browser page | Local serial link |
| --- | --- | --- | --- |
| TX | [FMFileModemTX](FMFileModemTX/README.md) | [FM_FILE_MODEM_TX.html](FM_FILE_MODEM_TX.html) | 2,000,000 baud, 8N1 |
| RX | [FMFileModemRX](FMFileModemRX/README.md) | [FM_FILE_MODEM_RX.html](FM_FILE_MODEM_RX.html) | 1,500,000 baud, 8N2 |

## Send a file

1. Flash the appropriate Arduino sketch on each ESP8266 using the 160 MHz CPU setting. Open the two local HTML pages on a desktop Web Serial browser. Loopback HTTP is suitable for the browser's secure-context APIs. See the [repository quick start](../README.md#start-with-cb-fm-stereo-v15).
2. Choose TX CH 1, 7 or 11. The nominal programme centres are 2412.3125, 2442.3125 and 2462.3125 MHz. Set the RX page to the received centre frequency and use the same modulation profile at both ends.
3. Start the RX page with **Recevoir**. Choose a short test file and an FEC level on the TX page, then select **Émettre le fichier**. Check progress and listen to the received signal if needed.
4. Wait for verification and use **Télécharger** on the RX page. The transmitter can show the samples sent even if receiver acquisition or decoding fails.

The browser offers V.21 (300 bit/s), V.22 (1,200 bit/s), V.22bis (2,400 bit/s), and experimental V.BoOm (120 kbit/s gross) profiles. Packet framing, parity and acquisition reduce useful throughput. Frames carry CRC32, optional redundancy helps recover erasures, and the final file is checked with SHA-256. The protocol accepts files up to 16 MiB. Start with a small file to check tuning and profile selection before attempting a longer transfer.
