/* CB FM Stereo PCM12 — ESP32-C5, stock Arduino-ESP32 3.3.12.
 * Shared ESP8266 PCM12 wire format, UART0 GPIO11/TX GPIO12/RX via CP210x.
 * Native I/Q and NCO processing support the 2.4 and 5 GHz RF bands.
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
extern "C" void cb_fm_stereo_run();
void setup() {
    if(c5_arduino_runtime_prepare()!=ESP_OK)for(;;)delay(1000);
    cb_fm_stereo_run();
}
void loop() {delay(1000);}
