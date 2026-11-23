/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "c6_rx.h"

typedef int32_t (*c6_rx_pcm_angle_cb_t)(int32_t i, int32_t q, int32_t previous_i,
                                         int32_t previous_q);

void c6_rx_pcm_prepare(unsigned profile, c6_rx_pcm_angle_cb_t angle);
void c6_rx_pcm_set_sink(uint8_t *frame, c6_rx_pcm_commit_cb_t commit, void *user);
/* One signed Q14 MPX sample at 120 kHz, referred to 75 kHz peak
 * FM deviation. When true, finish this output before the next MPX step. */
bool c6_rx_pcm_sample(int32_t mpx, bool valid, uint16_t strength);
void c6_rx_pcm_finish(void);
