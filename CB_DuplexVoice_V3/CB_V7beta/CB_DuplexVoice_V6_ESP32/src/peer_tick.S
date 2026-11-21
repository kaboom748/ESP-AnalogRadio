/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "sdkconfig.h"

#if CONFIG_FREERTOS_SYSTICK_USES_CCOUNT
    .section .iram1,"ax",@progbits
    .align 4
    .global e32_rearm_peer_tick
    .type e32_rearm_peer_tick, @function
e32_rearm_peer_tick:
    /* IPC invokes this leaf with CALLX0 and the tick divisor in a2. */
    rsr     a3, ccount
    add     a3, a3, a2
    wsr     a3, ccompare0
    esync
    ret
    .size e32_rearm_peer_tick, .-e32_rearm_peer_tick
#endif
