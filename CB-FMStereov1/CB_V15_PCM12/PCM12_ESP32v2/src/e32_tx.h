/* SPDX-License-Identifier: GPL-3.0-or-later
 * CB_FM_STEREOv13 native classic ESP32 continuous NCO MPX transmitter.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define E32_TX_PCM_HZ 120000u
#define E32_TX_IQ_HZ 120000u /* One MPX interval; modulation is native NCO. */
#define E32_TX_RING_SAMPLES 16384u
#define E32_TX_START_LEVEL 4096u
#define E32_TX_MPX_Q4_FULL_SCALE (2047 * 16)
typedef struct {
    uint32_t samples,underruns,zero_samples,timer_skips,ring_drops;
    uint32_t min_queue,max_queue,max_late_cycles,late_slots;
    uint32_t startup_cycles,startup_dco_changes;
    uint32_t pcm_tail_underruns;
} e32_tx_stats_t;
/* init prepares only tables: it emits no RF and starts no Wi-Fi. */
bool e32_tx_init(void);
bool e32_tx_start(uint32_t carrier_hz,uint8_t apwr);
bool e32_tx_start_pcm(uint32_t carrier_hz,uint8_t apwr);
void e32_tx_run(void (*pump)(void *),void *user,void (*produce)(void *));
bool e32_tx_uses_worker(void);
bool e32_tx_last_used_worker(void);
void e32_tx_stop(void);
void e32_tx_finish(void);
bool e32_tx_is_active(void);
size_t e32_tx_free_samples(void);
size_t e32_tx_queued_samples(void);
size_t e32_tx_feed(const uint8_t *samples,size_t count);
/* Publish one 120 kHz multiplex sample with four fractional PCM bits. */
bool e32_tx_enqueue_mpx_q4(int16_t sample);
/* Played counts real PCM12 samples consumed from the ring. */
uint32_t e32_tx_pcm_played(void);
/* CPU cycles left in the active streaming pump window. */
uint32_t e32_tx_pcm_pump_budget(void);
bool e32_tx_stage(uint16_t offset,uint8_t sample);
bool e32_tx_commit(size_t count);
void e32_tx_set_protected(bool protected_stream);
const char *e32_tx_fault(void);
void e32_tx_get_stats(e32_tx_stats_t *stats);
#ifdef __cplusplus
}
#endif
