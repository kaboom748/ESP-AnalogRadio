# CB Duplex Voice V6 firmware for ESP32-C5

Open [CB_DuplexVoice_V6_C5.ino](CB_DuplexVoice_V6_C5.ino) with **Arduino-ESP32 3.3.12**. Select **ESP32C5 Dev Module**, use the **240 MHz** CPU configuration and disable **USB CDC On Boot**. Keep the `src` directory with the sketch because it contains the radio backend for this core release.

Use a USB–UART bridge connected to **UART0 TX GPIO11 / RX GPIO12** at **921,600 baud, 8N2**. Open the same [V6 browser app](../CB_DuplexVoice_EASY_HTML/) as for ESP8266. The C5 port implements the same DPX1 audio and session protocol. This sketch accepts 2.4 GHz channels. See the [V6 guide](../) for the initial call settings.
