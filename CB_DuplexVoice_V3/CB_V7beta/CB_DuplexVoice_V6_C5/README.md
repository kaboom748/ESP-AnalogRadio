# CB Duplex Voice V7 beta firmware for ESP32-C5

Open [CB_DuplexVoice_V6_C5.ino](CB_DuplexVoice_V6_C5.ino) in Arduino IDE with **Arduino-ESP32 3.3.12**. Select **ESP32C5 Dev Module**, use **240 MHz** and disable **USB CDC On Boot**. Keep the sketch and its `src` directory together.

The [V7 beta browser app](../CB_DuplexVoice_EASY_HTML/) connects to the bridge on **UART0 TX GPIO11 / RX GPIO12** at **921,600 baud, 8N2**. This firmware and the browser tune within 2.4 GHz. See the [beta setup](../) for the same channel, companding and timing to use at each station.
