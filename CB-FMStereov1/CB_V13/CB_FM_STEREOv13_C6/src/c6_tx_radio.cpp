/* SPDX-License-Identifier: GPL-3.0-or-later
 * CB13 continuous FM: native C6 tone NCO and calibrated PA/BB stages.
 * Private ABI pinned to Arduino-ESP32 3.3.12 / IDF5.5.5, esp-phy-lib
 * e294ff039e26b3486d6c9e5853d24d98ee3300b2. C6 libphy SHA256:
 * fdba95c59c03560620c399f43a72a16230a8ab6fced172f3103ac6a29c36f43a.
 * The register/mux sequence
 * follows the qualified CB6 backend, without TDD or receiver gain control.
 */
#include "c6_tx_radio.h"
#include "c6_radio.h"
#include "esp_idf_version.h"
#include "esp_phy_cert_test.h"
#include "soc/soc.h"

#if !CONFIG_IDF_TARGET_ESP32C6 || ESP_IDF_VERSION != ESP_IDF_VERSION_VAL(5,5,5)
#error "Use ESP32C6 Dev Module with Arduino-ESP32 3.3.12"
#endif

extern "C" {
extern uint8_t phy_param[];
void phy_set_freq(unsigned,int);
void set_txclk_en(unsigned);
void pbus_debugmode(void);
void ram_pbus_xpd_tx_on(unsigned,unsigned);
void rom_pbus_xpd_tx_off(void);
void pbus_set_dco(const uint16_t*);
unsigned rom_chip_i2c_readReg(unsigned,unsigned,unsigned);
void rom_chip_i2c_writeReg(unsigned,unsigned,unsigned,unsigned);
void wifi_get_tx_tab(unsigned,int8_t*,uint16_t*,uint16_t*,unsigned);
unsigned txbbgain_to_index(unsigned);
}

namespace {
constexpr uint32_t Tone1=0x600a0420u,Tone2=0x600a0424u,Fraction=0x600a042cu;
constexpr uint32_t Mux=0x600a0414u,ConstantI=0x600a0418u,ConstantQ=0x600a041cu;
constexpr uint32_t Gate=1u<<18,FineMode=1u<<29,Fields=0x0fffffffu;
/* Exact C6 wifi_get_tx_tab() rodata, not the C5 PA/BB table. Power units
 * are quarter-dB; calibrated digital gain is fetched for the actual channel. */
constexpr uint16_t NativeRf[]={63,63,47,39,31,23,14,13,12,7,5,3,2,1,0,0};
constexpr uint16_t NativeBb[]={128,0,0,0,0,0,0,0,0,0,0,0,0,0,128,0};
constexpr int16_t NativePower[]={34,26,20,15,9,0,-16,-22,-28,-35,-45,-59,-69,-83,-99,-107};
constexpr int ToneMargin=12;
static DRAM_ATTR uint32_t words[8],fractions[8];
static DRAM_ATTR uint16_t dco[4],rfGain,bbGain,ncoAttenuation;
static uint32_t savedTone1,savedTone2,savedFraction,savedMux,savedI,savedQ,loHz;
static bool owned,keyed;
inline __attribute__((always_inline)) void fence(){asm volatile("fence iorw,iorw":::"memory");}
}

extern "C" bool c6_tx_radio_prepare(uint32_t carrier_hz,uint8_t apwr) {
    if(carrier_hz<2401000000u || carrier_hz>2482500000u || apwr>191u)return false;
    c6_tx_radio_release();
    if(c6_radio_init()!=ESP_OK)return false;
    /* Match the original carrier K=4 and LO offset312500Hz. Unlike RX,
     * TX's LO may be312.5kHz below the nominal lower radio-band bound. */
    const uint32_t requestedLo=carrier_hz-312500u;
    const uint32_t khz=(requestedLo+500u)/1000u;
    loHz=khz*1000u;
    savedTone1=REG_READ(Tone1);savedTone2=REG_READ(Tone2);savedFraction=REG_READ(Fraction);
    savedMux=REG_READ(Mux);savedI=REG_READ(ConstantI);savedQ=REG_READ(ConstantQ);
    owned=true;
    int channel=(int)((int64_t(carrier_hz)-2407000000LL+2500000LL)/5000000LL);
    if(channel<1)channel=1;if(channel>13)channel=13;
    esp_phy_wifi_tx_tone(1u,unsigned(channel),80u);
    REG_WRITE(Tone1,REG_READ(Tone1)&~Gate);REG_WRITE(Tone2,REG_READ(Tone2)&~Gate);
    /* A digital tone gate is insufficient: close the analog PA while host
     * PCM is being primed and READY is sent. */
    rom_pbus_xpd_tx_off();
    int8_t digital[32];uint16_t bb[32],rf[32];
    wifi_get_tx_tab(unsigned(channel),digital,bb,rf,0u);
    int offset=0;bool found=false;
    for(unsigned n=0;n<16u;++n) {
        if(NativeRf[n]==(rf[0]&63u) && NativeBb[n]==bb[0]) {
            offset=int(digital[0])+NativePower[n];found=true;break;
        }
    }
    if(!found){c6_tx_radio_release();return false;}
    const int baseline=offset-NativePower[0]+ToneMargin;
    unsigned stage=0;
    for(unsigned n=0;n<16u;++n)
        if(baseline+int(apwr)+NativePower[n]-offset>=ToneMargin)stage=n;
    const int attenuation=baseline+int(apwr)+NativePower[stage]-offset;
    /* APWR0..191 is entirely inside the native coarse+NCO range. Reject
     * unexpected SDK/table values rather than silently flatten the knob. */
    if(attenuation<0 || attenuation>127){c6_tx_radio_release();return false;}
    rfGain=NativeRf[stage];bbGain=NativeBb[stage];ncoAttenuation=uint16_t(attenuation);
    const unsigned bbIndex=txbbgain_to_index(bbGain);
    if(bbIndex>1u){c6_tx_radio_release();return false;}
    const unsigned dc=0xa8u+8u*bbIndex;
    for(unsigned n=0;n<4u;n++)dco[n]=uint16_t(phy_param[dc+2u*n])|(uint16_t(phy_param[dc+2u*n+1u])<<8);
    /* Exit the TX calibration mux completely; simple stop-tone left native
     * IQ stuck on a DC pair on the qualified C6. This is outside streaming. */
    esp_phy_wifi_tx_tone(0u,0u,0u);
    rom_pbus_xpd_tx_off();set_txclk_en(0u);
    phy_set_freq(khz/1000u,int(khz%1000u));
    const uint32_t common=(REG_READ(Tone1)&~(Fields|Gate))|FineMode|
        (uint32_t(uint8_t(0u-unsigned(ncoAttenuation)))<<10);
    const uint32_t fraction=REG_READ(Fraction)&~3u;
    for(unsigned k=0;k<8u;++k) {
        /* Fine-mode NCO resolution39.0625kHz; two codes match one original
         * ESP8266 step78.125kHz. K=4 therefore remains312500Hz above LO. */
        const uint32_t code=2u*k;
        words[k]=common|Gate|(code>>2);fractions[k]=fraction|(code&3u);
    }
    REG_WRITE(Tone1,common);REG_WRITE(Tone2,REG_READ(Tone2)&~Gate);
    fence();keyed=false;return true;
}

extern "C" bool IRAM_ATTR c6_tx_radio_key(void) {
    if(!owned || keyed)return false;
    /* Native primitive opens the complete warm frontend and TX clock. */
    pbus_debugmode();ram_pbus_xpd_tx_on(rfGain,bbGain);pbus_set_dco(dco);set_txclk_en(1u);
    /* This is the C6 alternate-LO /6:5 divider, NOT a TX-enable bit.
     * The ordinary2.4GHz plan must remain selected on TX as well as RX. */
    const unsigned route=rom_chip_i2c_readReg(0x62u,1u,16u);
    rom_chip_i2c_writeReg(0x62u,1u,16u,route&~8u);
    REG_WRITE(ConstantI,0u);REG_WRITE(ConstantQ,0u);
    /* Native start_tx_tone clears these two bypass bits; merely gating the
     * NCO did not restore modulation after stop/work mode. */
    REG_WRITE(Mux,REG_READ(Mux)&~3u);fence();keyed=true;return true;
}

extern "C" void IRAM_ATTR c6_tx_radio_write_k(unsigned k) {
    if(!keyed || k>=8u)return;
    REG_WRITE(Fraction,fractions[k]);REG_WRITE(Tone1,words[k]);fence();
}

extern "C" void IRAM_ATTR c6_tx_radio_off(void) {
    if(!owned)return;
    REG_WRITE(Tone1,REG_READ(Tone1)&~Gate);REG_WRITE(Mux,REG_READ(Mux)|3u);fence();
    set_txclk_en(0u);rom_pbus_xpd_tx_off();keyed=false;
}

extern "C" void c6_tx_radio_release(void) {
    if(!owned)return;
    c6_tx_radio_off();esp_phy_wifi_tx_tone(0u,0u,0u);rom_pbus_xpd_tx_off();set_txclk_en(0u);
    REG_WRITE(Tone1,savedTone1&~Gate);REG_WRITE(Tone2,savedTone2&~Gate);
    REG_WRITE(Fraction,savedFraction);
    REG_WRITE(ConstantI,savedI);REG_WRITE(ConstantQ,savedQ);REG_WRITE(Mux,savedMux);
    fence();owned=false;
}
extern "C" uint32_t c6_tx_radio_lo_hz(void){return loHz;}
extern "C" void c6_tx_radio_power(uint16_t *rf,uint16_t *bb,uint16_t *attenuation) {
    if(rf)*rf=rfGain;if(bb)*bb=bbGain;if(attenuation)*attenuation=ncoAttenuation;
}
