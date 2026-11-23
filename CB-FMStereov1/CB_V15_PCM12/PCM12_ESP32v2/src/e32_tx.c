/* SPDX-License-Identifier: GPL-3.0-or-later
 * CB13's centered native-NCO pulse-area FM at120000 MPX/s, CPU240MHz.
 * Adapted from ESP-AnalogRadio CB_FM_STEREOv13 fm_tx.h. Maintaining its
 * pulse-area modulation avoids adding a120kHz frequency ZOH's high-MPX
 * attenuation to the19k pilot, stereo subcarrier and57k RDS signal.
 */
#include "e32_tx.h"
#include "e32_pcm_worker.h"
#include "e32_dram_cursor.h"
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
#ifndef E32_TX_MPX_MIN_CYCLES
#define E32_TX_MPX_MIN_CYCLES 96
#endif
/* Account for the second NCO write in the pulse budget. */
#define TONE_WRITE_CYCLES 64
#define EXIT_CYCLES 336
#define MAX_PULSE_CYCLES 1020
#define FM_DEVIATION_HZ 75000LL
#define MPX_AREA_BITS 9u
#define MPX_AREA_SCALE (1u << MPX_AREA_BITS)
#define MPX_SLOPE_BITS 10u
#define MPX_SLOPE_ROUND (1 << (MPX_SLOPE_BITS - 1u))
#define MPX_AREA_DENOMINATOR ((int64_t)E32_TX_NCO_STEP_HZ_X2 * E32_TX_MPX_Q4_FULL_SCALE)
#define MPX_AREA_SLOPE ((SAMPLE_CYCLES * MPX_AREA_SCALE * 2 * FM_DEVIATION_HZ * (1LL << MPX_SLOPE_BITS) + \
                         MPX_AREA_DENOMINATOR / 2) / MPX_AREA_DENOMINATOR)

_Static_assert(CPU_HZ/E32_TX_PCM_HZ==2000 && CPU_HZ%E32_TX_PCM_HZ==0,"Exact integer MPX clock required");
_Static_assert((E32_TX_RING_SAMPLES&RING_MASK)==0,"Power-of-two PCM ring");
_Static_assert(MPX_AREA_SLOPE * E32_TX_MPX_Q4_FULL_SCALE + MPX_SLOPE_ROUND <= INT32_MAX,
               "MPX-to-area product must fit the signed 32-bit multiply");
_Static_assert((uint64_t)MIN_PULSE_CYCLES << (MPX_AREA_BITS + 16u) <= UINT32_MAX,
               "Pulse probability must fit an unsigned 32-bit multiply");
_Static_assert(E32_TX_MPX_MIN_CYCLES > TONE_WRITE_CYCLES && E32_TX_MPX_MIN_CYCLES <= MIN_PULSE_CYCLES,
               "Minimum MPX pulse must allow the RF write and fit the probability bound");

typedef struct {uint16_t nearest_k;int16_t pulse_q16;} mpx_level_t;
static DRAM_ATTR mpx_level_t levels[256];
static DRAM_ATTR union {
    uint8_t legacy[E32_TX_RING_SAMPLES];
    int16_t mpx_q4[E32_TX_RING_SAMPLES];
} pcm_ring;
static DRAM_ATTR e32_tx_stats_t counters;
static DRAM_ATTR volatile uint32_t write_head,read_tail,armed,end_seen;
static bool initialized,protected_stream,pcm12_mode,running,using_worker,last_used_worker;
static const char *last_fault;
static int32_t phase_debt;
static DRAM_ATTR uint32_t pulse_random;
static uint32_t pump_deadline;
static unsigned last_k;

static inline __attribute__((always_inline)) uint32_t cycles(void){return esp_cpu_get_cycle_count();}
static inline __attribute__((always_inline)) uint32_t load(const volatile uint32_t *slot){return e32_dram_load(slot);}
static inline __attribute__((always_inline)) void store(volatile uint32_t *slot,uint32_t value){e32_dram_store(slot,value);}
static inline __attribute__((always_inline)) uint32_t magnitude(int32_t x){return x<0?(uint32_t)-x:(uint32_t)x;}

bool e32_tx_init(void) {
    if(initialized)return true;
    const int64_t denominator=127LL*E32_TX_NCO_STEP_HZ_X2;
    for(unsigned b=0;b<256u;++b) {
        const int32_t sample=(int8_t)b;
        const int64_t scaled=(int64_t)sample*2*FM_DEVIATION_HZ*65536LL;
        const int32_t q=E32_TX_NCO_CARRIER_CODE*65536+
            (int32_t)((scaled+(scaled>=0?denominator/2:-denominator/2))/denominator);
        const int32_t nearest=(q+32768)>>16;
        levels[b]=(mpx_level_t){(uint16_t)nearest,(int16_t)(q-nearest*65536)};
    }
    initialized=true;return true;
}

static bool start_tx(uint32_t carrier_hz,uint8_t apwr,bool pcm12) {
    if(load(&armed) || running || apwr>191u || carrier_hz<2401000000u || carrier_hz>2482500000u)return false;
    if(!e32_tx_init() || !e32_tx_radio_prepare(carrier_hz,apwr))return false;
    memset(&counters,0,sizeof(counters));counters.min_queue=E32_TX_RING_SAMPLES;
    store(&write_head,0);store(&read_tail,0);phase_debt=0;last_k=UINT32_MAX;
    pulse_random=cycles()|1u;
    store(&end_seen,0);protected_stream=false;pcm12_mode=pcm12;last_fault=NULL;last_used_worker=false;
    store(&armed,1);return true;
}
bool e32_tx_start(uint32_t carrier_hz,uint8_t apwr){return start_tx(carrier_hz,apwr,false);}
bool e32_tx_start_pcm(uint32_t carrier_hz,uint8_t apwr){return start_tx(carrier_hz,apwr,true);}

size_t IRAM_ATTR e32_tx_queued_samples(void){return load(&write_head)-load(&read_tail);}
size_t IRAM_ATTR e32_tx_free_samples(void){return E32_TX_RING_SAMPLES-(load(&write_head)-load(&read_tail));}
bool IRAM_ATTR e32_tx_is_active(void){return load(&armed)!=0;}
bool IRAM_ATTR e32_tx_uses_worker(void){return using_worker;}
bool e32_tx_last_used_worker(void){return last_used_worker;}
const char *e32_tx_fault(void){return last_fault;}
void IRAM_ATTR e32_tx_set_protected(bool value){protected_stream=value;}

bool IRAM_ATTR e32_tx_stage(uint16_t offset,uint8_t sample) {
    if(pcm12_mode || !load(&armed) || load(&end_seen) || offset>=e32_tx_free_samples())return false;
    pcm_ring.legacy[(load(&write_head)+offset)&RING_MASK]=sample==0x80u?0:sample;return true;
}
bool IRAM_ATTR e32_tx_commit(size_t count) {
    if(!load(&armed) || load(&end_seen) || count>e32_tx_free_samples())return false;
    const uint32_t next=load(&write_head)+(uint32_t)count;
    store(&write_head,next);
    const uint32_t queued=next-load(&read_tail);
    if(queued>counters.max_queue)counters.max_queue=queued;
    return true;
}
size_t IRAM_ATTR e32_tx_feed(const uint8_t *samples,size_t count) {
    if(pcm12_mode || !samples || !load(&armed) || load(&end_seen))return 0;
    const size_t available=e32_tx_free_samples(),accepted=count<available?count:available;
    const uint32_t head=load(&write_head);
    for(size_t n=0;n<accepted;++n)pcm_ring.legacy[(head+n)&RING_MASK]=samples[n]==0x80u?0:samples[n];
    if(accepted<count)counters.ring_drops+=(uint32_t)(count-accepted);
    (void)e32_tx_commit(accepted);return accepted;
}
bool IRAM_ATTR e32_tx_enqueue_mpx_q4(int16_t sample) {
    if (!pcm12_mode || !load(&armed) || load(&end_seen)) return false;
    const uint32_t position = load(&write_head);
    const uint32_t queued = position - load(&read_tail);
    if (queued >= E32_TX_RING_SAMPLES) {
        ++counters.ring_drops;
        return false;
    }
    pcm_ring.mpx_q4[position & RING_MASK] = sample;
    // Publish the sample to the RF core only after writing the ring slot.
    store(&write_head, position + 1);
    if (queued + 1 > counters.max_queue) counters.max_queue = queued + 1;
    return true;
}
uint32_t IRAM_ATTR e32_tx_pcm_played(void){return pcm12_mode?load(&read_tail):0;}
uint32_t IRAM_ATTR e32_tx_pcm_pump_budget(void) {
    if(!running)return UINT32_MAX;
    const int32_t remaining=(int32_t)(pump_deadline-cycles());
    return remaining>0?(uint32_t)remaining:0;
}
void IRAM_ATTR e32_tx_finish(void){store(&end_seen,1);}
void IRAM_ATTR e32_tx_stop(void) {
    store(&armed,0);
    /* A streaming callback must never invoke slow calibration/PHY cleanup. */
    if(!running)e32_tx_radio_release();
}
void e32_tx_get_stats(e32_tx_stats_t *stats){if(stats)*stats=counters;}

static inline __attribute__((always_inline)) void wait_until(uint32_t deadline,void (*pump)(void*),void *user) {
    pump_deadline=deadline;
    while(load(&armed) && (int32_t)(cycles()-deadline)<0)pump(user);
}

static inline __attribute__((always_inline)) bool emit_minimum_pulse(uint32_t area, uint32_t minimum_area) {
    uint32_t random=pulse_random;
    random^=random<<13;
    random^=random>>17;
    random^=random<<5;
    pulse_random=random;
    return (random & 0xffffu) * minimum_area < (area << 16);
}

static inline __attribute__((always_inline)) void emit_level(unsigned nearest_k,int32_t target,
    unsigned fraction_bits,uint32_t deadline,void (*pump)(void*),void *user) {
    if(last_k!=nearest_k){e32_tx_radio_write_k(nearest_k);last_k=nearest_k;}
    int32_t area=target+phase_debt;
    const bool up=area>=0;
    const uint32_t magnitude_area=magnitude(area);
    const int32_t minimum = fraction_bits ? E32_TX_MPX_MIN_CYCLES : MIN_PULSE_CYCLES;
    const uint32_t minimum_area = (uint32_t)minimum << fraction_bits;
    int32_t width;
    // Randomize sub-minimum corrections to break patterns coherent with the
    // stereo pilot. Subtract only the correction actually emitted below.
    if(magnitude_area<minimum_area)
        width=magnitude_area && emit_minimum_pulse(magnitude_area,minimum_area)?minimum:0;
    else width=(magnitude_area+((1u<<fraction_bits)/2u))>>fraction_bits;
    if(width>=minimum) {
        if(width>MAX_PULSE_CYCLES)width=MAX_PULSE_CYCLES;
        const uint32_t start=deadline-(SAMPLE_CYCLES+(uint32_t)width)/2u;
        wait_until(start,pump,user);
        if(!load(&armed))return;
        const int32_t room=(int32_t)(deadline-cycles())-EXIT_CYCLES-TONE_WRITE_CYCLES;
        if(width>room)width=room;
        if(width>=minimum) {
            e32_tx_radio_write_k((unsigned)((int)nearest_k+(up?1:-1)));
            const uint32_t pulse_start=cycles();
            const uint32_t stop=pulse_start+(uint32_t)(width-TONE_WRITE_CYCLES);
            /* Holding the small centered pulse is the only interval without
             * a UART pump; the FIFO buffers its<4.25us duration. */
            while((int32_t)(cycles()-stop)<0){}
            e32_tx_radio_write_k(nearest_k);
            const int32_t actual=(int32_t)((cycles()-pulse_start)<<fraction_bits);
            area+=up?-actual:actual;
        }
    }
    const int32_t limit=(int32_t)(SAMPLE_CYCLES<<fraction_bits);
    if(area>limit)area=limit;
    else if(area<-limit)area=-limit;
    phase_debt=area;
}

static void IRAM_ATTR emit_mpx(uint8_t sample,uint32_t deadline,void (*pump)(void*),void *user) {
    const mpx_level_t level=levels[sample];
    const int32_t target=(int32_t)level.pulse_q16*(int32_t)SAMPLE_CYCLES;
    emit_level(level.nearest_k,(target+(target>=0?32768:-32768))/65536,0,deadline,pump,user);
}

static void IRAM_ATTR emit_mpx_q4(int16_t sample,uint32_t deadline,void (*pump)(void*),void *user) {
    // Map the Q4 multiplex directly to phase area. Keep the Q9 remainder
    // between slots while scheduling the radio in whole CPU cycles.
    const int32_t step = SAMPLE_CYCLES * MPX_AREA_SCALE;
    const int32_t target = ((int32_t)sample * (int32_t)MPX_AREA_SLOPE + MPX_SLOPE_ROUND) >> MPX_SLOPE_BITS;
    // At 75 kHz the nearest code can be two 39,062.5 Hz steps from center.
    const uint32_t distance=magnitude(target);
    const int32_t steps=distance>(uint32_t)(3*step/2)?2:
                        distance>(uint32_t)(step/2)?1:0;
    const int32_t offset=target<0?-steps:steps;
    const unsigned nearest=(unsigned)(E32_TX_NCO_CARRIER_CODE+offset);
    emit_level(nearest,target-offset*step,MPX_AREA_BITS,deadline,pump,user);
}

void IRAM_ATTR e32_tx_run(void (*pump)(void*),void *user,void (*produce)(void*)) {
    if(!load(&armed) || running || !pump)return;
    const uint32_t pending=load(&write_head)-load(&read_tail);
    if(pending<E32_TX_START_LEVEL && !load(&end_seen))return;
    if(!pending){store(&armed,0);e32_tx_radio_release();return;}
    running=true;
    const uint32_t startup=cycles();
    if(!e32_tx_radio_key()) {
        last_fault="TX_RF";store(&armed,0);
    }
    counters.startup_cycles=cycles()-startup;
    /* Native setup above can lock the Wi-Fi CPU. Only enter the protected
     * session afterwards, once the optional memory-only producer is ready. */
    if(load(&armed) && pcm12_mode) {
        e32_stream_record_start();
        last_used_worker=using_worker=e32_pcm_worker_start(produce,user);
        if(!using_worker) {
            last_fault="TX_WORKER";
            store(&armed,0);
        }
    }
    const uint32_t irq_state=using_worker?e32_tx_mask_local():e32_stream_enter();
    uint32_t period=SAMPLE_CYCLES;
    uint32_t sample_start=cycles(),deadline=sample_start+period;
    while(load(&armed)) {
        uint32_t consume=1u;
        const uint32_t now=cycles();
        const int32_t boundary_late=(int32_t)(now-sample_start);
        if(boundary_late>0 && (uint32_t)boundary_late>counters.max_late_cycles)
            counters.max_late_cycles=(uint32_t)boundary_late;
        if(boundary_late>(int32_t)(SAMPLE_CYCLES/4u))++counters.late_slots;
        while((int32_t)(deadline-now)<REARM_GUARD_CYCLES) {
            if(consume>=MAX_CATCHUP_SAMPLES){last_fault="TX_TIMING";store(&armed,0);break;}
            ++consume;sample_start=deadline;period=SAMPLE_CYCLES;
            deadline+=period;
        }
        if(!load(&armed))break;
        const uint32_t tail=load(&read_tail);
        const uint32_t queued=load(&write_head)-tail;
        if(queued<counters.min_queue)counters.min_queue=queued;
        if(consume>1u)counters.timer_skips+=consume-1u;
        int16_t sample=0;
        if(queued>=consume) {
            const uint32_t offset=(tail+consume-1u)&RING_MASK;
            if(pcm12_mode){sample=pcm_ring.mpx_q4[offset];counters.pcm_tail_underruns=0;}
            else sample=pcm_ring.legacy[offset];
            store(&read_tail,tail+consume);
        }
        else {
            if(load(&end_seen))break;
            ++counters.underruns;++counters.zero_samples;
            if(pcm12_mode)++counters.pcm_tail_underruns;
            if(protected_stream){last_fault="TX_UNDERRUN";store(&armed,0);break;}
        }
        if(pcm12_mode)emit_mpx_q4(sample,deadline,pump,user);
        else emit_mpx((uint8_t)sample,deadline,pump,user);
        ++counters.samples;
        wait_until(deadline,pump,user);
        sample_start=deadline;period=SAMPLE_CYCLES;
        deadline+=period;
    }
    e32_tx_radio_gate_off();
    if(using_worker) {
        const bool rearm_tick=e32_stream_prepare_leave();
        e32_pcm_worker_stop(rearm_tick);
        e32_tx_restore_local(irq_state);
        using_worker=false;
        if(rearm_tick)e32_stream_catchup_ticks();
    } else e32_stream_leave(irq_state);
    e32_tx_radio_off();
    running=false;store(&armed,0);
    e32_tx_radio_release();
}
