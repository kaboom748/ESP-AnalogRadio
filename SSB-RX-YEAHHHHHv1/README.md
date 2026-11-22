# SSB Receiver HQ

Receives **2.4 GHz** SSB with an **ESP8266**. The board captures and filters I/Q, and the browser demodulates **USB** or **LSB**, fine-tunes the receiver digitally, and plays the recovered audio.

- Firmware · [SSBReceiverHQ.ino](SSBReceiverHQ.ino)
- Web Serial receiver · [SSB_RECEIVER_HQ.html](SSB_RECEIVER_HQ.html)
- Matching transmitter for 2.4 GHz · [SSBTestv1](../SSBTestv1/README.md)

## Quick start

1. Use **Arduino ESP8266 3.1.2**, select your board, and set the CPU to **160 MHz**. Open the sketch in a folder named `SSBReceiverHQ`, upload it, and close Serial Monitor. [ESP8266 Arduino installation](https://arduino-esp8266.readthedocs.io/en/3.1.2/installing.html)
2. Serve the repository following the [Web Serial setup](../README.md) and open the receiver page in desktop Chrome or Edge. Click **Ouvrir le port** and select the USB-to-UART bridge.
3. Enter the approximate SSB carrier reference in MHz, select the transmitting sideband, and click **Recevoir**. The default **2442.287 MHz** is an earlier bench starting point. With SSBTestv1, start reception during its CW warm-up so the page can see the tuning marker.
4. Adjust the audio filter for the source and use the digital coarse and fine tuning to correct pitch. When testing with a known tone, enter its frequency under **Référence de suivi**. Leave that field at zero for voice or music.

## I/Q transport

The serial link runs at **1,500,000 baud, 8N2**. `START <kHz> HQ` requests the I/Q centre frequency. The radio LO is nominally 24 kHz below it. The ESP outputs **48 kSa/s complex** samples in 170-byte `IQ10` frames with a sample index and CRC16. Every five payload bytes pack two I/Q pairs as signed 10-bit values. The pair `(-512, -512)` marks an unavailable sample.

The page reports CRC, missing samples, and audio buffering. Its offline player uses raw packed I/Q at 48 kSa/s complex, with the same five-byte groups as the serial payload. The receiver runs its SSB detection on I/Q and uses a different page and serial format from [AM Receiver HQ](../AM-RX-YEAHHHHHv1/README.md).
