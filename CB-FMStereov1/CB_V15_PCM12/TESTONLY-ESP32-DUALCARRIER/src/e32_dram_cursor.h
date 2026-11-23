/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdint.h>

/* The two Xtensa cores exchange only aligned 32-bit cursor and state words
 * in internal DRAM. Acquire follows L32I and release precedes S32I, avoiding
 * a library fallback or spinlock while the two RF session tasks mask IRQs. */
static inline __attribute__((always_inline)) uint32_t e32_dram_load(const volatile uint32_t *slot) {
    uint32_t value;
    __asm__ volatile("l32i %0, %1, 0\n\tmemw" : "=r"(value) : "r"(slot) : "memory");
    return value;
}
static inline __attribute__((always_inline)) void e32_dram_store(volatile uint32_t *slot, uint32_t value) {
    __asm__ volatile("memw\n\ts32i %1, %0, 0" :: "r"(slot), "r"(value) : "memory");
}
