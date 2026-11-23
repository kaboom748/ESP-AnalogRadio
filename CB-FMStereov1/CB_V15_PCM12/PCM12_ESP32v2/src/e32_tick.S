/* SPDX-License-Identifier: GPL-3.0-or-later
 * ESP-IDF Xtensa IPC calls this peer callback with CALLX0 and the tick
 * interval in a2. It may only use registers a2 through a4.
 */
#include "xtensa_timer.h"

    .section .iram1, "ax"
    .align 4
    .global e32_rearm_peer_tick
    .type e32_rearm_peer_tick, @function
e32_rearm_peer_tick:
    rsr.ccount a3
    add a3, a3, a2
    wsr a3, XT_CCOMPARE
    esync
    ret
    .size e32_rearm_peer_tick, .-e32_rearm_peer_tick
