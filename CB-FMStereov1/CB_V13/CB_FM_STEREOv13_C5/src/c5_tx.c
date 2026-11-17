/* SPDX-License-Identifier: GPL-3.0-or-later
 * CB13's centered native-NCO pulse-area FM at120000 MPX/s, CPU240MHz.
 * Adapted from ESP-AnalogRadio CB_FM_STEREOv13 fm_tx.h. Maintaining its
 * pulse-area modulation avoids adding a120kHz frequency ZOH's high-MPX
 * attenuation to the19k pilot, stereo subcarrier and57k RDS signal.
 */
#include "c5_tx.h"
#include "c5_tx_radio.h"
#include <string.h>
#include "esp_attr.h"
#include "esp_cpu.h"
#include "riscv/rv_utils.h"

#if !CONFIG_IDF_TARGET_ESP32C5 || CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ != 240 || CONFIG_PM_ENABLE
#error "Native CB13 TX requires ESP32-C5 at fixed240MHz"
#endif
#define CPU_HZ 240000000u
#define SAMPLE_CYCLES (CPU_HZ/C5_TX_PCM_HZ)
#define RING_MASK (C5_TX_RING_SAMPLES-1u)
#define REARM_GUARD_CYCLES 336
#define MAX_CATCHUP_SAMPLES 8u
#define MIN_PULSE_CYCLES 96
#define TONE_WRITE_CYCLES 48
#define EXIT_CYCLES 336
#define MAX_PULSE_CYCLES ((int32_t)(SAMPLE_CYCLES*51u/100u))

_Static_assert(CPU_HZ%C5_TX_PCM_HZ==0,"Exact MPX clock required");
_Static_assert((C5_TX_RING_SAMPLES&RING_MASK)==0,"Power-of-two PCM ring");

typedef struct {uint16_t nearest_k;int16_t pulse_cycles;} mpx_level_t;
static DRAM_ATTR mpx_level_t levels[256];
static DRAM_ATTR uint8_t pcm_ring[C5_TX_RING_SAMPLES];
static DRAM_ATTR c5_tx_stats_t counters;
static volatile uint32_t write_head,read_tail;
static volatile bool armed,running,end_seen;
static bool initialized,protected_stream;
static const char *last_fault;
static int32_t phase_debt;
static unsigned last_k;

static inline __attribute__((always_inline)) uint32_t cycles(void){return esp_cpu_get_cycle_count();}
static inline __attribute__((always_inline)) void fence(void){asm volatile("fence rw,rw":::"memory");}
static inline __attribute__((always_inline)) uint32_t magnitude(int32_t x){return x<0?(uint32_t)-x:(uint32_t)x;}

bool c5_tx_init(void) {
    if(initialized)return true;
    const int64_t denominator=127LL*78125LL;
    for(unsigned b=0;b<256u;++b) {
        const int32_t sample=(int8_t)b;
        const int64_t scaled=(int64_t)sample*50000LL*65536LL;
        const int32_t q=4*65536+(int32_t)((scaled+(scaled>=0?denominator/2:-denominator/2))/denominator);
        const int32_t nearest=(q+32768)>>16;
        const int32_t area=(q-nearest*65536)*(int32_t)SAMPLE_CYCLES;
        levels[b]=(mpx_level_t){(uint16_t)nearest,(int16_t)((area+(area>=0?32768:-32768))/65536)};
    }
    initialized=true;return true;
}

bool c5_tx_start(uint32_t carrier_hz,uint8_t apwr) {
    if(armed || running || apwr>191u || carrier_hz<2401000000u || carrier_hz>2482500000u)return false;
    if(!c5_tx_init() || !c5_tx_radio_prepare(carrier_hz,apwr))return false;
    memset(&counters,0,sizeof(counters));counters.min_queue=C5_TX_RING_SAMPLES;
    write_head=read_tail=0;phase_debt=0;last_k=8u;
    end_seen=false;protected_stream=false;last_fault=NULL;armed=true;return true;
}

size_t IRAM_ATTR c5_tx_queued_samples(void){return write_head-read_tail;}
size_t IRAM_ATTR c5_tx_free_samples(void){return C5_TX_RING_SAMPLES-(write_head-read_tail);}
bool IRAM_ATTR c5_tx_is_active(void){return armed;}
const char *c5_tx_fault(void){return last_fault;}
void IRAM_ATTR c5_tx_set_protected(bool value){protected_stream=value;}

bool IRAM_ATTR c5_tx_stage(uint16_t offset,uint8_t sample) {
    if(!armed || end_seen || offset>=c5_tx_free_samples())return false;
    pcm_ring[(write_head+offset)&RING_MASK]=sample==0x80u?0:sample;return true;
}
bool IRAM_ATTR c5_tx_commit(size_t count) {
    if(!armed || end_seen || count>c5_tx_free_samples())return false;
    fence();write_head+=(uint32_t)count;
    const uint32_t queued=write_head-read_tail;
    if(queued>counters.max_queue)counters.max_queue=queued;
    return true;
}
size_t IRAM_ATTR c5_tx_feed(const uint8_t *samples,size_t count) {
    if(!samples || !armed || end_seen)return 0;
    const size_t available=c5_tx_free_samples(),accepted=count<available?count:available;
    for(size_t n=0;n<accepted;++n)pcm_ring[(write_head+n)&RING_MASK]=samples[n]==0x80u?0:samples[n];
    if(accepted<count)counters.ring_drops+=(uint32_t)(count-accepted);
    (void)c5_tx_commit(accepted);return accepted;
}
void IRAM_ATTR c5_tx_finish(void){end_seen=true;}
void IRAM_ATTR c5_tx_stop(void) {
    armed=false;
    /* A streaming callback must never invoke slow calibration/PHY cleanup. */
    if(!running)c5_tx_radio_release();
}
void c5_tx_get_stats(c5_tx_stats_t *stats){if(stats)*stats=counters;}

static inline __attribute__((always_inline)) void wait_until(uint32_t deadline,void (*pump)(void*),void *user) {
    while(armed && (int32_t)(cycles()-deadline)<0)pump(user);
}

static void IRAM_ATTR emit_mpx(uint8_t sample,uint32_t deadline,void (*pump)(void*),void *user) {
    const mpx_level_t level=levels[sample];
    if(last_k!=level.nearest_k){c5_tx_radio_write_k(level.nearest_k);last_k=level.nearest_k;}
    int32_t area=(int32_t)level.pulse_cycles+phase_debt;
    const bool up=area>=0;
    int32_t width=(int32_t)magnitude(area);
    if(width>=MIN_PULSE_CYCLES) {
        if(width>MAX_PULSE_CYCLES)width=MAX_PULSE_CYCLES;
        const uint32_t start=deadline-(SAMPLE_CYCLES+(uint32_t)width)/2u;
        wait_until(start,pump,user);
        if(!armed)return;
        const int32_t room=(int32_t)(deadline-cycles())-EXIT_CYCLES-TONE_WRITE_CYCLES;
        if(width>room)width=room;
        if(width>=MIN_PULSE_CYCLES) {
            c5_tx_radio_write_k((unsigned)((int)level.nearest_k+(up?1:-1)));
            const uint32_t pulse_start=cycles();
            const uint32_t stop=pulse_start+(uint32_t)(width-TONE_WRITE_CYCLES);
            /* Holding the small centered pulse is the only interval without
             * a UART pump; the FIFO buffers its<4.25us duration. */
            while((int32_t)(cycles()-stop)<0){}
            c5_tx_radio_write_k(level.nearest_k);
            const int32_t actual=(int32_t)(cycles()-pulse_start);
            area+=up?-actual:actual;
        }
    }
    if(area>(int32_t)SAMPLE_CYCLES)area=SAMPLE_CYCLES;
    else if(area<-(int32_t)SAMPLE_CYCLES)area=-(int32_t)SAMPLE_CYCLES;
    phase_debt=area;
}

void IRAM_ATTR c5_tx_run(void (*pump)(void*),void *user) {
    if(!armed || running || !pump)return;
    const uint32_t pending=write_head-read_tail;
    if(pending<C5_TX_START_LEVEL && !end_seen)return;
    if(!pending){armed=false;c5_tx_radio_release();return;}
    running=true;
    const unsigned irq_state=RV_CLEAR_CSR(mstatus,MSTATUS_MIE);
    const uint32_t startup=cycles();
    if(!c5_tx_radio_key()) {
        last_fault="TX_RF";armed=false;
    }
    counters.startup_cycles=cycles()-startup;
    uint32_t deadline=cycles()+SAMPLE_CYCLES;
    while(armed) {
        uint32_t consume=1u;
        const uint32_t now=cycles();
        const int32_t boundary_late=(int32_t)(now-(deadline-SAMPLE_CYCLES));
        if(boundary_late>0 && (uint32_t)boundary_late>counters.max_late_cycles)
            counters.max_late_cycles=(uint32_t)boundary_late;
        if(boundary_late>(int32_t)(SAMPLE_CYCLES/4u))++counters.late_slots;
        while((int32_t)(deadline-now)<REARM_GUARD_CYCLES) {
            if(consume>=MAX_CATCHUP_SAMPLES){last_fault="TX_TIMING";armed=false;break;}
            ++consume;deadline+=SAMPLE_CYCLES;
        }
        if(!armed)break;
        const uint32_t queued=write_head-read_tail;
        if(queued<counters.min_queue)counters.min_queue=queued;
        if(consume>1u)counters.timer_skips+=consume-1u;
        uint8_t sample=0;
        if(queued>=consume){sample=pcm_ring[(read_tail+consume-1u)&RING_MASK];read_tail+=consume;}
        else {
            if(end_seen)break;
            ++counters.underruns;++counters.zero_samples;
            if(protected_stream){last_fault="TX_UNDERRUN";armed=false;break;}
        }
        emit_mpx(sample,deadline,pump,user);++counters.samples;
        wait_until(deadline,pump,user);
        deadline+=SAMPLE_CYCLES;
    }
    c5_tx_radio_off();
    RV_SET_CSR(mstatus,irq_state&MSTATUS_MIE);
    running=false;armed=false;
    c5_tx_radio_release();
}
