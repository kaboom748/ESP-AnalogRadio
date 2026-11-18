/* SPDX-License-Identifier: GPL-3.0-or-later
 * Classic ESP32 native tone NCO and calibrated PA/BB stages.
 * Private ABI pinned to Arduino-ESP32 3.3.12 / IDF 5.5.5 libphy.a:
 * c2521071695734afa8f6f1ca3d3b604a1d08dd63fe52060449a11cfe8bac5740.
 * Control dispatch follows txcal_debuge_mode()/wifiscwout(); hot register
 * layout was checked against both ESP32 rev0 and rev300 ROMs. No C6
 * register, analogue LO selector, or PA table is used on classic ESP32.
 */
#include "e32_tx_radio.h"
#include "e32_radio.h"
#include "esp_idf_version.h"
#include "esp_phy_cert_test.h"
#include "soc/soc.h"
#include "soc/dport_access.h"

#if !CONFIG_IDF_TARGET_ESP32 || ESP_IDF_VERSION != ESP_IDF_VERSION_VAL(5,5,5)
#error "Use ESP32 Dev Module with Arduino-ESP32 3.3.12"
#endif

extern "C" {
extern void **g_phyFuns;
extern uint8_t chip7_sleep_params[];
extern uint16_t tx_rf_ana_gain[2];
void rom_pbus_xpd_tx_off(void);
unsigned ram_txbbgain_to_index(unsigned);
}

namespace {
/* ESP32 ROM rom_start_tx_tone_step: peripheral aliases also exist at
 * 0x3ff460b8/0x3ff460bc/0x3ff450a8. Use the ROM's exact address plan. */
constexpr uint32_t Tone1=0x600060b8u,Tone2=0x600060bcu,Fraction=0x600050a8u;
constexpr uint32_t Gate=1u<<18,FineMode=1u<<29,Fields=0x0fffffffu;
/* Exact classic correct_rf_ana_gain_new() RF stages. The seventeenth
 * rodata entry is a lower sentinel, not an additional RF stage. Units qdB. */
constexpr uint16_t NativeRf[]={0x7f,0x6f,0x5f,0x7a,0x5a,0x69,0x75,0x55,0x74,0x54,0x44,0x60,0x40,0x20,0x10,0};
constexpr int16_t NativePower[]={10,5,0,-8,-18,-25,-34,-44,-55,-65,-71,-84,-96,-113,-127,-151};
constexpr int ToneMargin=12;
static DRAM_ATTR uint32_t words[8],fractions[8],mutedWord;
static DRAM_ATTR uint16_t dco[4],rfGain,bbGain,ncoAttenuation;
static uint32_t savedTone1,savedTone2,savedFraction,loHz;
static DRAM_ATTR bool owned,keyed;
inline __attribute__((always_inline)) void fence(){asm volatile("memw":::"memory");}
void phy_clock(unsigned on){((void (*)(unsigned))g_phyFuns[0x3cu/4u])(on);}
void phy_work(){((void (*)(void))g_phyFuns[0x124u/4u])();}
}

extern "C" bool e32_tx_radio_prepare(uint32_t carrier_hz,uint8_t apwr) {
    if(carrier_hz<2401000000u || carrier_hz>2482500000u || apwr>191u)return false;
    e32_tx_radio_release();
    if(e32_radio_init()!=ESP_OK)return false;
    savedTone1=DPORT_REG_READ(Tone1);savedTone2=DPORT_REG_READ(Tone2);savedFraction=DPORT_REG_READ(Fraction);
    owned=true;
    int channel=(int)((int64_t(carrier_hz)-2407000000LL+2500000LL)/5000000LL);
    if(channel<1)channel=1;if(channel>13)channel=13;
    /* Calibrate native RF/BB/DCO for the actual channel, then close the
     * analogue frontend while the UART ring is primed and READY is sent. */
    esp_phy_wifi_tx_tone(1u,unsigned(channel),80u);
    REG_WRITE(Tone1,DPORT_REG_READ(Tone1)&~Gate);
    REG_WRITE(Tone2,DPORT_REG_READ(Tone2)&~Gate);
    rom_pbus_xpd_tx_off();
    const int8_t calibrated=(int8_t)(43u-DPORT_REG_READ(0x3ff46004u));
    const int baseline=(calibrated<0?0:int(calibrated))+ToneMargin;
    unsigned initial=16u;
    for(unsigned n=0;n<16u;++n)if(NativeRf[n]==tx_rf_ana_gain[0]){initial=n;break;}
    if(initial==16u){e32_tx_radio_release();return false;}
    unsigned stage=initial;
    for(unsigned n=initial;n<16u;++n)
        if(baseline+int(apwr)+NativePower[n]-NativePower[initial]>=ToneMargin)stage=n;
    const int attenuation=baseline+int(apwr)+NativePower[stage]-NativePower[initial];
    if(attenuation<0 || attenuation>127){e32_tx_radio_release();return false;}
    rfGain=NativeRf[stage];bbGain=tx_rf_ana_gain[1];ncoAttenuation=uint16_t(attenuation);
    const unsigned bbIndex=ram_txbbgain_to_index(bbGain);
    if(bbIndex>4u){e32_tx_radio_release();return false;}
    /* Native txcal_debuge_mode(): five calibrated four-u16 DCO rows. */
    const unsigned dc=12u+8u*bbIndex;
    for(unsigned n=0;n<4u;n++)dco[n]=uint16_t(chip7_sleep_params[dc+2u*n])|(uint16_t(chip7_sleep_params[dc+2u*n+1u])<<8);
    esp_phy_wifi_tx_tone(0u,0u,0u);rom_pbus_xpd_tx_off();phy_clock(0u);
    if(e32_radio_set_frequency_only(carrier_hz-312500u)!=ESP_OK){e32_tx_radio_release();return false;}
    loHz=e32_radio_frequency_hz();
    mutedWord=(DPORT_REG_READ(Tone1)&~(Fields|Gate))|FineMode|
        (uint32_t(uint8_t(0u-unsigned(ncoAttenuation)))<<10);
    const uint32_t fraction=DPORT_REG_READ(Fraction)&~3u;
    for(unsigned k=0;k<8u;++k) {
        /* Fine NCO unit39.0625kHz: two codes equal the original ESP8266's
         * 78.125kHz unit. K4 therefore places RF312500Hz above the LO. */
        const uint32_t code=2u*k;
        words[k]=mutedWord|Gate|(code>>2);fractions[k]=fraction|(code&3u);
    }
    REG_WRITE(Tone1,mutedWord);REG_WRITE(Tone2,DPORT_REG_READ(Tone2)&~Gate);
    fence();keyed=false;return true;
}

extern "C" bool e32_tx_radio_key(void) {
    if(!owned || keyed)return false;
    /* Run BEFORE parking the other CPU: patched vendor calls can lock.
     * This is exactly txcal_debuge_mode(0), with the selected RF/BB/DCO
     * cached during prepare instead of changing the SDK's global table. */
    ((void (*)(void))g_phyFuns[0xc0u/4u])();
    ((void (*)(unsigned,unsigned))g_phyFuns[0xd8u/4u])(rfGain,bbGain);
    ((void (*)(const uint16_t*))g_phyFuns[0xdcu/4u])(dco);
    phy_clock(1u);
    ((void (*)(void))g_phyFuns[0x11cu/4u])();
    keyed=true;return true;
}

extern "C" void e32_tx_radio_write_k(unsigned k) {
    if(!keyed || k>=8u)return;
    REG_WRITE(Fraction,fractions[k]);REG_WRITE(Tone1,words[k]);fence();
}

extern "C" void e32_tx_radio_gate_off(void) {
    if(!owned)return;
    /* Safe while CPU0 is parked: no vendor call, read, lock or I2C. */
    REG_WRITE(Tone1,mutedWord);fence();
}

extern "C" void e32_tx_radio_off(void) {
    if(!owned)return;
    e32_tx_radio_gate_off();phy_clock(0u);rom_pbus_xpd_tx_off();keyed=false;
}

extern "C" void e32_tx_radio_release(void) {
    if(!owned)return;
    e32_tx_radio_off();phy_work();rom_pbus_xpd_tx_off();phy_clock(0u);
    REG_WRITE(Tone1,savedTone1&~Gate);REG_WRITE(Tone2,savedTone2&~Gate);
    REG_WRITE(Fraction,savedFraction);fence();owned=false;
}
extern "C" uint32_t e32_tx_radio_lo_hz(void){return loHz;}
extern "C" void e32_tx_radio_power(uint16_t *rf,uint16_t *bb,uint16_t *attenuation) {
    if(rf)*rf=rfGain;if(bb)*bb=bbGain;if(attenuation)*attenuation=ncoAttenuation;
}
