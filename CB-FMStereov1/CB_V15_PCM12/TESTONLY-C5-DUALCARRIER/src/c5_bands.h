/* SPDX-License-Identifier: GPL-3.0-or-later
 * CB V13 dual-band frequency policy, based on the V16 C5 RF backend.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define C5_RF_24_MIN_HZ       UINT64_C(2401000000)
#define C5_RF_24_MAX_HZ       UINT64_C(2482500000)
#define C5_RF_5_MIN_HZ        UINT64_C(5180000000)
#define C5_RF_5_MAX_HZ        UINT64_C(5885000000)
#define C5_RF_FINE_MAX_HZ     50000
#define C5_RF_SECOND_CARRIER_OFFSET_HZ 781250u

/* Leave room for the upper carrier and the LO's nearest-kHz rounding. */
#define C5_RF_TX_UPPER_MARGIN_HZ (C5_RF_SECOND_CARRIER_OFFSET_HZ + 500u)
#define C5_RF_24_TX_MAX_HZ (C5_RF_24_MAX_HZ - C5_RF_TX_UPPER_MARGIN_HZ)
#define C5_RF_5_TX_MAX_HZ  (C5_RF_5_MAX_HZ - C5_RF_TX_UPPER_MARGIN_HZ)

static inline bool c5_tx_frequency_valid(uint64_t hz) {
    return (hz >= C5_RF_24_MIN_HZ && hz <= C5_RF_24_TX_MAX_HZ) ||
           (hz >= C5_RF_5_MIN_HZ && hz <= C5_RF_5_TX_MAX_HZ);
}

static inline bool c5_rx_frequency_valid(uint64_t hz) {
    return (hz >= C5_RF_24_MIN_HZ && hz <= C5_RF_24_MAX_HZ) ||
           (hz >= C5_RF_5_MIN_HZ && hz <= C5_RF_5_MAX_HZ);
}

static inline bool c5_rx_tuning_valid(uint64_t hz) {
    return (hz >= C5_RF_24_MIN_HZ - C5_RF_FINE_MAX_HZ &&
            hz <= C5_RF_24_MAX_HZ + C5_RF_FINE_MAX_HZ) ||
           (hz >= C5_RF_5_MIN_HZ - C5_RF_FINE_MAX_HZ &&
            hz <= C5_RF_5_MAX_HZ + C5_RF_FINE_MAX_HZ);
}
/* C5 has ONE Wi-Fi RF chain. 5 GHz must be selected, not mixed with 2.4. */
static inline bool c5_frequency_is_5g(uint64_t hz) {
    return hz >= UINT64_C(4000000000);
}
