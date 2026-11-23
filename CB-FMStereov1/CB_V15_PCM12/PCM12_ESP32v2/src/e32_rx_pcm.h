/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "e32_rx.h"

typedef int32_t (*e32_rx_pcm_angle_cb_t)(int32_t i, int32_t q, int32_t previous_i,
                                         int32_t previous_q);

void e32_rx_pcm_prepare(unsigned profile, e32_rx_pcm_angle_cb_t angle);
void e32_rx_pcm_set_sink(uint8_t *frame, e32_rx_pcm_commit_cb_t commit, void *user);
/* One signed Q14 MPX sample at 120 kHz, referred to 75 kHz peak
 * FM deviation. Missing IQ groups still advance the decoder's clock. */
void e32_rx_pcm_sample(int32_t mpx, bool valid, uint16_t strength);
