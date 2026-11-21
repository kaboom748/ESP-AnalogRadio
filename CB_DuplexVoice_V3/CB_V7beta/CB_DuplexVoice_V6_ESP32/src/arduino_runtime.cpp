/* SPDX-License-Identifier: GPL-3.0-or-later
 * Runtime adaptation for the unmodified Espressif Arduino-ESP32 3.3.12
 * board package. Watchdog HAL sequence checked against its ESP32 SDK.
 */
#include "arduino_runtime.h"
#include "sdkconfig.h"
#include "esp_ipc_isr.h"
#include "esp_timer.h"
#include "xtensa/core-macros.h"
#include "xtensa/xtruntime.h"

#if !CONFIG_IDF_TARGET_ESP32
#error "This runtime is for the original Xtensa ESP32"
#endif
#if CONFIG_FREERTOS_UNICORE || !CONFIG_ESP_IPC_ISR_ENABLE
#error "Use the stock dual-core ESP32 SDK with IPC ISR enabled"
#endif

#if CONFIG_FREERTOS_SYSTICK_USES_CCOUNT
extern "C" unsigned _xt_tick_divisor;
extern "C" void e32_rearm_peer_tick(void *divisor);

// The radio task owns the only active RX, TX or ADC capture.
static DRAM_ATTR int64_t stream_enter_us;
static constexpr int64_t TickCatchUpLimitUs = 8900000;
#endif

extern "C" uint32_t e32_stream_enter(void)
{
#if CONFIG_FREERTOS_SYSTICK_USES_CCOUNT
    stream_enter_us = esp_timer_get_time();
#endif
    // IPC masks the local core to level3 and parks the other at level5.
    // Stop all remaining local IRQs only after the IPC handshake succeeds.
    esp_ipc_isr_stall_other_cpu();
    return XTOS_SET_INTLEVEL(15);
}

extern "C" void e32_stream_leave(uint32_t token)
{
#if CONFIG_FREERTOS_SYSTICK_USES_CCOUNT
    // Beyond half a CCOUNT turn the signed tick catch-up comparison treats
    // an expired deadline as future. Re-arm both cores after a long capture.
    const bool rearm_tick = esp_timer_get_time() - stream_enter_us >= TickCatchUpLimitUs;
    if (rearm_tick) {
        XTHAL_SET_CCOMPARE(0, uint32_t(XTHAL_GET_CCOUNT()) + _xt_tick_divisor);
        __asm__ volatile("esync" ::: "memory");
    }
#endif
    // Restore the IPC critical level before releasing its saved context.
    XTOS_RESTORE_INTLEVEL(token);
    esp_ipc_isr_release_other_cpu();
#if CONFIG_FREERTOS_SYSTICK_USES_CCOUNT
    if (rearm_tick) {
        esp_ipc_isr_call_blocking(e32_rearm_peer_tick,
                                  reinterpret_cast<void *>(uintptr_t(_xt_tick_divisor)));
    }
#endif
}

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

#if !CONFIG_IDF_TARGET_ESP32
#error "Select ESP32 Dev Module in the Espressif Arduino board package"
#endif
#if ARDUINO_USB_CDC_ON_BOOT || ARDUINO_USB_MSC_ON_BOOT || ARDUINO_USB_DFU_ON_BOOT
#error "Use UART0 through the USB-UART bridge"
#endif
#if defined(F_CPU) && F_CPU != 240000000L
#error "Select CPU Frequency 240MHz for this timed radio firmware"
#endif

static esp_err_t stop_task_watchdog(void)
{
#if CONFIG_ESP_TASK_WDT_EN
#if CONFIG_AUTOSTART_ARDUINO
    disableLoopWDT();
#endif
    /* The SDK owns its configured idle subscriptions. deinit() removes
     * them itself according to idle_core_mask: deleting them manually
     * first makes its unsubscribe_idle() abort with NOT_FOUND. Remove
     * only our explicit task subscription before handing ownership back.
     * An unexpected third-party subscriber still makes deinit() fail. */
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
    wdt_hal_context_t watchdog = {};
#if SOC_TIMER_GROUPS > 1
    /* The exact stock classic SDK int_wdt.c assigns IRQ WDT to MWDT1. */
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
    /* A local critical section alone cannot exclude the CPU0 tick hook.
     * Park the other core briefly, then do only bare watchdog MMIO here.
     * Its stock tick hooks configure/feed/protect but never re-enable it. */
    esp_ipc_isr_stall_other_cpu();
    wdt_hal_write_protect_disable(&watchdog);
    wdt_hal_disable(&watchdog);
    const bool still_enabled = wdt_hal_is_enabled(&watchdog);
    wdt_hal_write_protect_enable(&watchdog);
    esp_ipc_isr_release_other_cpu();
    if (still_enabled) return ESP_ERR_INVALID_STATE;
#endif
    return ESP_OK;
}

extern "C" esp_err_t e32_arduino_runtime_prepare(void)
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
    /* The precompiled classic sdkconfig says 160MHz; the Arduino board's
     * F_CPU setting and the actual runtime clock are the relevant values. */
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

extern "C" esp_err_t e32_arduino_runtime_prepare(void)
{
    return ESP_OK;
}

#endif
