/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdint.h>
#include "esp_attr.h"
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
esp_err_t e32_arduino_runtime_prepare(void);

/* Call only after PHY preparation/warm TX keying is finished. The classic
 * dual-core ESP32 must keep the other CPU out of the radio/DPORT registers
 * while streaming. These functions park it through the SDK's IRAM IPC ISR
 * and mask the local interrupts. Between enter and leave, use only IRAM
 * code, DRAM and direct MMIO: no RTOS, PHY, driver, allocation or lock API.
 * Release before the slow PHY off/cleanup sequence. The token is opaque. */
uint32_t IRAM_ATTR e32_stream_enter(void);
void IRAM_ATTR e32_stream_leave(uint32_t token);

#ifdef __cplusplus
}
#endif
