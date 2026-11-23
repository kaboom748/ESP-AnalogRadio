/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_attr.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*e32_pcm_producer_t)(void *);
bool e32_pcm_worker_start(e32_pcm_producer_t produce, void *user);
void IRAM_ATTR e32_pcm_worker_stop(bool rearm_tick);
uint32_t IRAM_ATTR e32_tx_mask_local(void);
void IRAM_ATTR e32_tx_restore_local(uint32_t previous);

#ifdef __cplusplus
}
#endif
