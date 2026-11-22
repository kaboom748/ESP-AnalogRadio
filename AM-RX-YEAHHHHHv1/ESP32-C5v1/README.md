# AM Receiver HQ for ESP32-C5

The C5 port demodulates mono AM on either RF band and sends **48 kSa/s PCM16** to the included Web Serial player.

## Build and receive

1. Open [AMReceiverHQ_C5.ino](AMReceiverHQ_C5/AMReceiverHQ_C5.ino) with **Arduino ESP32 3.3.12**. Select **ESP32C5 Dev Module**, **240 MHz**, **DIO 80 MHz**, PSRAM disabled, and **USB CDC On Boot disabled** for the USB-to-UART bridge. Set the flash size and partition scheme for your module. The documented bench used an **8 MB** flash layout.
2. Upload, close Serial Monitor, then open [AM_RECEIVER_HQ.html](AM_RECEIVER_HQ.html) in desktop Chrome or Edge using the [local Web Serial server](../../README.md).
3. Click **Ouvrir le port**, select the RF band, and enter the AM carrier in MHz. Click **Recevoir**. Start with automatic gain. If the audio clips, switch to manual gain using the PHY's 0–55 index and check the PCM meters. On the bench, manual index 45 reduced clipping compared with automatic gain.

| RF band | Firmware tuning range | Page default |
| --- | --- | ---: |
| 2.4 GHz | 2412–2472 MHz | 2442.313 MHz |
| 5 GHz | 5180–5885 MHz | 5180.313 MHz |

The defaults round the nominal AM carriers of the [C5 transmitter](../../AMSerialHQv1/ESP32-C5v1/README.md). Tune in 1 kHz increments to account for the references on your boards.

## Signal and transport

The modem provides I/Q at 96 kSa/s with a nominal 24 kHz low IF. The firmware shifts the signal to baseband, filters it with a 63-tap I/Q filter, and demodulates the AM envelope.

The serial link runs at **2,000,000 baud, 8N1**. Use `STATUS` to identify the device, `GAIN AUTO` or `GAIN <index>` for gain, and `START <kHz> HQ` for capture. Each `AM01` frame holds a little-endian sample index, 64 signed PCM16 values, and CRC16 over index and samples. The value `0x8000` marks an unavailable sample. Any serial byte stops capture. The browser also plays raw signed little-endian 48 kSa/s mono PCM16 files.

## Bench check

With HackRF as the AM source at 60% depth, the C5 recovered a 1 kHz tone near **2442.313 MHz** and **5180.313 MHz**, and recovered 700 Hz and 1700 Hz tones near **5785.313 MHz**. Five captures across manual and automatic gain reported zero CRC errors, and the active-signal measurement windows had continuous frame indices and valid samples. These measurements verified reception and framing at the listed settings.

**Related projects** · [Original ESP8266 receiver](../README.md) · [C5 AM transmitter](../../AMSerialHQv1/ESP32-C5v1/README.md)
