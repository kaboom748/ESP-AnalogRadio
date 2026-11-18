/* SPDX-License-Identifier: GPL-3.0-or-later
 * Arduino-ESP32 3.3.12 / IDF 5.5.5 classic ESP32 CW adapter.
 * Stock Arduino omits librftest.a. This is its wifiscwout control sequence,
 * using the SDK's patched g_phyFuns dispatch table, including on ECO1.
 * SDK libphy.a SHA256 c2521071695734afa8f6f1ca3d3b604a1d08dd63fe52060449a11cfe8bac5740
 * is identical to the inspected esp-phy-lib e294ff039e26b3486d6c9e5853d24d98ee3300b2.
 */
#include <stdint.h>
#include "sdkconfig.h"
#include "esp_idf_version.h"
#include "esp_phy_cert_test.h"
#include "soc/dport_access.h"

#if !CONFIG_IDF_TARGET_ESP32 || ESP_IDF_VERSION != ESP_IDF_VERSION_VAL(5,5,5)
#error "Use ESP32 Dev Module and Arduino-ESP32 3.3.12 (IDF 5.5.5)"
#endif

#if !CONFIG_ESP_PHY_ENABLE_CERT_TEST
extern void **g_phyFuns;
extern void RFChannelSel(unsigned channel,int offset);
extern void txcal_debuge_mode(unsigned mode);
typedef void (*tone_step_fn)(unsigned,int,unsigned,unsigned,int,unsigned);

void esp_phy_wifi_tx_tone(uint32_t start,uint32_t channel,uint32_t backoff)
{
    tone_step_fn tone=(tone_step_fn)g_phyFuns[0x44u/4u];
    if(start) {
        RFChannelSel((unsigned)(int)(int8_t)channel,0);
        txcal_debuge_mode(0u);
        /* Match the vendor's signed-eight-bit truncation and negative clamp. */
        const int8_t encoded=(int8_t)(backoff+43u-DPORT_REG_READ(0x3ff46004u));
        tone(1u,0,encoded<0?0u:(unsigned)encoded,0u,0,0u);
    } else {
        tone(0u,0,0u,0u,0,0u);
        ((void (*)(void))g_phyFuns[0x124u/4u])();
    }
}
#endif
