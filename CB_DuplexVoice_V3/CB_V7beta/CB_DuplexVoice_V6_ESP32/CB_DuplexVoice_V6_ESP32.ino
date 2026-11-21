/* CB_V6 native TDD on ESP32 Dev Module, Arduino-ESP32 3.3.12.
 * CPU 240 MHz, DIO flash at 40 MHz, external UART0 GPIO1/GPIO3, 921600 8N2.
 */
#include <Arduino.h>
#include <esp_arduino_version.h>
#include "src/arduino_runtime.h"

#if !CONFIG_IDF_TARGET_ESP32
#error "Select ESP32 Dev Module"
#endif
#if ESP_ARDUINO_VERSION != ESP_ARDUINO_VERSION_VAL(3,3,12)
#error "Use Espressif Arduino-ESP32 3.3.12"
#endif

extern "C" void cb_duplex_run();

void setup() {
    if (e32_arduino_runtime_prepare() != ESP_OK) for (;;) delay(1000);
    cb_duplex_run();
}

void loop() { delay(1000); }
