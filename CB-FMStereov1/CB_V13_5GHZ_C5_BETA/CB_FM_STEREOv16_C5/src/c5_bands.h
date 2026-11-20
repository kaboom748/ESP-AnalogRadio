/* SPDX-License-Identifier: GPL-3.0-or-later
 * CB V15 TX/RX dual-band frequency policy.
 * Software accepts RX *attempts* across ESP-SDR's 100–6000 MHz test window.
 * This is NOT a claim that a PLL or antenna works everywhere in that window.
 * At 1.8 GHz on C5, controlled experiments have not shown usable reception.
 * TX is restricted to the original CB13 range plus the C5 datasheet 5G band.
 * A permitted TX frequency is NOT proof of calibration or legal authority.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define C5_RF_RX_TRIAL_MIN_HZ UINT64_C(100000000)
#define C5_RF_RX_TRIAL_MAX_HZ UINT64_C(6000000000)
#define C5_RF_24_TX_MIN_HZ    UINT64_C(2401000000)
#define C5_RF_24_TX_MAX_HZ    UINT64_C(2482500000)
#define C5_RF_5_TX_MIN_HZ     UINT64_C(5180000000)
#define C5_RF_5_TX_MAX_HZ     UINT64_C(5885000000)

static inline bool c5_rx_frequency_valid(uint64_t hz) {
    return hz >= C5_RF_RX_TRIAL_MIN_HZ && hz <= C5_RF_RX_TRIAL_MAX_HZ;
}
static inline bool c5_tx_frequency_valid(uint64_t hz) {
    return (hz >= C5_RF_24_TX_MIN_HZ && hz <= C5_RF_24_TX_MAX_HZ) ||
           (hz >= C5_RF_5_TX_MIN_HZ && hz <= C5_RF_5_TX_MAX_HZ);
}
/* C5 has ONE Wi-Fi RF chain. 5 GHz must be selected, not mixed with 2.4. */
static inline bool c5_frequency_is_5g(uint64_t hz) {
    return hz >= UINT64_C(4000000000);
}
