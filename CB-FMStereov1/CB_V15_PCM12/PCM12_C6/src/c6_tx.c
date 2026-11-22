/* SPDX-License-Identifier: GPL-3.0-or-later
 * CB13's centered native-NCO pulse-area FM at120000 MPX/s, CPU160MHz.
 * Adapted from ESP-AnalogRadio CB_FM_STEREOv13 fm_tx.h. Maintaining its
 * pulse-area modulation avoids adding a120kHz frequency ZOH's high-MPX
 * attenuation to the19k pilot, stereo subcarrier and57k RDS signal.
 */
#include "c6_tx.h"
#include "c6_tx_radio.h"
#include <string.h>
#include "esp_attr.h"
#include "esp_cpu.h"
#include "riscv/rv_utils.h"

#if !CONFIG_IDF_TARGET_ESP32C6 || CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ != 160 || CONFIG_PM_ENABLE
#error "Native CB13 TX requires ESP32-C6 at fixed160MHz"
#endif
#define CPU_HZ 160000000u
#define SAMPLE_CYCLES (CPU_HZ/C6_TX_PCM_HZ)
#define RING_MASK (C6_TX_RING_SAMPLES-1u)
#define REARM_GUARD_CYCLES 224
#define MAX_CATCHUP_SAMPLES 8u
#define MIN_PULSE_CYCLES 64
#define C6_TX_MPX_MIN_CYCLES 36
#define TONE_WRITE_CYCLES 32
#define EXIT_CYCLES 224
#define MAX_PULSE_CYCLES 680
#define MPX_AREA_BITS 9u
#define MPX_AREA_SCALE (1u << MPX_AREA_BITS)
#define MPX_SLOPE_BITS 11u
#define MPX_SLOPE_ROUND (1 << (MPX_SLOPE_BITS - 1u))
#define MPX_AREA_DENOMINATOR ((int64_t)C6_TX_NCO_STEP_HZ_X2 * C6_TX_MPX_Q4_FULL_SCALE)
#define MPX_AREA_SLOPE(period) (((period) * MPX_AREA_SCALE * 100000LL * (1LL << MPX_SLOPE_BITS) + \
                               MPX_AREA_DENOMINATOR / 2) / MPX_AREA_DENOMINATOR)
#define PCM12_EDGE_RESERVE_CYCLES 64
#define PCM12_PUMP_MIN_CYCLES 200
#define PCM12_CENTROID_DELAY_CYCLES 180
#define PCM12_EXIT_CYCLES 80
#define PCM12_EARLY_WINDOW_CYCLES 224
// Half a native step is 19,531.25 Hz, relative to a full-scale 50 kHz.
#define MPX_NEAREST_SWITCH ((C6_TX_NCO_STEP_HZ_X2 * C6_TX_MPX_Q4_FULL_SCALE + 199999u) / 200000u)

_Static_assert(CPU_HZ/C6_TX_PCM_HZ==1333 && CPU_HZ%C6_TX_PCM_HZ==40000,"Exact fractional MPX clock required");
_Static_assert((C6_TX_RING_SAMPLES&RING_MASK)==0,"Power-of-two PCM ring");
_Static_assert(MPX_AREA_SLOPE(SAMPLE_CYCLES + 1) * C6_TX_MPX_Q4_FULL_SCALE + MPX_SLOPE_ROUND <= INT32_MAX,
               "MPX-to-area product must fit a signed 32-bit multiply");
_Static_assert((uint64_t)MIN_PULSE_CYCLES << (MPX_AREA_BITS + 16u) <= UINT32_MAX,
               "Pulse probability must fit an unsigned 32-bit multiply");
_Static_assert(C6_TX_MPX_MIN_CYCLES > TONE_WRITE_CYCLES && C6_TX_MPX_MIN_CYCLES <= MIN_PULSE_CYCLES,
               "Minimum MPX pulse must allow the RF write and fit the probability bound");

typedef struct {uint16_t nearest_k;int16_t pulse_q16;} mpx_level_t;
static DRAM_ATTR mpx_level_t levels[256];
static DRAM_ATTR union {
    uint8_t legacy[C6_TX_RING_SAMPLES];
    int16_t mpx_q4[C6_TX_RING_SAMPLES];
} pcm_ring;
static DRAM_ATTR c6_tx_stats_t counters;
static volatile uint32_t write_head,read_tail;
static volatile bool armed,running,end_seen;
static bool initialized,protected_stream,pcm12_mode,pcm_in_pulse;
static const char *last_fault;
static int32_t phase_debt;
static uint32_t pulse_random;
static uint32_t pump_deadline;
static unsigned last_k, mpx_center;

static inline __attribute__((always_inline)) uint32_t cycles(void){return esp_cpu_get_cycle_count();}
static inline __attribute__((always_inline)) void fence(void){asm volatile("fence rw,rw":::"memory");}
static inline __attribute__((always_inline)) uint32_t magnitude(int32_t x){return x<0?(uint32_t)-x:(uint32_t)x;}

bool c6_tx_init(void) {
    if(initialized)return true;
    const int64_t denominator=127LL*C6_TX_NCO_STEP_HZ_X2;
    for(unsigned b=0;b<256u;++b) {
        const int32_t sample=(int8_t)b;
        const int64_t scaled=(int64_t)sample*2LL*50000LL*65536LL;
        const int32_t q=C6_TX_NCO_CARRIER_CODE*65536+
            (int32_t)((scaled+(scaled>=0?denominator/2:-denominator/2))/denominator);
        const int32_t nearest=(q+32768)>>16;
        levels[b]=(mpx_level_t){(uint16_t)nearest,(int16_t)(q-nearest*65536)};
    }
    initialized=true;return true;
}

static bool start_tx(uint32_t carrier_hz,uint8_t apwr,bool pcm12) {
    if(armed || running || apwr>191u || carrier_hz<2401000000u || carrier_hz>2482500000u)return false;
    // Keep the legacy centre and the lower edge on the original LO plan.
    mpx_center = pcm12 && carrier_hz >= 2401001000u ?
                 C6_TX_NCO_IN_GROUP_CENTER_CODE : C6_TX_NCO_CARRIER_CODE;
    if(!c6_tx_init() || !c6_tx_radio_prepare(carrier_hz,apwr,mpx_center))return false;
    memset(&counters,0,sizeof(counters));counters.min_queue=C6_TX_RING_SAMPLES;
    write_head=read_tail=0;phase_debt=0;last_k=UINT32_MAX;
    pulse_random=cycles()|1u;
    end_seen=false;protected_stream=false;last_fault=NULL;
    pcm12_mode=pcm12;pcm_in_pulse=false;armed=true;return true;
}
bool c6_tx_start(uint32_t carrier_hz,uint8_t apwr){return start_tx(carrier_hz,apwr,false);}
bool c6_tx_start_pcm(uint32_t carrier_hz,uint8_t apwr){return start_tx(carrier_hz,apwr,true);}

size_t IRAM_ATTR c6_tx_queued_samples(void){return write_head-read_tail;}
size_t IRAM_ATTR c6_tx_free_samples(void){return C6_TX_RING_SAMPLES-(write_head-read_tail);}
bool IRAM_ATTR c6_tx_is_active(void){return armed;}
const char *c6_tx_fault(void){return last_fault;}
void IRAM_ATTR c6_tx_set_protected(bool value){protected_stream=value;}

bool IRAM_ATTR c6_tx_stage(uint16_t offset,uint8_t sample) {
    if(pcm12_mode || !armed || end_seen || offset>=c6_tx_free_samples())return false;
    pcm_ring.legacy[(write_head+offset)&RING_MASK]=sample==0x80u?0:sample;return true;
}
bool IRAM_ATTR c6_tx_commit(size_t count) {
    if(pcm12_mode || !armed || end_seen || count>c6_tx_free_samples())return false;
    fence();write_head+=(uint32_t)count;
    const uint32_t queued=write_head-read_tail;
    if(queued>counters.max_queue)counters.max_queue=queued;
    return true;
}
size_t IRAM_ATTR c6_tx_feed(const uint8_t *samples,size_t count) {
    if(pcm12_mode || !samples || !armed || end_seen)return 0;
    const size_t available=c6_tx_free_samples(),accepted=count<available?count:available;
    for(size_t n=0;n<accepted;++n)pcm_ring.legacy[(write_head+n)&RING_MASK]=samples[n]==0x80u?0:samples[n];
    if(accepted<count)counters.ring_drops+=(uint32_t)(count-accepted);
    (void)c6_tx_commit(accepted);return accepted;
}
bool IRAM_ATTR c6_tx_enqueue_mpx_q4(int16_t sample) {
    if(!pcm12_mode || !armed || end_seen)return false;
    const uint32_t head=write_head;
    const uint32_t queued=head-read_tail;
    if(queued==C6_TX_RING_SAMPLES){++counters.ring_drops;return false;}
    // The producer runs in the RF loop's own callback on the same core.
    pcm_ring.mpx_q4[head&RING_MASK]=sample;
    write_head=head+1;
    if(queued+1>counters.max_queue)counters.max_queue=queued+1;
    return true;
}
uint32_t IRAM_ATTR c6_tx_pcm_played(void){return pcm12_mode?read_tail:0;}
bool IRAM_ATTR c6_tx_pcm_in_pulse(void){return pcm_in_pulse;}
uint32_t IRAM_ATTR c6_tx_pcm_pump_budget(void) {
    if(!running)return UINT32_MAX;
    const int32_t remaining=(int32_t)(pump_deadline-cycles())-PCM12_EDGE_RESERVE_CYCLES;
    return remaining>0?(uint32_t)remaining:0;
}
void IRAM_ATTR c6_tx_finish(void){end_seen=true;}
void IRAM_ATTR c6_tx_stop(void) {
    armed=false;
    /* A streaming callback must never invoke slow calibration/PHY cleanup. */
    if(!running)c6_tx_radio_release();
}
void c6_tx_get_stats(c6_tx_stats_t *stats){if(stats)*stats=counters;}

static inline __attribute__((always_inline)) void wait_until(uint32_t deadline,void (*pump)(void*),void *user) {
    if(pcm12_mode) {
        pump_deadline=deadline;
        while(armed && (int32_t)(deadline-cycles())>PCM12_PUMP_MIN_CYCLES)pump(user);
        while(armed && (int32_t)(deadline-cycles())>0){}
    } else {
        while(armed && (int32_t)(cycles()-deadline)<0)pump(user);
    }
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
    unsigned fraction_bits,uint32_t deadline,uint32_t period,void (*pump)(void*),void *user) {
    if(last_k!=nearest_k){c6_tx_radio_write_k(nearest_k);last_k=nearest_k;}
    int32_t area=target+phase_debt;
    const bool up=area>=0;
    const uint32_t magnitude_area=magnitude(area);
    const int32_t minimum = fraction_bits ? C6_TX_MPX_MIN_CYCLES : MIN_PULSE_CYCLES;
    const uint32_t minimum_area = (uint32_t)minimum << fraction_bits;
    int32_t width;
    // Randomize sub-minimum corrections to break patterns coherent with the
    // stereo pilot. Subtract only the correction actually emitted below.
    if(magnitude_area<minimum_area)
        width=magnitude_area && emit_minimum_pulse(magnitude_area,minimum_area)?minimum:0;
    else width=(magnitude_area+((1u<<fraction_bits)/2u))>>fraction_bits;
    if(width>=minimum) {
        if(width>MAX_PULSE_CYCLES)width=MAX_PULSE_CYCLES;
        // A fixed PCM offset reserves one uninterrupted render window before
        // the pulse. Pulse area and the legacy center stay the same.
        const uint32_t offset=fraction_bits?PCM12_CENTROID_DELAY_CYCLES:0;
        const uint32_t start=deadline-(period+(uint32_t)width)/2u+offset;
        wait_until(start,pump,user);
        if(!armed)return;
        const int32_t exit_budget=fraction_bits?PCM12_EXIT_CYCLES:EXIT_CYCLES;
        const int32_t room=(int32_t)(deadline-cycles())-exit_budget-TONE_WRITE_CYCLES;
        if(width>room)width=room;
        if(width>=minimum) {
            c6_tx_radio_write_k((unsigned)((int)nearest_k+(up?1:-1)));
            const uint32_t pulse_start=cycles();
            const uint32_t stop=pulse_start+(uint32_t)(width-TONE_WRITE_CYCLES);
            // A short plateau cannot reach wait_until()'s PCM pump
            // threshold. Use the dedicated cycle wait for that case.
            if (fraction_bits && width > TONE_WRITE_CYCLES + PCM12_PUMP_MIN_CYCLES) {
                // The PCM producer may use part of the held RF plateau.
                // Its callback must restrict this window to bounded DSP.
                pcm_in_pulse=true;
                wait_until(stop,pump,user);
                pcm_in_pulse=false;
            } else {
                while((int32_t)(cycles()-stop)<0){}
            }
            c6_tx_radio_write_k(nearest_k);
            const uint32_t elapsed=cycles()-pulse_start;
            const int32_t actual=(int32_t)(elapsed<<fraction_bits);
            area+=up?-actual:actual;
            if(fraction_bits && elapsed>(uint32_t)width) {
                const uint32_t late=elapsed-(uint32_t)width;
                if(late>counters.max_pulse_late_cycles)counters.max_pulse_late_cycles=late;
            }
        }
    }
    const int32_t limit=(int32_t)(period<<fraction_bits);
    if(area>limit)area=limit;
    else if(area<-limit)area=-limit;
    phase_debt=area;
}

static void IRAM_ATTR emit_mpx(uint8_t sample,uint32_t deadline,uint32_t period,
                              void (*pump)(void*),void *user) {
    const mpx_level_t level=levels[sample];
    const int32_t target=(int32_t)level.pulse_q16*(int32_t)period;
    const int32_t area=(target+(target>=0?32768:-32768))/65536;
    emit_level(level.nearest_k,area,0,deadline,period,pump,user);
}

static inline __attribute__((always_inline)) unsigned mpx_nearest(int16_t sample) {
    if(sample>=(int32_t)MPX_NEAREST_SWITCH)return mpx_center+1;
    if(sample<=-(int32_t)MPX_NEAREST_SWITCH)return mpx_center-1;
    return mpx_center;
}

static void IRAM_ATTR emit_mpx_q4(int16_t sample,uint32_t deadline,uint32_t period,
                                 void (*pump)(void*),void *user) {
    // Choose a precomputed slope for the current 1,333/1,334-cycle interval.
    // Q9 retains fractional pulse area between intervals.
    const int32_t slope = period == SAMPLE_CYCLES ? MPX_AREA_SLOPE(SAMPLE_CYCLES) : MPX_AREA_SLOPE(SAMPLE_CYCLES + 1);
    const int32_t step = (int32_t)(period * MPX_AREA_SCALE);
    int32_t target = ((int32_t)sample * slope + MPX_SLOPE_ROUND) >> MPX_SLOPE_BITS;
    const unsigned nearest = mpx_nearest(sample);
    if(nearest>mpx_center)target-=step;
    else if(nearest<mpx_center)target+=step;
    emit_level(nearest,target,MPX_AREA_BITS,deadline,period,pump,user);
}

void IRAM_ATTR c6_tx_run(void (*pump)(void*),void *user) {
    if(!armed || running || !pump)return;
    const uint32_t pending=write_head-read_tail;
    if(pending<C6_TX_START_LEVEL && !end_seen)return;
    if(!pending){armed=false;c6_tx_radio_release();return;}
    running=true;
    const unsigned irq_state=RV_CLEAR_CSR(mstatus,MSTATUS_MIE);
    const uint32_t startup=cycles();
    if(!c6_tx_radio_key()) {
        last_fault="TX_RF";armed=false;
    }
    counters.startup_cycles=cycles()-startup;
    /* 160MHz/120kHz =1333+1/3 cycles: never truncate the MPX sample clock.
     * Each group of3 intervals is exactly4000 cycles, including wraparound. */
    unsigned fraction=1u;
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
            if(++fraction==3u){fraction=0;period++;}
            deadline+=period;
        }
        if(!armed)break;
        const uint32_t queued=write_head-read_tail;
        if(queued<counters.min_queue)counters.min_queue=queued;
        if(consume>1u)counters.timer_skips+=consume-1u;
        int16_t sample=0;
        if(queued>=consume) {
            const uint32_t offset=(read_tail+consume-1u)&RING_MASK;
            if(pcm12_mode){sample=pcm_ring.mpx_q4[offset];counters.pcm_tail_underruns=0;}
            else sample=pcm_ring.legacy[offset];
            read_tail+=consume;
        }
        else {
            if(end_seen)break;
            ++counters.underruns;++counters.zero_samples;
            if(pcm12_mode)++counters.pcm_tail_underruns;
            if(protected_stream){last_fault="TX_UNDERRUN";armed=false;break;}
        }
        if(pcm12_mode)emit_mpx_q4(sample,deadline,period,pump,user);
        else emit_mpx((uint8_t)sample,deadline,period,pump,user);
        ++counters.samples;
        uint32_t next_processing=deadline;
        if(pcm12_mode && write_head!=read_tail) {
            // The next sample is immutable once queued. When its coarse
            // code is already selected there is no RF edge at the sample
            // boundary, so do not chop the idle interval at that point.
            const int16_t next_sample=pcm_ring.mpx_q4[read_tail&RING_MASK];
            if(mpx_nearest(next_sample)==last_k)next_processing-=PCM12_EARLY_WINDOW_CYCLES;
        }
        wait_until(next_processing,pump,user);
        sample_start=deadline;period=SAMPLE_CYCLES;
        if(++fraction==3u){fraction=0;period++;}
        deadline+=period;
    }
    c6_tx_radio_off();
    RV_SET_CSR(mstatus,irq_state&MSTATUS_MIE);
    running=false;armed=false;
    c6_tx_radio_release();
}
