/* SPDX-License-Identifier: GPL-3.0-or-later
 * SRC0 capture adapted from ESPARGOS/esp-sdr main/targets/esp32/receiver.c
 * pristine 3ab3b65bb62f2d9427ee33c2c3b627427e729294 (GPL-3.0-or-later).
 * Register sequence: reserved SRAM mode3, byte lanes, pulse trigger,
 * sentinel verification and forced ADC-on. PHY setup remains CB13's.
 */
#include "e32_adc_bench.h"

#if CB13_BENCH_IQ
#include <string.h>
#include "arduino_runtime.h"
#include "esp_cpu.h"
#include "heap_memory_layout.h"
#include "soc/soc.h"
#include "soc/dport_reg.h"
#include "soc/dport_access.h"

#if !CONFIG_IDF_TARGET_ESP32 || F_CPU != 240000000L || CONFIG_PM_ENABLE
#error "ADC bench requires the classic ESP32 at fixed 240MHz"
#endif

#define ADC_BEGIN 0x3ffe8000u
#define ADC_END   0x3fff8000u
#define ADC_IRAM_BEGIN 0x400a8000u
#define ADC_IRAM_END   0x400b8000u
#define ADC_WORDS 16384u
#define SENTINEL 0xa55aa55au
#define CPU_HZ 240000000u
#define DUMP_CTRL 0x60033d90u
#define DUMP_STATUS 0x60033d94u
#define DUMP_BYTES 0x60033dc4u
#define ADC_CTRL 0x3ff460a0u
#define ADC_ON_FORCE ((1u<<9)|(1u<<8))
#define DUMP_ENABLE (1u<<31)
#define DUMP_TRIGGER (1u<<19)
#define DUMP_DONE (1u<<18)
#define EST_CTL0 0x3ff46060u
#define EST_CTL1 0x3ff4607cu
#define EST_I    0x3ff460dcu
#define EST_Q    0x3ff460e0u
#define EST_POWER 0x3ff460e4u
#define EST_DONE (1u<<31)
#define EST_COUNT_MASK 0x0001fffcu

/* The SDK removes both DIRAM regions, including their IRAM aliases,
 * before creating heaps. KEEP(.reserved_memory_address) is stock IDF5.5.5. */
SOC_RESERVE_MEMORY_REGION(ADC_BEGIN, ADC_END, e32_adc_bench);
extern char _iram_end[], _data_end[], _bss_end[], _heap_start[];
extern soc_reserved_region_t soc_reserved_memory_region_start;
extern soc_reserved_region_t soc_reserved_memory_region_end;

static inline __attribute__((always_inline)) uint32_t cycles(void)
{ return esp_cpu_get_cycle_count(); }
static inline __attribute__((always_inline)) void fence(void)
{ __asm__ volatile("memw":::"memory"); }
static inline __attribute__((always_inline)) void wait_cycles(uint32_t n)
{
    const uint32_t start=cycles();
    while(cycles()-start<n){}
}

static bool memory_safe(void)
{
    const uintptr_t reservation=(uintptr_t)&reserved_region_e32_adc_bench;
    const uintptr_t table_start=(uintptr_t)&soc_reserved_memory_region_start;
    const uintptr_t table_end=(uintptr_t)&soc_reserved_memory_region_end;
    volatile uint32_t stack_marker=0;
    const uintptr_t stack=(uintptr_t)&stack_marker;
    return (uintptr_t)_iram_end<=ADC_IRAM_BEGIN &&
           (uintptr_t)_data_end<=ADC_BEGIN && (uintptr_t)_bss_end<=ADC_BEGIN &&
           (uintptr_t)_heap_start<ADC_BEGIN &&
           reservation>=table_start && reservation+sizeof(soc_reserved_region_t)<=table_end &&
           reserved_region_e32_adc_bench.start==ADC_BEGIN &&
           reserved_region_e32_adc_bench.end==ADC_END &&
           !(stack>=ADC_BEGIN && stack<ADC_END) &&
           !(stack>=ADC_IRAM_BEGIN && stack<ADC_IRAM_END);
}

esp_err_t e32_adc_bench_capture(unsigned n,e32_adc_bench_stats_t *stats)
{
    if(!stats || n<256u || n>E32_ADC_BENCH_MAX_SAMPLES) return ESP_ERR_INVALID_ARG;
    const uintptr_t destination=(uintptr_t)stats;
    if(destination<ADC_END && destination+sizeof(*stats)>ADC_BEGIN)
        return ESP_ERR_INVALID_ARG;
    if(destination<ADC_IRAM_END && destination+sizeof(*stats)>ADC_IRAM_BEGIN)
        return ESP_ERR_INVALID_ARG;
    memset(stats,0,sizeof(*stats));
    stats->bad_index=UINT32_MAX;
    if(!memory_safe()) return ESP_ERR_INVALID_STATE;

    volatile uint32_t *const samples=(volatile uint32_t *)ADC_BEGIN;
    const uint32_t token=e32_stream_enter();
    const uint32_t saved_ctrl=REG_READ(DUMP_CTRL);
    const uint32_t saved_owner=DPORT_SEQUENCE_REG_READ(DPORT_IRAM_DRAM_AHB_SEL_REG);
    const uint32_t saved_lane=REG_READ(DUMP_BYTES);
    const uint32_t saved_adc=DPORT_SEQUENCE_REG_READ(ADC_CTRL);
    const uint32_t saved_est=DPORT_SEQUENCE_REG_READ(EST_CTL1)&~EST_DONE;
    const uint32_t saved_est0=DPORT_SEQUENCE_REG_READ(EST_CTL0);
    /* Never take over an existing dump or restore a live trigger later.
     * Requiring the caller's ADC force also checks the prepared-RX contract. */
    if((saved_ctrl&(DUMP_ENABLE|DUMP_TRIGGER)) ||
       (saved_owner&DPORT_MAC_DUMP_MODE_M)!=0u ||
       (saved_adc&ADC_ON_FORCE)!=ADC_ON_FORCE ||
       (saved_est&3u)!=1u || (saved_est&0x00180000u)!=0x00100000u ||
       (saved_est0&0x0c000000u)!=0x04000000u) {
        e32_stream_leave(token);
        stats->saved_owner=saved_owner;stats->saved_lane=saved_lane;
        stats->saved_control=saved_ctrl;
        return ESP_ERR_INVALID_STATE;
    }

    REG_WRITE(DUMP_CTRL,0);
    for(unsigned j=0;j<ADC_WORDS;++j)samples[j]=SENTINEL;
    DPORT_REG_WRITE(ADC_CTRL,saved_adc|ADC_ON_FORCE);fence();
    wait_cycles(CPU_HZ/500000u); /* 2us, no ROM/RTOS call while parked. */
    DPORT_REG_WRITE(DPORT_IRAM_DRAM_AHB_SEL_REG,
        (saved_owner&~DPORT_MAC_DUMP_MODE_M)|(3u<<DPORT_MAC_DUMP_MODE_S));
    REG_WRITE(DUMP_BYTES,0x03020100u);fence();
    /* SRC0, clock selector1 (80MS/s): neither divider bit15 nor16 set. */
    const uint32_t ctrl=n|DUMP_ENABLE;
    REG_WRITE(DUMP_CTRL,ctrl);fence();
    /* Only replace the native length. Source, enable, CTL0 and analog
     * gain/ADC clocks retain the state established by e32_rx_start(). */
    const uint32_t native_ctrl=(saved_est&~(EST_COUNT_MASK|2u))|((n-1u)<<2);
    REG_WRITE(EST_CTL1,native_ctrl);fence();
    const uint32_t clear_start=cycles();
    uint32_t native_status;
    do {
        native_status=DPORT_SEQUENCE_REG_READ(EST_CTL1);
        if(!(native_status&EST_DONE))break;
    } while(cycles()-clear_start<CPU_HZ/50u);
    const bool native_cleared=(native_status&EST_DONE)==0u;
    const uint32_t native_started=cycles();
    if(native_cleared) {REG_WRITE(EST_CTL1,native_ctrl|2u);fence();}
    const uint32_t started=cycles();
    REG_WRITE(DUMP_CTRL,ctrl|DUMP_TRIGGER);
    REG_WRITE(DUMP_CTRL,ctrl);fence();
    uint32_t result;
    do {
        result=REG_READ(DUMP_CTRL);
        if(result&DUMP_DONE)break;
    } while(cycles()-started<CPU_HZ/50u); /* Bound only the dump wait to20ms. */
    const uint32_t elapsed=cycles()-started;
    const uint32_t status=REG_READ(DUMP_STATUS);
    REG_WRITE(DUMP_CTRL,0);fence();
    native_status=DPORT_SEQUENCE_REG_READ(EST_CTL1);
    const bool native_at_raw_end=native_cleared && (native_status&EST_DONE)!=0u;
    while(native_cleared && !(native_status&EST_DONE) &&
          cycles()-native_started<CPU_HZ/50u)
        native_status=DPORT_SEQUENCE_REG_READ(EST_CTL1);
    const bool native_ready=native_cleared && (native_status&EST_DONE)!=0u;
    int32_t native_i=0,native_q=0;
    uint32_t native_power=0;
    if(native_ready) {
        native_i=(int32_t)DPORT_SEQUENCE_REG_READ(EST_I);
        native_q=(int32_t)DPORT_SEQUENCE_REG_READ(EST_Q);
        native_power=DPORT_SEQUENCE_REG_READ(EST_POWER);
    }
    /* Read sums before lowering the trigger, then restore the full old
     * control word, excluding only its read-only DONE flag. */
    REG_WRITE(EST_CTL1,native_ctrl);fence();
    REG_WRITE(EST_CTL1,saved_est);fence();
    const bool native_restored=
        (DPORT_SEQUENCE_REG_READ(EST_CTL1)&~EST_DONE)==saved_est;
    wait_cycles(CPU_HZ/100000u); /* 10us before releasing MAC SRAM ownership. */
    DPORT_REG_WRITE(DPORT_IRAM_DRAM_AHB_SEL_REG,saved_owner);
    REG_WRITE(DUMP_BYTES,saved_lane);
    DPORT_REG_WRITE(ADC_CTRL,
        (DPORT_SEQUENCE_REG_READ(ADC_CTRL)&~ADC_ON_FORCE)|(saved_adc&ADC_ON_FORCE));
    REG_WRITE(DUMP_CTRL,saved_ctrl);fence();
    uint32_t restored=0;
    if(DPORT_SEQUENCE_REG_READ(DPORT_IRAM_DRAM_AHB_SEL_REG)==saved_owner)restored|=1u;
    if(REG_READ(DUMP_BYTES)==saved_lane)restored|=2u;
    if((REG_READ(DUMP_CTRL)&~DUMP_DONE)==(saved_ctrl&~DUMP_DONE))restored|=4u;
    if((DPORT_SEQUENCE_REG_READ(ADC_CTRL)&ADC_ON_FORCE)==(saved_adc&ADC_ON_FORCE))restored|=8u;
    e32_stream_leave(token);

    stats->capture_cycles=elapsed;stats->control=result;stats->status=status;
    stats->saved_owner=saved_owner;stats->saved_lane=saved_lane;
    stats->saved_control=saved_ctrl;stats->restored=restored;
    stats->native_i=native_i;stats->native_q=native_q;
    stats->native_power=native_power;stats->native_ready=native_ready;
    stats->trigger_delta_cycles=native_cleared?started-native_started:0u;
    stats->native_done_at_raw_end=native_at_raw_end;
    if(restored!=15u||!native_restored)return ESP_ERR_INVALID_STATE;
    if(!(result&DUMP_DONE)||(status&0x7fffu)!=n)return ESP_ERR_TIMEOUT;
    /* The owner is restored and peer released before inspecting results.
     * Four or more sentinel words after n guard the end of the aperture. */
    for(unsigned j=0;j<ADC_WORDS;++j) {
        const uint32_t word=samples[j];
        if((j<n && word==SENTINEL)||(j>=n && word!=SENTINEL)) {
            stats->bad_index=j;
            return ESP_ERR_INVALID_RESPONSE;
        }
    }
    stats->raw=(const uint32_t *)ADC_BEGIN;stats->samples=n;
    return ESP_OK;
}
#endif
