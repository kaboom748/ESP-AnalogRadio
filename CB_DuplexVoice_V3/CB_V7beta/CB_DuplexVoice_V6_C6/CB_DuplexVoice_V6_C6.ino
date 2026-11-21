/* CB_V6 TDD voice on ESP32-C6, Arduino-ESP32 3.3.12.
 * External UART0 TX16/RX17 at 921600 baud, 8N2.
 */
#include <Arduino.h>
#include <esp_arduino_version.h>
#include "src/arduino_runtime.h"

#if !CONFIG_IDF_TARGET_ESP32C6 || CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ != 160 || CONFIG_PM_ENABLE
#error "Select ESP32C6 Dev Module at a fixed 160 MHz"
#endif
#if ESP_ARDUINO_VERSION != ESP_ARDUINO_VERSION_VAL(3,3,12)
#error "Use Espressif Arduino-ESP32 3.3.12"
#endif

extern "C" void cb_duplex_run();

void setup() {
    if (c6_arduino_runtime_prepare() != ESP_OK) for (;;) delay(1000);
    cb_duplex_run();
}

void loop() { delay(1000); }
