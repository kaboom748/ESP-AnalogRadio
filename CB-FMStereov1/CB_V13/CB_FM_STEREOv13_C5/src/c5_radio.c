/* SPDX-License-Identifier: GPL-3.0-or-later
 * C5 PHY bring-up and tuning adapted from ESPARGOS ESP-SDR and
 * ESPsoup contributors / Peter Holzhauser (2026). See project NOTICE.
 */
#include "c5_radio.h"
#include <stdbool.h>
#include "esp_event.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_rom_sys.h"
#include "soc/soc.h"

extern void phy_set_chanfreq(unsigned, unsigned);
extern void phy_set_rf_freq_offset(unsigned, unsigned, int);
extern unsigned char phy_param[];
extern void phy_stop_tx_tone(unsigned);
extern void phy_pbus_workmode(void);
extern void phy_pbus_xpd_rx_on(unsigned);
extern void phy_pbus_xpd_tx_off(void);
extern void phy_set_rxclk_en(unsigned);
extern void phy_force_rx_gain(unsigned, unsigned);
extern void phy_enable_agc(void);

static bool initialized;
static uint32_t frequency;
static int gain_override=-1;
static DRAM_ATTR bool gain_automatic;
static DRAM_ATTR uint8_t gain_index,gain_maximum;
void c5_radio_set_rx_gain(int index) {gain_override=index;}

bool IRAM_ATTR c5_radio_rx_gain_automatic(void) {return gain_automatic;}
unsigned IRAM_ATTR c5_radio_rx_gain_index(void) {return gain_index;}
unsigned IRAM_ATTR c5_radio_rx_gain_maximum(void) {return gain_maximum;}
bool IRAM_ATTR c5_radio_rx_gain_update(unsigned index) {
    if(index>gain_maximum)index=gain_maximum;
    if(index==gain_index)return false;
    /* Match the pinned SDK's phy_force_rx_gain(1,index) exactly: first
     * replace the index while preserving forcebit23, then assert force1.
     * A held force bit supports level changes; no4us deassert/assert pulse
     * is needed. Caller already finished and cleared the IQ accumulator. */
    uint32_t word=(REG_READ(0x600a702cu)&0x00ffffffu)|(index<<24);
    REG_WRITE(0x600a702cu,word);
    __asm__ volatile("fence iorw,iorw":::"memory");
    word=REG_READ(0x600a702cu)|(1u<<23);
    REG_WRITE(0x600a702cu,word);
    __asm__ volatile("fence iorw,iorw":::"memory");
    gain_index=(uint8_t)index;
    return true;
}

/* Required by Espressif's RF test archive; this port exposes no RF shell. */
__attribute__((weak)) int cmd_parse(char *cmd, char *name, int *argc, char **argv)
{ (void)cmd; (void)name; (void)argc; (void)argv; return -1; }

esp_err_t c5_radio_init(void)
{
    if (initialized) return ESP_OK;
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        e = nvs_flash_erase();
        if (e == ESP_OK) e = nvs_flash_init();
    }
    if (e != ESP_OK) return e;
    e = esp_event_loop_create_default();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return e;
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    cfg.static_rx_buf_num = 4;
    /* Match the qualified packetless firmware on the stock SDK. */
    cfg.ampdu_rx_enable = 0;
    cfg.ampdu_tx_enable = 0;
    if ((e = esp_wifi_init(&cfg)) != ESP_OK) return e;
    if ((e = esp_wifi_set_storage(WIFI_STORAGE_RAM)) != ESP_OK) return e;
    if ((e = esp_wifi_set_mode(WIFI_MODE_NULL)) != ESP_OK) return e;
    if ((e = esp_wifi_start()) != ESP_OK) return e;
    if ((e = esp_wifi_set_ps(WIFI_PS_NONE)) != ESP_OK) return e;
    if ((e = esp_wifi_set_promiscuous(true)) != ESP_OK) return e;
    if ((e = esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE)) != ESP_OK) return e;
    initialized = true;
    return ESP_OK;
}

esp_err_t c5_radio_tune(uint32_t carrier_hz)
{
    /* The command parser and TX validate the nominal band. RX may add the
     * original +/-50 kHz fine adjustment at either edge of that band. */
    if (carrier_hz < 2400950000u || carrier_hz > 2482550000u)
        return ESP_ERR_INVALID_ARG;
    esp_err_t e = c5_radio_init();
    if (e != ESP_OK) return e;
    phy_set_chanfreq(2412, 0);
    return c5_radio_set_frequency_only(carrier_hz);
}

esp_err_t c5_radio_set_frequency_only(uint32_t carrier_hz)
{
    if (!initialized) return ESP_ERR_INVALID_STATE;
    if (carrier_hz < 2400950000u || carrier_hz > 2482550000u)
        return ESP_ERR_INVALID_ARG;
    uint32_t khz = (carrier_hz + 500u) / 1000u;
    unsigned mhz = khz / 1000u;
    int offset = (int)(khz % 1000u);
    phy_set_rf_freq_offset(phy_param[49], mhz, offset);
    frequency = khz * 1000u;
    return ESP_OK;
}

uint32_t c5_radio_frequency_hz(void) { return frequency; }

esp_err_t c5_radio_receive(uint32_t carrier_hz, int32_t fine_hz)
{
    int64_t requested = (int64_t)carrier_hz + fine_hz;
    if (carrier_hz < 2401000000u || carrier_hz > 2482500000u ||
        fine_hz < -50000 || fine_hz > 50000 ||
        requested < 2400950000LL || requested > 2482550000LL)
        return ESP_ERR_INVALID_ARG;
    esp_err_t e = c5_radio_tune((uint32_t)requested);
    if (e != ESP_OK) return e;
    phy_stop_tx_tone(1);
    phy_pbus_workmode();
    phy_pbus_xpd_tx_off();
    phy_pbus_xpd_rx_on(1);
    phy_set_rxclk_en(1);
    phy_force_rx_gain(0, 0);
    phy_enable_agc();
    /* Packet AGC hunts on continuous analog MPX and varied its signal
     * level enough to corrupt RDS. Use calibrated hardware gain/DC rows,
     * holding each level until the IQ-side slow peak controller changes it.
     * Keep free-running hardware AGC enabled before the final force1 only. */
    unsigned maximum=(REG_READ(0x600a702cu)>>8)&127u;
    if(maximum>83u)maximum=83u;
    gain_maximum=(uint8_t)maximum;
    gain_automatic=gain_override<0;
    unsigned selected=gain_automatic?40u:(unsigned)gain_override;
    if(selected>maximum)selected=maximum;
    gain_index=(uint8_t)selected;
    phy_force_rx_gain(0,selected);
    esp_rom_delay_us(2);
    phy_force_rx_gain(1,selected);
    vTaskDelay(pdMS_TO_TICKS(20));
    return ESP_OK;
}

void c5_radio_idle(void)
{
    if (!initialized) return;
    phy_stop_tx_tone(1);
    phy_pbus_xpd_tx_off();
    phy_pbus_xpd_rx_on(0);
    phy_set_rxclk_en(0);
}
