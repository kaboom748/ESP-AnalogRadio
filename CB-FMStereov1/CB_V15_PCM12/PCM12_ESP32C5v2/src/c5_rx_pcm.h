/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "c5_rx.h"

typedef int32_t (*c5_rx_pcm_angle_cb_t)(int32_t i, int32_t q, int32_t previous_i,
                                         int32_t previous_q);

void c5_rx_pcm_prepare(unsigned profile, c5_rx_pcm_angle_cb_t angle);
void c5_rx_pcm_set_sink(c5_rx_pcm_emit_cb_t emit, void *user);
/* One signed Q14 MPX sample at precisely 120 kHz, referred to 75 kHz peak
 * FM deviation. Missing IQ groups still advance the decoder's clock. */
void c5_rx_pcm_sample(int32_t mpx, bool valid, uint16_t strength);
