/* SPDX-License-Identifier: GPL-3.0-or-later
 * Arduino-ESP32 3.3.12 / ESP-IDF 5.5.5 C6 carrier-test adapter.
 *
 * The standard Arduino package omits librftest.a. This adapter reproduces
 * the control sequence of Espressif's esp_phy_wifi_tx_tone -> wifiscwout ->
 * rftest_set_chan, without their console messages. It uses only primitives
 * already exported by the standard C6 libphy.a, not an additional SDK or
 * a replacement board platform. The C6 public CW API takes Wi-Fi channels.
 *
 * Provenance: Espressif esp-phy-lib e294ff039e26b3486d6c9e5853d24d98ee3300b2,
 * inspected in the qualified ESP-IDF 6.2 tree and in Arduino 3.3.12's SDK.
 * Both C6 libphy.a files have the identical SHA256:
 * fdba95c59c03560620c399f43a72a16230a8ab6fced172f3103ac6a29c36f43a.
 * Private interfaces are therefore pinned, not assumed across SDK releases.
 */
#include <stdint.h>
#include "sdkconfig.h"
#include "esp_idf_version.h"
#include "esp_phy_cert_test.h"

#if !CONFIG_IDF_TARGET_ESP32C6
#error "This PHY adapter is for ESP32-C6 only"
#endif

#if ESP_IDF_VERSION != ESP_IDF_VERSION_VAL(5, 5, 5)
#error "Use Espressif Arduino-ESP32 3.3.12 (ESP-IDF 5.5.5) for this pinned PHY adapter"
#endif

/* A cert-test-enabled SDK supplies the public function itself. */
#if !CONFIG_ESP_PHY_ENABLE_CERT_TEST
extern uint8_t phy_param[];
extern void RFChannelSel(uint16_t frequency_or_channel, int8_t offset);
extern int rfpll_cap_correct_new(unsigned);
extern void ets_delay_us(unsigned);
extern void ram_txcal_debuge_mode(void);
extern int8_t ram_set_chan_cal_interp(const uint8_t *table, uint16_t channel);
extern void ram_start_tx_tone_step(unsigned first_on, unsigned first_step,
                                   unsigned first_attenuation, unsigned second_on,
                                   unsigned second_step, unsigned second_attenuation);
extern void txcal_work_mode(void);

void esp_phy_wifi_tx_tone(uint32_t start, uint32_t channel, uint32_t backoff)
{
    if (start) {
        const uint16_t original_channel = (uint16_t)channel;
        RFChannelSel(original_channel, 0);
        /* Optional PLL-cap correction from C6 rftest_set_chan(), bounded
         * outside the streaming loop. The test-only background tracking
         * switches do not exist in stock Arduino and stay disabled. */
        if(phy_param[10])for(unsigned n=0;n<10u;n++) {
            ets_delay_us(20u);
            if(!rfpll_cap_correct_new(phy_param[9]))break;
        }
        ram_txcal_debuge_mode();
        const int32_t adjustment = ram_set_chan_cal_interp(phy_param + 0xf4u,
                                                           original_channel);
        /* Vendor arithmetic truncates to signed8 AFTER adding12/backoff,
         * then clamps negative values to zero. Do not replace by a wider
         * saturation: that would alter the calibrated gain selection. */
        const uint8_t encoded = (uint8_t)((uint32_t)adjustment + backoff + 12u);
        const unsigned attenuation = (encoded & 0x80u) ? 0u : encoded;
        ram_start_tx_tone_step(1, 0, attenuation, 0, 0, 0);
    } else {
        ram_start_tx_tone_step(0, 0, 0, 0, 0, 0);
        txcal_work_mode();
    }
}
#endif
