/* SPDX-License-Identifier: GPL-3.0-or-later
 * Runtime adaptation for the unmodified Espressif Arduino-ESP32 3.3.12
 * board package. Watchdog HAL sequence checked against its ESP32-C5 SDK.
 */
#include "arduino_runtime.h"

#if defined(ARDUINO)

#include <Arduino.h>
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/wdt_hal.h"
#include "soc/soc_caps.h"
#include "soc/timer_group_struct.h"

#if !CONFIG_IDF_TARGET_ESP32C5
#error "Select ESP32C5 Dev Module in the Espressif Arduino board package"
#endif
#if ARDUINO_USB_CDC_ON_BOOT || ARDUINO_USB_MSC_ON_BOOT || ARDUINO_USB_DFU_ON_BOOT
#error "Use UART0/CP210x: disable USB CDC, MSC and DFU On Boot in Arduino Tools"
#endif

static esp_err_t stop_task_watchdog(void)
{
#if CONFIG_ESP_TASK_WDT_EN
#if CONFIG_AUTOSTART_ARDUINO
    disableLoopWDT();
#endif
    /* C5 is single-core. Only remove the firmware/idle subscriptions that
     * this runtime owns; an unexpected third-party subscriber is an error. */
    TaskHandle_t idle = xTaskGetIdleTaskHandleForCore(0);
    if (idle && esp_task_wdt_status(idle) == ESP_OK) {
        const esp_err_t error = esp_task_wdt_delete(idle);
        if (error != ESP_OK) return error;
    }
    if (esp_task_wdt_status(nullptr) == ESP_OK) {
        const esp_err_t error = esp_task_wdt_delete(nullptr);
        if (error != ESP_OK) return error;
    }
    const esp_err_t error = esp_task_wdt_deinit();
    if (error != ESP_OK) {
        /* INVALID_STATE also means already deinitialized. Distinguish that
         * from a running watchdog with another task still subscribed. */
        if (esp_task_wdt_status(nullptr) != ESP_ERR_INVALID_STATE) return error;
    }
#endif
    return ESP_OK;
}

static esp_err_t stop_interrupt_watchdog(void)
{
#if CONFIG_ESP_INT_WDT
    static portMUX_TYPE watchdog_lock = portMUX_INITIALIZER_UNLOCKED;
    wdt_hal_context_t watchdog = {};
#if SOC_TIMER_GROUPS > 1
    /* The stock C5 int_wdt.c assigns the interrupt watchdog to MWDT1. */
    watchdog.inst = WDT_MWDT1;
    watchdog.mwdt_dev = &TIMERG1;
#else
    watchdog.inst = WDT_MWDT0;
    watchdog.mwdt_dev = &TIMERG0;
#endif
    /* Do not call wdt_hal_init(): it would reset prescalers/stages. The
     * existing context needs only its device pointer for a protected stop.
     * The IDF tick hook subsequently feeds/configures it but never enables
     * it again, so it remains off during the continuous RF critical loop. */
    /* Keep the tick-hook's own protect/feed/protect sequence from racing
     * these writes and re-protecting the device before our disable. */
    portENTER_CRITICAL(&watchdog_lock);
    wdt_hal_write_protect_disable(&watchdog);
    wdt_hal_disable(&watchdog);
    const bool still_enabled = wdt_hal_is_enabled(&watchdog);
    wdt_hal_write_protect_enable(&watchdog);
    portEXIT_CRITICAL(&watchdog_lock);
    if (still_enabled) return ESP_ERR_INVALID_STATE;
#endif
    return ESP_OK;
}

extern "C" esp_err_t c5_arduino_runtime_prepare(void)
{
    esp_log_level_set("*", ESP_LOG_NONE);
#if CONFIG_PM_ENABLE
    esp_pm_config_t fixed = {};
    fixed.max_freq_mhz = 240;
    fixed.min_freq_mhz = 240;
    fixed.light_sleep_enable = false;
    const esp_err_t pm_error = esp_pm_configure(&fixed);
    if (pm_error != ESP_OK) return pm_error;
#endif
    if (getCpuFrequencyMhz() != 240 && !setCpuFrequencyMhz(240))
        return ESP_ERR_INVALID_STATE;
    if (getCpuFrequencyMhz() != 240) return ESP_ERR_INVALID_STATE;
    esp_err_t error = stop_task_watchdog();
    if (error != ESP_OK) return error;
    error = stop_interrupt_watchdog();
    if (error != ESP_OK) return error;
    return ESP_OK;
}

#else

extern "C" esp_err_t c5_arduino_runtime_prepare(void)
{
    return ESP_OK;
}

#endif
