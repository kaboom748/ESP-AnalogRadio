/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "e32_pcm_worker.h"
#include "e32_dram_cursor.h"
#include "arduino_runtime.h"
#include "esp_cpu.h"
#include "sdkconfig.h"
#include "xtensa/xtruntime.h"
#include "xt_utils.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if !CONFIG_IDF_TARGET_ESP32 || CONFIG_FREERTOS_UNICORE || !CONFIG_ESP_IPC_ISR_ENABLE
#error "Use the stock dual-core ESP32 SDK"
#endif

namespace {
enum State : uint32_t { Idle, Requested, Busy, Stop, StopRearmTick };
constexpr uint32_t StartTimeoutCycles = 240000000u / 20u;
constexpr uint32_t StackBytes = 2048;
DRAM_ATTR volatile uint32_t state = Idle;
DRAM_ATTR e32_pcm_producer_t sessionProducer;
DRAM_ATTR void *sessionUser;
DRAM_ATTR StaticTask_t task;
DRAM_ATTR StackType_t stack[StackBytes / sizeof(StackType_t)];
TaskHandle_t handle;

// Calls use a local IRQ mask. An interrupt between WSR SCOMPARE1 and S32C1I
// could otherwise change the compare register with its own atomic access.
inline __attribute__((always_inline)) bool advance(State from, State to) {
    __asm__ volatile("memw" ::: "memory");
    const bool changed = xt_utils_compare_and_set(&state, from, to);
    __asm__ volatile("memw" ::: "memory");
    return changed;
}

void IRAM_ATTR runWorker(void *) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (e32_dram_load(&state) != Requested) continue;
        const uint32_t previous = XTOS_SET_INTLEVEL(15);
        // After publishing Busy, only internal RAM and the CPU cycle counter
        // may be used on core 0 until core 1 has left its radio critical loop.
        if (advance(Requested, Busy)) {
            const e32_pcm_producer_t produce = sessionProducer;
            void *const user = sessionUser;
            uint32_t command;
            while ((command = e32_dram_load(&state)) == Busy) produce(user);
            if (command == StopRearmTick) e32_stream_rearm_local_tick();
            e32_dram_store(&state, Idle);
        }
        XTOS_RESTORE_INTLEVEL(previous);
    }
}
} // namespace

extern "C" bool e32_pcm_worker_start(e32_pcm_producer_t produce, void *user) {
    if (!produce || xPortGetCoreID() != 1 || e32_dram_load(&state) != Idle) return false;
    if (!handle) {
        handle = xTaskCreateStaticPinnedToCore(runWorker, "pcm12", StackBytes, nullptr,
            configMAX_PRIORITIES - 2, stack, &task, 0);
        if (!handle) return false;
    }
    sessionProducer = produce;
    sessionUser = user;
    e32_dram_store(&state, Requested);
    xTaskNotifyGive(handle);
    const uint32_t started = esp_cpu_get_cycle_count();
    while (e32_dram_load(&state) == Requested) {
        if (uint32_t(esp_cpu_get_cycle_count() - started) < StartTimeoutCycles) continue;
        const uint32_t previous = XTOS_SET_INTLEVEL(15);
        const bool cancelled = advance(Requested, Idle);
        XTOS_RESTORE_INTLEVEL(previous);
        return !cancelled && e32_dram_load(&state) == Busy;
    }
    return e32_dram_load(&state) == Busy;
}

extern "C" void IRAM_ATTR e32_pcm_worker_stop(bool rearm_tick) {
    e32_dram_store(&state, rearm_tick ? StopRearmTick : Stop);
    while (e32_dram_load(&state) != Idle) {}
}

extern "C" uint32_t IRAM_ATTR e32_tx_mask_local(void) { return XTOS_SET_INTLEVEL(15); }
extern "C" void IRAM_ATTR e32_tx_restore_local(uint32_t previous) { XTOS_RESTORE_INTLEVEL(previous); }
