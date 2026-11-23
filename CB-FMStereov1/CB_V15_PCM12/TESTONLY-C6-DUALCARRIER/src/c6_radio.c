/* SPDX-License-Identifier: GPL-3.0-or-later
 * C6 PHY bring-up and tuning adapted from ESPARGOS ESP-SDR and
 * ESPsoup contributors / Peter Holzhauser (2026). See project NOTICE.
 */
#include "c6_radio.h"
#include <stdbool.h>
#include "esp_event.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_rom_sys.h"
#include "soc/soc.h"

extern void chip_v7_set_chan(unsigned, unsigned);
extern void phy_set_freq(unsigned, int);
extern void ram_stop_tx_tone(unsigned);
extern void pbus_workmode(void);
extern void ram_pbus_xpd_rx_on(unsigned);
extern void rom_pbus_xpd_tx_off(void);
extern void set_rxclk_en(unsigned);
extern void phy_force_rx_gain(unsigned, unsigned);
extern void enable_agc(void);
extern unsigned rom_chip_i2c_readReg(unsigned, unsigned, unsigned);
extern void rom_chip_i2c_writeReg(unsigned, unsigned, unsigned, unsigned);

#if !CONFIG_IDF_TARGET_ESP32C6
#error "This native PHY adaptation requires ESP32-C6"
#endif

/* C6 native 2.4GHz LO. ESPARGOS qualifies bit3 of this C6-specific
 * selector as the alternate 5/6 receive LO path; CB13 uses the direct LO. */
static void normal_rx_lo(void)
{
    const unsigned old=rom_chip_i2c_readReg(0x62u,1u,16u);
    if(old&8u)rom_chip_i2c_writeReg(0x62u,1u,16u,old&~8u);
}

static bool initialized;
static uint32_t frequency;
static int gain_override=-1;
static DRAM_ATTR bool gain_automatic;
static DRAM_ATTR uint8_t gain_index,gain_maximum;
void c6_radio_set_rx_gain(int index) {gain_override=index;}

bool IRAM_ATTR c6_radio_rx_gain_automatic(void) {return gain_automatic;}
unsigned IRAM_ATTR c6_radio_rx_gain_index(void) {return gain_index;}
unsigned IRAM_ATTR c6_radio_rx_gain_maximum(void) {return gain_maximum;}
bool IRAM_ATTR c6_radio_rx_gain_update(unsigned index) {
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

esp_err_t c6_radio_init(void)
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

esp_err_t c6_radio_tune(uint32_t carrier_hz)
{
    /* The command parser and TX validate the nominal band. RX may add the
     * original +/-50 kHz fine adjustment at either edge of that band. */
    if (carrier_hz < 2400950000u || carrier_hz > 2482550000u)
        return ESP_ERR_INVALID_ARG;
    esp_err_t e = c6_radio_init();
    if (e != ESP_OK) return e;
    normal_rx_lo();
    chip_v7_set_chan(2412, 0);
    return c6_radio_set_frequency_only(carrier_hz);
}

esp_err_t c6_radio_set_frequency_only(uint32_t carrier_hz)
{
    if (!initialized) return ESP_ERR_INVALID_STATE;
    if (carrier_hz < 2400950000u || carrier_hz > 2482550000u)
        return ESP_ERR_INVALID_ARG;
    uint32_t khz = (carrier_hz + 500u) / 1000u;
    unsigned mhz = khz / 1000u;
    int offset = (int)(khz % 1000u);
    /* Pinned C6 wrapper stores signed kHz offset and reads phy_param[81]
     * before calling its native set_rf_freq_offset; C5's[49] is different. */
    phy_set_freq(mhz, offset);
    frequency = khz * 1000u;
    return ESP_OK;
}

uint32_t c6_radio_frequency_hz(void) { return frequency; }

esp_err_t c6_radio_receive(uint32_t carrier_hz, int32_t fine_hz)
{
    int64_t requested = (int64_t)carrier_hz + fine_hz;
    if (carrier_hz < 2401000000u || carrier_hz > 2482500000u ||
        fine_hz < -50000 || fine_hz > 50000 ||
        requested < 2400950000LL || requested > 2482550000LL)
        return ESP_ERR_INVALID_ARG;
    esp_err_t e = c6_radio_tune((uint32_t)requested);
    if (e != ESP_OK) return e;
    ram_stop_tx_tone(1);
    pbus_workmode();
    rom_pbus_xpd_tx_off();
    ram_pbus_xpd_rx_on(1);
    set_rxclk_en(1);
    phy_force_rx_gain(0, 0);
    enable_agc();
    /* Packet AGC hunts on continuous analog MPX and varied its signal
     * level enough to corrupt RDS. Use calibrated hardware gain/DC rows,
     * holding each level until the IQ-side slow peak controller changes it.
     * Keep free-running hardware AGC enabled before the final force1 only. */
    unsigned maximum=(REG_READ(0x600a702cu)>>8)&127u;
    /* C6 calibrated low-table range is below80 (unlike C5's up-to83).
     * Reject an absent/unexpected table rather than selecting an uncalibrated row. */
    if(!maximum || maximum>=80u) {c6_radio_idle();return ESP_ERR_INVALID_STATE;}
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

void c6_radio_idle(void)
{
    if (!initialized) return;
    ram_stop_tx_tone(1);
    rom_pbus_xpd_tx_off();
    ram_pbus_xpd_rx_on(0);
    set_rxclk_en(0);
}
