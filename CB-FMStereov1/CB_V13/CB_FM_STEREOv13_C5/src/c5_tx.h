/* SPDX-License-Identifier: GPL-3.0-or-later
 * CB_FM_STEREOv13 native C5 continuous NCO MPX transmitter.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define C5_TX_PCM_HZ 120000u
#define C5_TX_IQ_HZ 120000u /* One MPX interval; modulation is native NCO. */
#define C5_TX_RING_SAMPLES 16384u
#define C5_TX_START_LEVEL 4096u
typedef struct {
    uint32_t samples,underruns,zero_samples,timer_skips,ring_drops;
    uint32_t min_queue,max_queue,max_late_cycles,late_slots;
    uint32_t startup_cycles,startup_dco_changes;
} c5_tx_stats_t;
/* init prepares only tables: it emits no RF and starts no Wi-Fi. */
bool c5_tx_init(void);
bool c5_tx_start(uint32_t carrier_hz,uint8_t apwr);
void c5_tx_run(void (*pump)(void *),void *user);
void c5_tx_stop(void);
void c5_tx_finish(void);
bool c5_tx_is_active(void);
size_t c5_tx_free_samples(void);
size_t c5_tx_queued_samples(void);
size_t c5_tx_feed(const uint8_t *samples,size_t count);
bool c5_tx_stage(uint16_t offset,uint8_t sample);
bool c5_tx_commit(size_t count);
void c5_tx_set_protected(bool protected_stream);
const char *c5_tx_fault(void);
void c5_tx_get_stats(c5_tx_stats_t *stats);
#ifdef __cplusplus
}
#endif
