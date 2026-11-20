/* SPDX-License-Identifier: GPL-3.0-or-later
 * ESP32-C5 5 GHz continuous-FM TX: power-stage selection (experimental).
 *
 * 2.4 GHz V13 uses its own characterized RF/BB attenuation ladder.
 * That ladder must NOT be silently reused in 5 GHz. Request the real 5 GHz
 * per-frequency TX table from the pinned Espressif PHY and use its first
 * available PA/BB pair. This avoids inventing a 5 GHz gain ladder.
 *
 * APWR maps across the NCO's established 32..127 attenuation window while
 * 5 GHz analog gain stays at the calibrated PHY point. Therefore the 5 GHz
 * power law and total dynamic range are NOT equal to V13 2.4 GHz. Refining
 * them requires measurements and a known 5 GHz PA-stage model.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint16_t rf_gain;
    uint16_t bb_gain;
    uint16_t attenuation;
    unsigned table_index;
} c5_tx_5g_plan_t;

static inline bool c5_tx_5g_make_plan(uint8_t apwr, const int8_t digital[32],
                                     const uint16_t bb[32], const uint16_t rf[32],
                                     c5_tx_5g_plan_t *plan) {
    if (!plan || !digital || !bb || !rf || apwr > 191u) return false;
    /* Raw 5 GHz PA codes and baseband codes come from the active PHY table.
     * Do NOT mask RF codes to 2.4 GHz's 6-bit NativeRf model. Zero and 0xffff
     * are treated as uninitialized entries; no RF output if all are invalid.
     * A valid table is necessary but does not prove calibrated RF output. */
    for (unsigned i = 0; i < 32u; ++i) {
        if (rf[i] == 0 || rf[i] == UINT16_MAX || bb[i] == UINT16_MAX) continue;
        plan->rf_gain = rf[i];
        plan->bb_gain = bb[i];
        plan->table_index = i;
        /* Do not fabricate a 5 GHz PA curve. Sweep the available NCO
         * attenuation range monotonically. V15 preserves APWR 0..191 API. */
        plan->attenuation = (uint16_t)(32u + ((uint32_t)apwr * 95u + 95u) / 191u);
        return true;
    }
    return false;
}
