/* SPDX-License-Identifier: GPL-3.0-or-later
 * Native C5 pulse-area FM at 120000 samples/s on a 240 MHz CPU.
 * PCM12 schedules two independent mono pulse widths in each sample slot.
 * The legacy MPX command retains the original shared pulse schedule.
 */
#include "c5_tx.h"
#include "c5_tx_radio.h"
#include "c5_bands.h"
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
#define MONO_MIN_PULSE_CYCLES 80
#define TONE_WRITE_CYCLES 64
#define MONO_EDGE_GUARD_CYCLES 32u
#define MONO_MIN_PUMP_CYCLES 200
#define MONO_CENTER_ADVANCE_CYCLES 192u
#define EXIT_CYCLES 336
#define MAX_PULSE_CYCLES ((int32_t)(SAMPLE_CYCLES*51u/100u))
#define MONO_DEVIATION_HZ 100000LL
#define MONO_AREA_BITS 9u
#define MONO_AREA_SCALE (1u << MONO_AREA_BITS)
#define MONO_STEP_AREA ((int32_t)(SAMPLE_CYCLES * MONO_AREA_SCALE))
#define MONO_SLOPE_BITS 9u
#define MONO_SLOPE_ROUND (1 << (MONO_SLOPE_BITS - 1u))
#define MONO_AREA_DENOMINATOR ((int64_t)C5_TX_NCO_STEP_HZ_X2 * C5_TX_PCM_Q4_FULL_SCALE)
#define MONO_AREA_SLOPE ((2 * MONO_DEVIATION_HZ * MONO_STEP_AREA * (1LL << MONO_SLOPE_BITS) + \
                         MONO_AREA_DENOMINATOR / 2) / MONO_AREA_DENOMINATOR)

_Static_assert(CPU_HZ%C5_TX_PCM_HZ==0,"Exact MPX clock required");
_Static_assert((C5_TX_RING_SAMPLES&RING_MASK)==0,"Power-of-two PCM ring");
_Static_assert(MONO_AREA_SLOPE * C5_TX_PCM_Q4_FULL_SCALE + MONO_SLOPE_ROUND <= INT32_MAX,
               "PCM-to-area product must fit the fast signed 32-bit multiply");
_Static_assert((uint64_t)MONO_MIN_PULSE_CYCLES << (MONO_AREA_BITS + 16) <= UINT32_MAX,
               "Small-pulse probability must fit an unsigned 32-bit multiply");
_Static_assert(MONO_MIN_PULSE_CYCLES > TONE_WRITE_CYCLES,
               "Short mono pulse must allow the tone write");
_Static_assert(SAMPLE_CYCLES/2u + MONO_CENTER_ADVANCE_CYCLES - (MAX_PULSE_CYCLES+1u)/2u >= 680u,
               "Dual mono must leave a continuous UART/DSP window after both pulses");
_Static_assert(4 * MONO_DEVIATION_HZ < 7 * (int64_t)C5_TX_NCO_STEP_HZ_X2,
               "Full-scale mono must fit within three steps of the centre");

typedef struct {uint16_t nearest_k;int16_t pulse_cycles;} mpx_level_t;
typedef struct {int16_t right,left;} mono_sample_t;
typedef struct {int32_t phase_debt;uint32_t random;} mono_state_t;
typedef struct {unsigned base,pulse;int32_t area;uint32_t width;} mono_level_t;
static DRAM_ATTR mpx_level_t levels[256];
static DRAM_ATTR union {
    uint8_t legacy[C5_TX_RING_SAMPLES];
    mono_sample_t mono[C5_TX_RING_SAMPLES];
} pcm_ring;
static DRAM_ATTR c5_tx_stats_t counters;
static volatile uint32_t write_head,read_tail;
static volatile bool armed,running,end_seen;
static bool initialized,protected_stream,pcm12_mode;
static const char *last_fault;
static int32_t phase_debt;
static uint32_t pulse_random;
static DRAM_ATTR mono_state_t right_state,left_state;
static uint32_t pump_deadline;
static unsigned last_k;

static inline __attribute__((always_inline)) uint32_t cycles(void){return esp_cpu_get_cycle_count();}
static inline __attribute__((always_inline)) void fence(void){asm volatile("fence rw,rw":::"memory");}
static inline __attribute__((always_inline)) uint32_t magnitude(int32_t x){return x<0?(uint32_t)-x:(uint32_t)x;}

bool c5_tx_init(void) {
    if(initialized)return true;
    const int64_t denominator=127LL*C5_TX_NCO_STEP_HZ_X2;
    for(unsigned b=0;b<256u;++b) {
        const int32_t sample=(int8_t)b;
        const int64_t scaled=(int64_t)sample*2LL*50000LL*65536LL;
        const int32_t q=C5_TX_NCO_CARRIER_CODE*65536+
            (int32_t)((scaled+(scaled>=0?denominator/2:-denominator/2))/denominator);
        const int32_t nearest=(q+32768)>>16;
        const int32_t area=(q-nearest*65536)*(int32_t)SAMPLE_CYCLES;
        levels[b]=(mpx_level_t){(uint16_t)nearest,(int16_t)((area+(area>=0?32768:-32768))/65536)};
    }
    initialized=true;return true;
}

static bool start_tx(uint64_t carrier_hz,uint8_t apwr,bool pcm12) {
    if(armed || running || !c5_tx_frequency_valid(carrier_hz) || apwr>191u)return false;
    if(!c5_tx_init() || !c5_tx_radio_prepare(carrier_hz,apwr))return false;
    memset(&counters,0,sizeof(counters));counters.min_queue=C5_TX_RING_SAMPLES;
    write_head=read_tail=0;phase_debt=0;last_k=UINT32_MAX;
    pulse_random=cycles()|1u;
    right_state=(mono_state_t){0,pulse_random};
    left_state=(mono_state_t){0,pulse_random^UINT32_C(0x9e3779b8)};
    end_seen=false;protected_stream=false;last_fault=NULL;pcm12_mode=pcm12;armed=true;return true;
}
bool c5_tx_start(uint64_t carrier_hz,uint8_t apwr){return start_tx(carrier_hz,apwr,false);}
bool c5_tx_start_pcm(uint64_t carrier_hz,uint8_t apwr){return start_tx(carrier_hz,apwr,true);}

size_t IRAM_ATTR c5_tx_queued_samples(void){return write_head-read_tail;}
size_t IRAM_ATTR c5_tx_free_samples(void){return C5_TX_RING_SAMPLES-(write_head-read_tail);}
bool IRAM_ATTR c5_tx_is_active(void){return armed;}
const char *c5_tx_fault(void){return last_fault;}
void IRAM_ATTR c5_tx_set_protected(bool value){protected_stream=value;}

bool IRAM_ATTR c5_tx_stage(uint16_t offset,uint8_t sample) {
    if(pcm12_mode || !armed || end_seen || offset>=c5_tx_free_samples())return false;
    pcm_ring.legacy[(write_head+offset)&RING_MASK]=sample==0x80u?0:sample;return true;
}
bool IRAM_ATTR c5_tx_commit(size_t count) {
    if(!armed || end_seen || count>c5_tx_free_samples())return false;
    fence();write_head+=(uint32_t)count;
    const uint32_t queued=write_head-read_tail;
    if(queued>counters.max_queue)counters.max_queue=queued;
    return true;
}
size_t IRAM_ATTR c5_tx_feed(const uint8_t *samples,size_t count) {
    if(pcm12_mode || !samples || !armed || end_seen)return 0;
    const size_t available=c5_tx_free_samples(),accepted=count<available?count:available;
    for(size_t n=0;n<accepted;++n)pcm_ring.legacy[(write_head+n)&RING_MASK]=samples[n]==0x80u?0:samples[n];
    if(accepted<count)counters.ring_drops+=(uint32_t)(count-accepted);
    (void)c5_tx_commit(accepted);return accepted;
}
bool IRAM_ATTR c5_tx_enqueue_mono_q4(int16_t right,int16_t left) {
    if (!pcm12_mode || !armed || end_seen) return false;
    const uint32_t head = write_head;
    const uint32_t queued = head - read_tail;
    if (queued >= C5_TX_RING_SAMPLES) {
        ++counters.ring_drops;
        return false;
    }
    // The resampler clamps both values and runs on the same core as RF.
    pcm_ring.mono[head & RING_MASK] = (mono_sample_t){right,left};
    write_head = head + 1;
    if (queued + 1 > counters.max_queue) counters.max_queue = queued + 1;
    return true;
}
uint32_t IRAM_ATTR c5_tx_pcm_played(void){return pcm12_mode?read_tail:0;}
uint32_t IRAM_ATTR c5_tx_pcm_pump_budget(void) {
    if(!running)return UINT32_MAX;
    const int32_t remaining=(int32_t)(pump_deadline-cycles());
    return remaining>0?(uint32_t)remaining:0;
}
void IRAM_ATTR c5_tx_finish(void){end_seen=true;}
void IRAM_ATTR c5_tx_stop(void) {
    armed=false;
    /* A streaming callback must never invoke slow calibration/PHY cleanup. */
    if(!running)c5_tx_radio_release();
}
void c5_tx_get_stats(c5_tx_stats_t *stats){if(stats)*stats=counters;}

static inline __attribute__((always_inline)) void wait_until(uint32_t deadline,void (*pump)(void*),void *user) {
    pump_deadline=deadline;
    const int32_t minimum=pcm12_mode?MONO_MIN_PUMP_CYCLES:0;
    while(armed && (int32_t)(deadline-cycles())>minimum)pump(user);
    while(armed && (int32_t)(deadline-cycles())>0){}
}

static inline __attribute__((always_inline)) bool emit_minimum_pulse(uint32_t width,uint32_t minimum_area,
                                                                   uint32_t *random_state) {
    uint32_t random=*random_state;
    random^=random<<13;
    random^=random>>17;
    random^=random<<5;
    *random_state=random;
    return (random&0xffffu)*minimum_area<(width<<16);
}

static inline __attribute__((always_inline)) void write_legacy(unsigned k) {
    const unsigned secondary=(unsigned)(2*(int)k-C5_TX_NCO_CARRIER_CODE);
    c5_tx_radio_write_pair(k,secondary);
}

static void IRAM_ATTR emit_mpx(uint8_t sample,uint32_t deadline,void (*pump)(void*),void *user) {
    const mpx_level_t level=levels[sample];
    const unsigned nearest_k=level.nearest_k;
    if(last_k!=nearest_k){write_legacy(nearest_k);last_k=nearest_k;}
    int32_t area=level.pulse_cycles+phase_debt;
    const bool up=area>=0;
    const uint32_t magnitude_area=magnitude(area);
    int32_t width;
    // Small corrections alternate between zero and the shortest usable
    // pulse. Randomize that decision to break patterns coherent with the
    // stereo pilot. Subtract only the correction actually emitted below.
    if(magnitude_area<MIN_PULSE_CYCLES)
        width=magnitude_area && emit_minimum_pulse(magnitude_area,MIN_PULSE_CYCLES,&pulse_random)?MIN_PULSE_CYCLES:0;
    else width=(int32_t)magnitude_area;
    if(width>=MIN_PULSE_CYCLES) {
        if(width>MAX_PULSE_CYCLES)width=MAX_PULSE_CYCLES;
        const uint32_t start=deadline-(SAMPLE_CYCLES+(uint32_t)width)/2u;
        wait_until(start,pump,user);
        if(!armed)return;
        const int32_t room=(int32_t)(deadline-cycles())-EXIT_CYCLES-TONE_WRITE_CYCLES;
        if(width>room)width=room;
        if(width>=MIN_PULSE_CYCLES) {
            write_legacy((unsigned)((int)nearest_k+(up?1:-1)));
            const uint32_t pulse_start=cycles();
            const uint32_t stop=pulse_start+(uint32_t)(width-TONE_WRITE_CYCLES);
            /* Holding the small centered pulse is the only interval without
             * a UART pump; the FIFO buffers its<4.25us duration. */
            while((int32_t)(cycles()-stop)<0){}
            write_legacy(nearest_k);
            const int32_t actual=(int32_t)(cycles()-pulse_start);
            area+=up?-actual:actual;
        }
    }
    const int32_t limit=(int32_t)SAMPLE_CYCLES;
    if(area>limit)area=limit;
    else if(area<-limit)area=-limit;
    phase_debt=area;
}

static inline __attribute__((always_inline)) mono_level_t mono_level(int16_t sample,mono_state_t *state) {
    const int32_t target=((int32_t)sample*(int32_t)MONO_AREA_SLOPE+MONO_SLOPE_ROUND)>>MONO_SLOPE_BITS;
    const int32_t absolute=(int32_t)magnitude(target);
    const int32_t steps=absolute>=5*MONO_STEP_AREA/2?3:
                        absolute>=3*MONO_STEP_AREA/2?2:
                        absolute>=MONO_STEP_AREA/2?1:0;
    const int32_t offset=target<0?-steps:steps;
    const int32_t base=C5_TX_NCO_CARRIER_CODE+offset;
    const int32_t area=target-offset*MONO_STEP_AREA+state->phase_debt;
    const uint32_t minimum_area=MONO_MIN_PULSE_CYCLES*MONO_AREA_SCALE;
    const uint32_t distance=magnitude(area);
    uint32_t width;
    if(distance<minimum_area)
        width=distance && emit_minimum_pulse(distance,minimum_area,&state->random)?MONO_MIN_PULSE_CYCLES:0;
    else width=(distance+MONO_AREA_SCALE/2u)>>MONO_AREA_BITS;
    if(width>(uint32_t)MAX_PULSE_CYCLES)width=MAX_PULSE_CYCLES;
    return (mono_level_t){(unsigned)base,(unsigned)(base+(area<0?-1:1)),area,width};
}

static inline __attribute__((always_inline)) void settle_mono(mono_state_t *state,mono_level_t level,
                                                              uint32_t observed) {
    const int32_t correction=(int32_t)(observed*MONO_AREA_SCALE);
    int32_t debt=level.area+(level.area<0?correction:-correction);
    if(debt>MONO_STEP_AREA)debt=MONO_STEP_AREA;
    else if(debt<-MONO_STEP_AREA)debt=-MONO_STEP_AREA;
    state->phase_debt=debt;
}

static inline __attribute__((always_inline)) void wait_mono_edge(uint32_t edge,void (*pump)(void*),void *user) {
    wait_until(edge-MONO_EDGE_GUARD_CYCLES,pump,user);
    while(armed && (int32_t)(cycles()-edge)<0){}
}

static void IRAM_ATTR emit_dual_mono(mono_sample_t sample,uint32_t deadline,void (*pump)(void*),void *user) {
    const mono_level_t right=mono_level(sample.right,&right_state);
    const mono_level_t left=mono_level(sample.left,&left_state);
    c5_tx_radio_write_pair(right.base,left.base);

    // Both pulses are centred in this sample slot. The shorter one is
    // nested inside the longer one, so no general-purpose event sort is needed.
    const bool right_outer=right.width>=left.width;
    const uint32_t outer=right_outer?right.width:left.width;
    const uint32_t inner=right_outer?left.width:right.width;
    uint32_t outer_elapsed=0,inner_elapsed=0;
    if(outer) {
        // Nearly coincident edges share a register update. Account for the
        // common duration separately in each channel's phase-error feedback.
        const bool merge=inner && outer-inner<2u*TONE_WRITE_CYCLES;
        const uint32_t width=merge?(outer+inner+1u)/2u:outer;
        const unsigned outer_right=right_outer || merge?right.pulse:right.base;
        const unsigned outer_left=!right_outer || merge?left.pulse:left.base;
        // Give both channels the same fixed phase advance. The longest
        // possible pulse still leaves 682 cycles for the producer and UART.
        const uint32_t middle=deadline-SAMPLE_CYCLES/2u-MONO_CENTER_ADVANCE_CYCLES;
        const uint32_t outer_begin=middle-width/2u;
        const uint32_t outer_end=outer_begin+width;
        wait_mono_edge(outer_begin,pump,user);
        if(!armed)return;
        if((int32_t)(outer_end-cycles())>=TONE_WRITE_CYCLES) {
            c5_tx_radio_write_pair(outer_right,outer_left);
            const uint32_t outer_started=cycles();
            if(inner && !merge) {
                const uint32_t inner_begin=middle-inner/2u;
                const uint32_t inner_end=inner_begin+inner;
                wait_mono_edge(inner_begin,pump,user);
                if(!armed)return;
                if((int32_t)(inner_end-cycles())>=TONE_WRITE_CYCLES &&
                   (int32_t)(outer_end-inner_end)>=TONE_WRITE_CYCLES) {
                    c5_tx_radio_write_pair(right.pulse,left.pulse);
                    const uint32_t inner_started=cycles();
                    wait_mono_edge(inner_end,pump,user);
                    if(!armed)return;
                    c5_tx_radio_write_pair(outer_right,outer_left);
                    inner_elapsed=cycles()-inner_started;
                }
            }
            wait_mono_edge(outer_end,pump,user);
            if(!armed)return;
            c5_tx_radio_write_pair(right.base,left.base);
            outer_elapsed=cycles()-outer_started;
            if(merge)inner_elapsed=outer_elapsed;
        }
    }
    settle_mono(&right_state,right,right_outer?outer_elapsed:inner_elapsed);
    settle_mono(&left_state,left,right_outer?inner_elapsed:outer_elapsed);
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
        mono_sample_t mono={0,0};
        if(queued>=consume){
            const uint32_t offset=(read_tail+consume-1u)&RING_MASK;
            if(pcm12_mode){mono=pcm_ring.mono[offset];counters.pcm_tail_underruns=0;}
            else sample=pcm_ring.legacy[offset];
            read_tail+=consume;
        }
        else {
            if(end_seen)break;
            ++counters.underruns;++counters.zero_samples;
            if(pcm12_mode)++counters.pcm_tail_underruns;
            if(protected_stream){last_fault="TX_UNDERRUN";armed=false;break;}
        }
        if(pcm12_mode)emit_dual_mono(mono,deadline,pump,user);
        else emit_mpx(sample,deadline,pump,user);
        ++counters.samples;
        wait_until(deadline,pump,user);
        deadline+=SAMPLE_CYCLES;
    }
    c5_tx_radio_off();
    RV_SET_CSR(mstatus,irq_state&MSTATUS_MIE);
    running=false;armed=false;
    c5_tx_radio_release();
}
