/* SPDX-License-Identifier: GPL-3.0-or-later
 * Arduino-ESP32 3.3.12 / ESP-IDF 5.5.5 C5 carrier-test adapter.
 *
 * The standard Arduino package omits librftest.a. This adapter reproduces
 * the control sequence of Espressif's esp_phy_wifi_tx_tone -> wifiscwout ->
 * rftest_set_chan, without their console messages. It uses only primitives
 * already exported by the standard C5 libphy.a, not an additional SDK or
 * a replacement board platform. The application uses MHz values in 2.4GHz.
 *
 * Provenance: Espressif esp-phy-lib e294ff039e26b3486d6c9e5853d24d98ee3300b2,
 * inspected in the qualified ESP-IDF 6.2 tree and in Arduino 3.3.12's SDK.
 * Both C5 libphy.a files have the identical SHA256:
 * dbf33c418c8d408d4005c849d12a1432deea82e2e5e57de3c8ddf914d104fffb.
 * Private interfaces are therefore pinned, not assumed across SDK releases.
 */
#include <stdint.h>
#include "sdkconfig.h"
#include "esp_idf_version.h"
#include "esp_phy_cert_test.h"

#if !CONFIG_IDF_TARGET_ESP32C5
#error "This PHY adapter is for ESP32-C5 only"
#endif

#if ESP_IDF_VERSION != ESP_IDF_VERSION_VAL(5, 5, 5)
#error "Use Espressif Arduino-ESP32 3.3.12 (ESP-IDF 5.5.5) for this pinned PHY adapter"
#endif

/* A cert-test-enabled SDK supplies the public function itself. */
#if !CONFIG_ESP_PHY_ENABLE_CERT_TEST
extern uint8_t phy_param[];
extern void RFChannelSel(uint16_t frequency_or_channel, int8_t offset);
extern void phy_param_track_tot(unsigned first, unsigned second);
extern void phy_txcal_debuge_mode_new(void);
extern int8_t phy_set_chan_interp(const uint8_t *table, uint16_t channel);
extern void phy_start_tx_tone_step(unsigned first_on, unsigned first_step,
                                   unsigned first_attenuation, unsigned second_on,
                                   unsigned second_step, unsigned second_attenuation);
extern void phy_txcal_work_mode(void);

void esp_phy_wifi_tx_tone(uint32_t start, uint32_t channel, uint32_t backoff)
{
    if (start) {
        const uint16_t original_channel = (uint16_t)channel;
        uint16_t radio_channel = original_channel;
        /* The provider converts 5GHz channel indices here; MHz inputs such
         * as2442 pass through unchanged. Calibration interpolation receives
         * the ORIGINAL argument, exactly as in wifiscwout(). */
        if (radio_channel >= 36u && radio_channel <= 177u)
            radio_channel = (uint16_t)(5000u + 5u * radio_channel);
        RFChannelSel(radio_channel, 0);
        phy_param_track_tot(0, 0);
        phy_txcal_debuge_mode_new();
        const int32_t adjustment = phy_set_chan_interp(phy_param + 0x100u,
                                                       original_channel);
        /* Vendor arithmetic truncates to signed8 AFTER adding32/backoff,
         * then clamps negative values to zero. Do not replace by a wider
         * saturation: that would alter the calibrated gain selection. */
        const uint8_t encoded = (uint8_t)((uint32_t)adjustment + backoff + 32u);
        const unsigned attenuation = (encoded & 0x80u) ? 0u : encoded;
        phy_start_tx_tone_step(1, 0, attenuation, 0, 0, 0);
    } else {
        phy_start_tx_tone_step(0, 0, 0, 0, 0, 0);
        phy_txcal_work_mode();
    }
}
#endif
