/* SPDX-License-Identifier: GPL-3.0-or-later
 * Classic ESP32 PHY bring-up and tuning adapted from ESPARGOS ESP-SDR and
 * ESPsoup contributors / Peter Holzhauser (2026). See project NOTICE.
 */
#include "e32_radio.h"
#include <stdbool.h>
#include "esp_event.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_rom_sys.h"
#include "esp_idf_version.h"
#include "esp_private/phy.h"
#include <sys/lock.h>
#include "soc/soc.h"
#include "soc/rtc.h"
#include "soc/dport_access.h"
#include "dport_access.h"

extern void set_chanfreq(unsigned, unsigned);
extern void rom_set_rf_freq_offset(unsigned, unsigned, int);
extern void rom_stop_tx_tone(unsigned);
extern void rom_pbus_workmode(void);
extern void rom_pbus_xpd_rx_on(unsigned);
extern void rom_pbus_xpd_tx_off(void);
extern void ram_set_rxclk_en(unsigned);
extern void rom_rx_gain_force(unsigned, unsigned);
extern void ram_enable_agc(void);
extern unsigned ram_chip_i2c_readReg(unsigned, unsigned, unsigned);
extern void ram_chip_i2c_writeReg(unsigned, unsigned, unsigned, unsigned);
extern void set_rx_gain_table(unsigned, unsigned);
extern uint32_t chip7_sleep_params[];
extern void **g_phyFuns;

#if !CONFIG_IDF_TARGET_ESP32
#error "This native PHY adaptation requires ESP32"
#endif
#if ESP_IDF_VERSION != ESP_IDF_VERSION_VAL(5,5,5)
#error "Classic ESP32 PHY layout requires Arduino-ESP32 3.3.12 / IDF5.5.5"
#endif

/* Classic ESP32 direct 2.4 GHz LO: its selector is a different analogue
 * block, host and bit from the C6 selector. */
static void normal_rx_lo(void)
{
    const unsigned old=ram_chip_i2c_readReg(0x65u,4u,0u);
    if(old&0x10u)ram_chip_i2c_writeReg(0x65u,4u,0u,old&~0x10u);
}

#define RX_GAIN 0x3ff5c02cu

/* These two pre-ECO3 ROM routines perform only bounded MMIO; they neither
 * take RTOS locks nor call the RAM PHY table. Their raw DPORT accesses need
 * a parked peer CPU on the dual-core ESP32. Keep the stall narrow: channel
 * calibration, RAM I2C, AGC/clock setup and all delays stay outside it. */
static void stop_tx_tone(void)
{
    DPORT_STALL_OTHER_CPU_START();
    rom_stop_tx_tone(1);
    DPORT_STALL_OTHER_CPU_END();
}

static void force_rx_gain(unsigned enabled,unsigned index)
{
    DPORT_STALL_OTHER_CPU_START();
    rom_rx_gain_force(enabled,index);
    DPORT_STALL_OTHER_CPU_END();
}

static bool initialized;
static uint32_t frequency;

/* Stock3.3.12 libphy set_channel_rfpll_freq checks sleep_params bit4:
 * when clear it dispatches through g_phyFuns[0xf4/4]. DC calibration calls
 * chip_v7_set_chan_ana directly, which reaches exactly that dispatcher.
 * A scoped PHY-table hook supplies the same per-measurement LO override
 * as a linker --wrap, while keeping the sketch compatible with plain IDE
 * builds (Arduino's build_opt.h is not passed to the linker).
 * All hook state is protected by the SDK PHY lock, never used hot. */
#define PHY_PLL_CHANNEL_SLOT (0xf4u/4u)
#define PHY_PLL_CHANNEL_CACHE (1u<<4)
#define PHY_IQ_VALID (1u<<10)
#define PHY_DC_TABLE_VALID (0x20000u|0x200u|0x100u|0x20u)
typedef unsigned (*phy_channel_fn_t)(int,unsigned,int);
static phy_channel_fn_t calibration_channel_original;
static uint32_t calibration_lo_hz;
static unsigned calibration_calls;
static esp_err_t calibration_error;

#if CB13_BENCH_IQ
/* Audited wr_rx_gain_mem calls slot13(low32,high32,index). The original
 * native low word packs its 15-bit RF/BB gain code in bits31:17. Wi-Fi
 * uses indices0..127; the separate BT bank starts at128. */
#define PHY_GAIN_WRITE_SLOT (0x34u/4u)
typedef void (*phy_gain_write_fn_t)(uint32_t,uint32_t,unsigned);
static phy_gain_write_fn_t bench_gain_write_original;
static uint16_t bench_gain_codes[128];
static bool bench_gain_valid[128];
static bool bench_reinitialize_enabled=true;
static bool bench_hardware_agc_enabled;
static uint32_t bench_if_hz;

static void bench_gain_write(uint32_t low,uint32_t high,unsigned index)
{
    if(index<128u) {
        bench_gain_codes[index]=(uint16_t)(low>>17);
        bench_gain_valid[index]=true;
    }
    bench_gain_write_original(low,high,index);
}
#endif

static unsigned calibration_channel(int channel,unsigned crystal,int mode)
{
    const unsigned result=calibration_channel_original(channel,crystal,mode);
    ++calibration_calls;
    if(calibration_error==ESP_OK)
        calibration_error=e32_radio_set_frequency_only(calibration_lo_hz);
    return result;
}

static esp_err_t recalibrate_receive_dc(uint32_t lo_hz)
{
    _lock_t lock=phy_get_lock();
    _lock_acquire(&lock);
    /* IQ-valid0x400 skips set_rx_gain_cal_iq and its radiated loopback
     * tone in this exact SDK. Never manufacture an IQ-valid flag when
     * startup did not install calibrated coefficients. */
    if(!(chip7_sleep_params[0]&PHY_IQ_VALID) || !g_phyFuns ||
       !g_phyFuns[PHY_PLL_CHANNEL_SLOT]) {
        _lock_release(&lock);
        return ESP_ERR_INVALID_STATE;
    }
#if CB13_BENCH_IQ
    if(!g_phyFuns[PHY_GAIN_WRITE_SLOT]) {
        _lock_release(&lock);
        return ESP_ERR_INVALID_STATE;
    }
    for(unsigned index=0;index<128u;++index)bench_gain_valid[index]=false;
    bench_gain_write_original=(phy_gain_write_fn_t)g_phyFuns[PHY_GAIN_WRITE_SLOT];
    g_phyFuns[PHY_GAIN_WRITE_SLOT]=(void *)bench_gain_write;
#endif
    const uint32_t saved_pll_mode=chip7_sleep_params[0]&PHY_PLL_CHANNEL_CACHE;
    calibration_channel_original=(phy_channel_fn_t)g_phyFuns[PHY_PLL_CHANNEL_SLOT];
    calibration_lo_hz=lo_hz;
    calibration_calls=0;
    calibration_error=ESP_OK;
    g_phyFuns[PHY_PLL_CHANNEL_SLOT]=(void *)calibration_channel;
    __asm__ volatile("memw":::"memory");
    /* Invalidate only BT/Wi-Fi DC, gain-code and gain-memory caches.
     * Preserve installed IQ coefficients and every unrelated state bit. */
    chip7_sleep_params[0]&=~(PHY_DC_TABLE_VALID|PHY_PLL_CHANNEL_CACHE);
    __asm__ volatile("memw":::"memory");
    set_rx_gain_table((lo_hz+500000u)/1000000u,0u);
#if CB13_BENCH_IQ
    g_phyFuns[PHY_GAIN_WRITE_SLOT]=(void *)bench_gain_write_original;
#endif
    g_phyFuns[PHY_PLL_CHANNEL_SLOT]=(void *)calibration_channel_original;
    chip7_sleep_params[0]=(chip7_sleep_params[0]&~PHY_PLL_CHANNEL_CACHE)|saved_pll_mode;
    __asm__ volatile("memw":::"memory");
    const esp_err_t result=calibration_error!=ESP_OK?calibration_error:
        (!calibration_calls || !(chip7_sleep_params[0]&PHY_IQ_VALID))?
        ESP_ERR_INVALID_STATE:e32_radio_set_frequency_only(lo_hz);
    calibration_channel_original=NULL;
#if CB13_BENCH_IQ
    bench_gain_write_original=NULL;
#endif
    _lock_release(&lock);
    return result;
}

static unsigned nearest_wifi_mhz(uint32_t hz)
{
    unsigned channel=hz<=2412000000u?1u:
        1u+(hz-2412000000u+2500000u)/5000000u;
    if(channel>13u)channel=13u;
    const unsigned mhz=2407u+5u*channel;
    /* Channel14 is a valid calibration reference at the upper band edge. */
    return hz>(mhz+2484u)*500000u?2484u:mhz;
}
static int gain_override=-1;
static DRAM_ATTR bool gain_automatic;
static DRAM_ATTR uint8_t gain_index,gain_maximum;
void e32_radio_set_rx_gain(int index) {gain_override=index;}

bool e32_radio_rx_gain_automatic(void) {return gain_automatic;}
unsigned e32_radio_rx_gain_index(void) {return gain_index;}
unsigned e32_radio_rx_gain_maximum(void) {return gain_maximum;}
bool e32_radio_rx_gain_update(unsigned index) {
    if(index>gain_maximum)index=gain_maximum;
    if(index==gain_index)return false;
    /* RX start already asserted force23. With the peer parked and no
     * other writer, update index and keep force1 in one ordered RMW.
     * The ROM's second force1 RMW would rewrite this identical word;
     * avoiding it keeps the calibrated gain step within the IQ deadline. */
    /* Called with local interrupts masked by the streaming runtime.
     * Sequence reads include the classic ESP32 APB/DPORT workaround. */
    uint32_t word=(DPORT_SEQUENCE_REG_READ(RX_GAIN)&0x00ffffffu)|(index<<24)|(1u<<23);
    REG_WRITE(RX_GAIN,word);
    __asm__ volatile("memw":::"memory");
    gain_index=(uint8_t)index;
    return true;
}

/* Required by Espressif's RF test archive; this port exposes no RF shell. */
__attribute__((weak)) int cmd_parse(char *cmd, char *name, int *argc, char **argv)
{ (void)cmd; (void)name; (void)argc; (void)argv; return -1; }

esp_err_t e32_radio_init(void)
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

esp_err_t e32_radio_tune(uint32_t carrier_hz)
{
    /* The command parser and TX validate the nominal band. RX may add the
     * original +/-50 kHz fine adjustment at either edge of that band. */
    if (carrier_hz < 2400950000u || carrier_hz > 2482550000u)
        return ESP_ERR_INVALID_ARG;
    esp_err_t e = e32_radio_init();
    if (e != ESP_OK) return e;
    normal_rx_lo();
    set_chanfreq(nearest_wifi_mhz(carrier_hz),0);
    return e32_radio_set_frequency_only(carrier_hz);
}

esp_err_t e32_radio_set_frequency_only(uint32_t carrier_hz)
{
    if (!initialized) return ESP_ERR_INVALID_STATE;
    /* TX's minimum requested LO is 2401 MHz minus its 312.5 kHz NCO. */
    if (carrier_hz < 2400687500u || carrier_hz > 2482550000u)
        return ESP_ERR_INVALID_ARG;
    unsigned mhz=carrier_hz/1000000u;
    unsigned offset=(unsigned)(((uint64_t)(carrier_hz%1000000u)*1024u+500000u)/1000000u);
    if(offset==1024u) {++mhz;offset=0;}
    unsigned xtal=rtc_clk_xtal_freq_get();
    if(xtal!=40u&&xtal!=26u&&xtal!=24u)return ESP_ERR_NOT_SUPPORTED;
    /* Channel calibration forces a capacitor code. Release it before
     * direct PLL retuning; otherwise its SDM can change without locking. */
    unsigned cap=ram_chip_i2c_readReg(0x62u,1u,0u);
    ram_chip_i2c_writeReg(0x62u,1u,0u,cap&~0x80u);
    /* Classic ROM units are 1/1024 MHz, not the C6's integer kHz. */
    rom_set_rf_freq_offset(xtal==26u?1u:xtal==24u?2u:0u,mhz,(int)offset);
    frequency=(uint32_t)((((uint64_t)mhz*1024u+offset)*1000000u+512u)/1024u);
    return ESP_OK;
}

uint32_t e32_radio_frequency_hz(void) { return frequency; }

esp_err_t e32_radio_receive(uint32_t carrier_hz, int32_t fine_hz)
{
    int64_t requested = (int64_t)carrier_hz + fine_hz;
    if (carrier_hz < 2401000000u || carrier_hz > 2482500000u ||
        fine_hz < -50000 || fine_hz > 50000 ||
        requested < 2400950000LL || requested > 2482550000LL)
        return ESP_ERR_INVALID_ARG;
    esp_err_t e = e32_radio_tune((uint32_t)requested);
    if (e != ESP_OK) return e;
#if CB13_BENCH_IQ
    if(bench_if_hz) {
        /* Nominal band/fine validation above is unchanged. The existing
         * PLL-only path already accepts the lowest experimental RX LO. */
        requested-=bench_if_hz;
        e=e32_radio_set_frequency_only((uint32_t)requested);
        if(e!=ESP_OK) {e32_radio_idle();return e;}
    }
#endif
    stop_tx_tone();
    rom_pbus_xpd_tx_off();
    force_rx_gain(0,0);
    e=recalibrate_receive_dc((uint32_t)requested);
    if(e!=ESP_OK) {e32_radio_idle();return e;}
#if CB13_BENCH_IQ
    if(bench_reinitialize_enabled)
#endif
    {
        /* Match the reference's receive order: fresh calibration first,
         * then native channel/BB setup, followed by the fractional LO.
         * Restore the complete native receive setup after DC calibration. */
        set_chanfreq(nearest_wifi_mhz((uint32_t)requested),0u);
        e=e32_radio_set_frequency_only((uint32_t)requested);
        if(e!=ESP_OK) {e32_radio_idle();return e;}
    }
    rom_pbus_workmode();
    rom_pbus_xpd_tx_off();
    rom_pbus_xpd_rx_on(1);
    ram_set_rxclk_en(1);
    force_rx_gain(0, 0);
    ram_enable_agc();
    /* Packet AGC hunts on continuous analog MPX and varied its signal
     * level enough to corrupt RDS. Use calibrated hardware gain/DC rows,
     * holding each level until the native ADC-power controller changes it.
     * Keep free-running hardware AGC enabled before the final force1 only. */
    unsigned maximum=(DPORT_REG_READ(RX_GAIN)>>8)&127u;
    /* The classic table has its own calibrated bound (it can exceed the
     * C6's range). Never substitute an index from a different chip. */
    if(!maximum) {e32_radio_idle();return ESP_ERR_INVALID_STATE;}
    gain_maximum=(uint8_t)maximum;
    gain_automatic=gain_override<0;
#if CB13_BENCH_IQ
    if(bench_hardware_agc_enabled)gain_automatic=false;
#endif
    unsigned selected=gain_automatic?40u:(unsigned)gain_override;
#if CB13_BENCH_IQ
    if(bench_hardware_agc_enabled)selected=0u;
#endif
    if(selected>maximum)selected=maximum;
    gain_index=(uint8_t)selected;
#if CB13_BENCH_IQ
    if(bench_hardware_agc_enabled) {
        /* Native reference RX mode: AGC was enabled above, now release
         * the forced index. No manual/software gain update follows. */
        force_rx_gain(0,0);
    } else
#endif
    {
        force_rx_gain(0,selected);
        esp_rom_delay_us(2);
        force_rx_gain(1,selected);
    }
#if CB13_BENCH_IQ
    if(bench_reinitialize_enabled)
#endif
    normal_rx_lo();
    vTaskDelay(pdMS_TO_TICKS(20));
    return ESP_OK;
}

void e32_radio_idle(void)
{
    if (!initialized) return;
    stop_tx_tone();
    rom_pbus_xpd_tx_off();
    rom_pbus_xpd_rx_on(0);
    ram_set_rxclk_en(0);
}

#if CB13_BENCH_IQ
void e32_radio_bench_reinitialize(bool enabled)
{
    bench_reinitialize_enabled=enabled;
}

void e32_radio_bench_hardware_agc(bool enabled)
{
    bench_hardware_agc_enabled=enabled;
}

bool e32_radio_bench_if(unsigned hz)
{
    if(hz!=0u&&hz!=192000u&&hz!=240000u&&hz!=312500u)return false;
    bench_if_hz=hz;
    return true;
}

bool e32_radio_bench_gain_code(unsigned index,uint16_t *code)
{
    if(index>=128u||!code||!initialized)return false;
    _lock_t lock=phy_get_lock();
    _lock_acquire(&lock);
    const bool valid=bench_gain_valid[index];
    if(valid)*code=bench_gain_codes[index];
    _lock_release(&lock);
    return valid;
}

esp_err_t e32_radio_bench_readback(e32_radio_bench_readback_t *readback)
{
    if(!readback)return ESP_ERR_INVALID_ARG;
    if(!initialized)return ESP_ERR_INVALID_STATE;
    _lock_t lock=phy_get_lock();
    _lock_acquire(&lock);
    readback->lo_control=ram_chip_i2c_readReg(0x65u,4u,0u);
    readback->filter_i=ram_chip_i2c_readReg(0x67u,1u,1u);
    readback->filter_q=ram_chip_i2c_readReg(0x67u,1u,2u);
    readback->calibration_flags=chip7_sleep_params[0];
    _lock_release(&lock);
    return ESP_OK;
}
#endif
