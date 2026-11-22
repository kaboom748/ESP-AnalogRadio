/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Call once from Arduino setup(), before firmware_run(). No Serial.begin(),
 * UART ownership, Wi-Fi or RF calls are made here. The stock Arduino core
 * starts watchdogs; the firmware's continuous IRQ-masked RF loops require
 * those watchdogs stopped, just as in the qualified ESP-IDF configuration.
 * ESP-IDF builds configure this through sdkconfig and get an inert stub.
 * On any failure the caller must not start the timed radio engine. */
esp_err_t c5_arduino_runtime_prepare(void);

#ifdef __cplusplus
}
#endif
