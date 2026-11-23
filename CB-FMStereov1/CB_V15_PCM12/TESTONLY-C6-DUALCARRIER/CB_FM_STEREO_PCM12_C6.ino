/* CB FM Stereo PCM12 for ESP32-C6, Arduino-ESP32 3.3.12.
 * UART0 GPIO16/TX GPIO17/RX via CH343, fixed 160 MHz.
 * Native I/Q and NCO preserve the stereo pilot and RDS.
 */
#include <Arduino.h>
#include <esp_arduino_version.h>
#include "src/arduino_runtime.h"
#if !CONFIG_IDF_TARGET_ESP32C6
#error "Select ESP32C6 Dev Module"
#endif
#if ESP_ARDUINO_VERSION != ESP_ARDUINO_VERSION_VAL(3,3,12)
#error "Use Espressif Arduino-ESP32 3.3.12"
#endif
extern "C" void cb_fm_stereo_run();
void setup() {
    if(c6_arduino_runtime_prepare()!=ESP_OK)for(;;)delay(1000);
    cb_fm_stereo_run();
}
void loop() {delay(1000);}
