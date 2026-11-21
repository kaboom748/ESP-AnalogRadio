/* SPDX-License-Identifier: GPL-3.0-or-later
 * CB13's centered native-NCO pulse-area FM at120000 MPX/s, CPU240MHz.
 * Adapted from ESP-AnalogRadio CB_FM_STEREOv13 fm_tx.h. Maintaining its
 * pulse-area modulation avoids adding a120kHz frequency ZOH's high-MPX
 * attenuation to the19k pilot, stereo subcarrier and57k RDS signal.
 */
#include "e32_tx.h"
#include "e32_tx_radio.h"
#include <string.h>
#include "esp_attr.h"
#include "esp_cpu.h"
#include "arduino_runtime.h"

#if !CONFIG_IDF_TARGET_ESP32 || F_CPU != 240000000L || CONFIG_PM_ENABLE
#error "Native CB13 TX requires ESP32 at fixed240MHz"
#endif
#define CPU_HZ 240000000u
#define SAMPLE_CYCLES (CPU_HZ/E32_TX_PCM_HZ)
#define RING_MASK (E32_TX_RING_SAMPLES-1u)
#define REARM_GUARD_CYCLES 336
#define MAX_CATCHUP_SAMPLES 8u
#define MIN_PULSE_CYCLES 96
#define TONE_WRITE_CYCLES 48
#define EXIT_CYCLES 336
#define MAX_PULSE_CYCLES 1020

_Static_assert(CPU_HZ/E32_TX_PCM_HZ==2000 && CPU_HZ%E32_TX_PCM_HZ==0,"Exact integer MPX clock required");
_Static_assert((E32_TX_RING_SAMPLES&RING_MASK)==0,"Power-of-two PCM ring");

typedef struct {uint16_t nearest_k;int16_t pulse_q16;} mpx_level_t;
static DRAM_ATTR mpx_level_t levels[256];
static DRAM_ATTR uint8_t pcm_ring[E32_TX_RING_SAMPLES];
static DRAM_ATTR e32_tx_stats_t counters;
static volatile uint32_t write_head,read_tail;
static volatile bool armed,running,end_seen;
static bool initialized,protected_stream;
static const char *last_fault;
static int32_t phase_debt;
static DRAM_ATTR uint32_t pulse_random;
static unsigned last_k;

static inline __attribute__((always_inline)) uint32_t cycles(void){return esp_cpu_get_cycle_count();}
static inline __attribute__((always_inline)) void fence(void){asm volatile("memw":::"memory");}
static inline __attribute__((always_inline)) uint32_t magnitude(int32_t x){return x<0?(uint32_t)-x:(uint32_t)x;}

bool e32_tx_init(void) {
    if(initialized)return true;
    const int64_t denominator=127LL*E32_TX_NCO_STEP_HZ_X2;
    for(unsigned b=0;b<256u;++b) {
        const int32_t sample=(int8_t)b;
        const int64_t scaled=(int64_t)sample*2LL*50000LL*65536LL;
        const int32_t q=E32_TX_NCO_CARRIER_CODE*65536+
            (int32_t)((scaled+(scaled>=0?denominator/2:-denominator/2))/denominator);
        const int32_t nearest=(q+32768)>>16;
        levels[b]=(mpx_level_t){(uint16_t)nearest,(int16_t)(q-nearest*65536)};
    }
    initialized=true;return true;
}

bool e32_tx_start(uint32_t carrier_hz,uint8_t apwr) {
    if(armed || running || apwr>191u || carrier_hz<2401000000u || carrier_hz>2482500000u)return false;
    if(!e32_tx_init() || !e32_tx_radio_prepare(carrier_hz,apwr))return false;
    memset(&counters,0,sizeof(counters));counters.min_queue=E32_TX_RING_SAMPLES;
    write_head=read_tail=0;phase_debt=0;last_k=UINT32_MAX;
    pulse_random=cycles()|1u;
    end_seen=false;protected_stream=false;last_fault=NULL;armed=true;return true;
}

size_t IRAM_ATTR e32_tx_queued_samples(void){return write_head-read_tail;}
size_t IRAM_ATTR e32_tx_free_samples(void){return E32_TX_RING_SAMPLES-(write_head-read_tail);}
bool IRAM_ATTR e32_tx_is_active(void){return armed;}
const char *e32_tx_fault(void){return last_fault;}
void IRAM_ATTR e32_tx_set_protected(bool value){protected_stream=value;}

bool IRAM_ATTR e32_tx_stage(uint16_t offset,uint8_t sample) {
    if(!armed || end_seen || offset>=e32_tx_free_samples())return false;
    pcm_ring[(write_head+offset)&RING_MASK]=sample==0x80u?0:sample;return true;
}
bool IRAM_ATTR e32_tx_commit(size_t count) {
    if(!armed || end_seen || count>e32_tx_free_samples())return false;
    fence();write_head+=(uint32_t)count;
    const uint32_t queued=write_head-read_tail;
    if(queued>counters.max_queue)counters.max_queue=queued;
    return true;
}
size_t IRAM_ATTR e32_tx_feed(const uint8_t *samples,size_t count) {
    if(!samples || !armed || end_seen)return 0;
    const size_t available=e32_tx_free_samples(),accepted=count<available?count:available;
    for(size_t n=0;n<accepted;++n)pcm_ring[(write_head+n)&RING_MASK]=samples[n]==0x80u?0:samples[n];
    if(accepted<count)counters.ring_drops+=(uint32_t)(count-accepted);
    (void)e32_tx_commit(accepted);return accepted;
}
void IRAM_ATTR e32_tx_finish(void){end_seen=true;}
void IRAM_ATTR e32_tx_stop(void) {
    armed=false;
    /* A streaming callback must never invoke slow calibration/PHY cleanup. */
    if(!running)e32_tx_radio_release();
}
void e32_tx_get_stats(e32_tx_stats_t *stats){if(stats)*stats=counters;}

static inline __attribute__((always_inline)) void wait_until(uint32_t deadline,void (*pump)(void*),void *user) {
    while(armed && (int32_t)(cycles()-deadline)<0)pump(user);
}

static inline __attribute__((always_inline)) bool emit_minimum_pulse(uint32_t width) {
    uint32_t random=pulse_random;
    random^=random<<13;
    random^=random>>17;
    random^=random<<5;
    pulse_random=random;
    return (random&0xffffu)*MIN_PULSE_CYCLES<(width<<16);
}

static void IRAM_ATTR emit_mpx(uint8_t sample,uint32_t deadline,uint32_t period,void (*pump)(void*),void *user) {
    const mpx_level_t level=levels[sample];
    if(last_k!=level.nearest_k){e32_tx_radio_write_k(level.nearest_k);last_k=level.nearest_k;}
    const int32_t target=(int32_t)level.pulse_q16*(int32_t)period;
    int32_t area=((target+(target>=0?32768:-32768))/65536)+phase_debt;
    const bool up=area>=0;
    int32_t width=(int32_t)magnitude(area);
    // Randomize sub-minimum corrections to break patterns coherent with the
    // stereo pilot. Subtract only the correction actually emitted below.
    if(width>0 && width<MIN_PULSE_CYCLES && emit_minimum_pulse((uint32_t)width))
        width=MIN_PULSE_CYCLES;
    if(width>=MIN_PULSE_CYCLES) {
        if(width>MAX_PULSE_CYCLES)width=MAX_PULSE_CYCLES;
        const uint32_t start=deadline-(period+(uint32_t)width)/2u;
        wait_until(start,pump,user);
        if(!armed)return;
        const int32_t room=(int32_t)(deadline-cycles())-EXIT_CYCLES-TONE_WRITE_CYCLES;
        if(width>room)width=room;
        if(width>=MIN_PULSE_CYCLES) {
            e32_tx_radio_write_k((unsigned)((int)level.nearest_k+(up?1:-1)));
            const uint32_t pulse_start=cycles();
            const uint32_t stop=pulse_start+(uint32_t)(width-TONE_WRITE_CYCLES);
            /* Holding the small centered pulse is the only interval without
             * a UART pump; the FIFO buffers its<4.25us duration. */
            while((int32_t)(cycles()-stop)<0){}
            e32_tx_radio_write_k(level.nearest_k);
            const int32_t actual=(int32_t)(cycles()-pulse_start);
            area+=up?-actual:actual;
        }
    }
    if(area>(int32_t)period)area=(int32_t)period;
    else if(area<-(int32_t)period)area=-(int32_t)period;
    phase_debt=area;
}

void IRAM_ATTR e32_tx_run(void (*pump)(void*),void *user) {
    if(!armed || running || !pump)return;
    const uint32_t pending=write_head-read_tail;
    if(pending<E32_TX_START_LEVEL && !end_seen)return;
    if(!pending){armed=false;e32_tx_radio_release();return;}
    running=true;
    const uint32_t startup=cycles();
    if(!e32_tx_radio_key()) {
        last_fault="TX_RF";armed=false;
    }
    counters.startup_cycles=cycles()-startup;
    /* Native setup above can lock the Wi-Fi CPU; park it only afterwards.
     * At240MHz every120kHz MPX interval is exactly2000 cycles. */
    const uint32_t irq_state=e32_stream_enter();
    uint32_t period=SAMPLE_CYCLES;
    uint32_t sample_start=cycles(),deadline=sample_start+period;
    while(armed) {
        uint32_t consume=1u;
        const uint32_t now=cycles();
        const int32_t boundary_late=(int32_t)(now-sample_start);
        if(boundary_late>0 && (uint32_t)boundary_late>counters.max_late_cycles)
            counters.max_late_cycles=(uint32_t)boundary_late;
        if(boundary_late>(int32_t)(SAMPLE_CYCLES/4u))++counters.late_slots;
        while((int32_t)(deadline-now)<REARM_GUARD_CYCLES) {
            if(consume>=MAX_CATCHUP_SAMPLES){last_fault="TX_TIMING";armed=false;break;}
            ++consume;sample_start=deadline;period=SAMPLE_CYCLES;
            deadline+=period;
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
        emit_mpx(sample,deadline,period,pump,user);++counters.samples;
        wait_until(deadline,pump,user);
        sample_start=deadline;period=SAMPLE_CYCLES;
        deadline+=period;
    }
    e32_tx_radio_gate_off();
    e32_stream_leave(irq_state);
    e32_tx_radio_off();
    running=false;armed=false;
    e32_tx_radio_release();
}
