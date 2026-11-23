/* SPDX-License-Identifier: GPL-3.0-or-later
 * Runtime adaptation for the unmodified Espressif Arduino-ESP32 3.3.12
 * board package. Watchdog HAL sequence checked against its ESP32 SDK.
 */
#include "arduino_runtime.h"
#include "sdkconfig.h"
#include "esp_cpu.h"
#include "esp_ipc_isr.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "xtensa_timer.h"
#include "xtensa/xtruntime.h"

#if !CONFIG_IDF_TARGET_ESP32
#error "This runtime is for the original Xtensa ESP32"
#endif
#if CONFIG_FREERTOS_UNICORE || !CONFIG_ESP_IPC_ISR_ENABLE
#error "Use the stock dual-core ESP32 SDK with IPC ISR enabled"
#endif
#if !CONFIG_FREERTOS_SYSTICK_USES_CCOUNT || XT_TIMER_INDEX != 0
#error "This runtime expects the Classic SDK's CCOMPARE0 tick timer"
#endif

extern "C" void e32_rearm_peer_tick(void *period);

namespace {
constexpr uint32_t CpuHz = 240000000;
constexpr uint32_t TickCycles = CpuHz / configTICK_RATE_HZ;
constexpr int64_t TickUs = 1000000 / configTICK_RATE_HZ;
// The Xtensa tick ISR compares CCOUNT-CCOMPARE as a signed 32-bit value.
// Allow for a tick already due when the radio takes ownership.
constexpr int64_t MaxTickDelayUs = (INT64_C(1) << 31) / (CpuHz / 1000000) - 2 * TickUs;
DRAM_ATTR int64_t streamStartUs;
DRAM_ATTR int64_t streamElapsedUs;
DRAM_ATTR TickType_t streamStartTick;

void rearmPeerTick()
{
    void *const period = reinterpret_cast<void *>(TickCycles);
    const TickType_t before = xTaskGetTickCount();
    esp_ipc_isr_call_blocking(e32_rearm_peer_tick, period);
    // A pending tick ISR may have read CCOMPARE just before IPC interrupted
    // it. Verify two ticks so that one old pending tick cannot hide a stall.
    const int64_t deadline = esp_timer_get_time() + 4 * TickUs;
    while (xTaskGetTickCount() - before < 2 && esp_timer_get_time() < deadline) {}
    if (xTaskGetTickCount() - before < 2) {
        esp_ipc_isr_call_blocking(e32_rearm_peer_tick, period);
    }
}
}

extern "C" void e32_stream_record_start(void)
{
    streamStartTick = xTaskGetTickCount();
    streamStartUs = esp_timer_get_time();
}

extern "C" void IRAM_ATTR e32_stream_rearm_local_tick(void)
{
    const uint32_t nextTick = esp_cpu_get_cycle_count() + TickCycles;
    asm volatile("wsr.ccompare0 %0\n\tesync" :: "a"(nextTick) : "memory");
}

extern "C" bool IRAM_ATTR e32_stream_prepare_leave(void)
{
    streamElapsedUs = esp_timer_get_time() - streamStartUs;
    if (streamElapsedUs < MaxTickDelayUs) return false;
    e32_stream_rearm_local_tick();
    return true;
}

extern "C" void e32_stream_catchup_ticks(void)
{
    const TickType_t elapsedTicks = static_cast<TickType_t>(streamElapsedUs / TickUs);
    const TickType_t deliveredTicks = xTaskGetTickCount() - streamStartTick;
    if (deliveredTicks < elapsedTicks) xTaskCatchUpTicks(elapsedTicks - deliveredTicks);
}

extern "C" uint32_t e32_stream_enter(void)
{
    e32_stream_record_start();
    // IPC masks the local core to level3 and parks the other at level5.
    // Stop all remaining local IRQs only after the IPC handshake succeeds.
    esp_ipc_isr_stall_other_cpu();
    return XTOS_SET_INTLEVEL(15);
}

extern "C" void e32_stream_leave(uint32_t token)
{
    const bool rearmTick = e32_stream_prepare_leave();
    // Restore the IPC critical level before releasing its saved context.
    XTOS_RESTORE_INTLEVEL(token);
    esp_ipc_isr_release_other_cpu();
    if (rearmTick) {
        rearmPeerTick();
        e32_stream_catchup_ticks();
    }
}

#if defined(ARDUINO)

#include <Arduino.h>
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_task_wdt.h"
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
