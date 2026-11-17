/* CB_V6 transparent ESP32-C5 port, Arduino-ESP32 3.3.12.
 * Select ESP32C5 Dev Module; USB CDC On Boot Disabled.
 * CP210x UART0: TX11/RX12, 921600 baud, 8N2. Original HTML unchanged.
 */
#include <Arduino.h>
#include <esp_arduino_version.h>
#include "src/arduino_runtime.h"
#if !CONFIG_IDF_TARGET_ESP32C5
#error "Select ESP32C5 Dev Module"
#endif
#if ESP_ARDUINO_VERSION != ESP_ARDUINO_VERSION_VAL(3,3,12)
#error "Use Espressif Arduino-ESP32 3.3.12"
#endif
extern "C" void cb_duplex_run();
void setup() {
  if(c5_arduino_runtime_prepare()!=ESP_OK)for(;;)delay(1000);
  cb_duplex_run();
}
void loop() {delay(1000);}
