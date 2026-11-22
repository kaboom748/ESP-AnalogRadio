/* SPDX-License-Identifier: GPL-3.0-or-later
 * CB_FM_STEREOv13 native C6 continuous NCO MPX transmitter.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define C6_TX_PCM_HZ 120000u
#define C6_TX_IQ_HZ 120000u /* One MPX interval; modulation is native NCO. */
#define C6_TX_RING_SAMPLES 16384u
#define C6_TX_START_LEVEL 4096u
typedef struct {
    uint32_t samples,underruns,zero_samples,timer_skips,ring_drops;
    uint32_t min_queue,max_queue,max_late_cycles,late_slots;
    uint32_t startup_cycles,startup_dco_changes;
    uint32_t pcm_tail_underruns,max_pulse_late_cycles;
} c6_tx_stats_t;
/* init prepares only tables: it emits no RF and starts no Wi-Fi. */
bool c6_tx_init(void);
bool c6_tx_start(uint32_t carrier_hz,uint8_t apwr);
bool c6_tx_start_pcm(uint32_t carrier_hz,uint8_t apwr);
void c6_tx_run(void (*pump)(void *),void *user);
void c6_tx_stop(void);
void c6_tx_finish(void);
bool c6_tx_is_active(void);
size_t c6_tx_free_samples(void);
size_t c6_tx_queued_samples(void);
size_t c6_tx_feed(const uint8_t *samples,size_t count);
bool c6_tx_enqueue_pcm12(int16_t sample);
uint32_t c6_tx_pcm_played(void);
/* Budget is measured to the next RF transition with an edge reserve. */
uint32_t c6_tx_pcm_pump_budget(void);
bool c6_tx_pcm_in_pulse(void);
bool c6_tx_stage(uint16_t offset,uint8_t sample);
bool c6_tx_commit(size_t count);
void c6_tx_set_protected(bool protected_stream);
const char *c6_tx_fault(void);
void c6_tx_get_stats(c6_tx_stats_t *stats);
#ifdef __cplusplus
}
#endif
