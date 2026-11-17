/* SPDX-License-Identifier: GPL-3.0-or-later
 * CB13 continuous FM: native C5 tone NCO and calibrated PA/BB stages.
 * Private ABI pinned to Arduino-ESP32 3.3.12 / IDF5.5.5, esp-phy-lib
 * e294ff039e26b3486d6c9e5853d24d98ee3300b2. The register/mux sequence
 * follows the qualified CB6 backend, without TDD or receiver gain control.
 */
#include "c5_tx_radio.h"
#include "c5_radio.h"
#include "esp_idf_version.h"
#include "esp_phy_cert_test.h"
#include "soc/soc.h"

#if !CONFIG_IDF_TARGET_ESP32C5 || ESP_IDF_VERSION != ESP_IDF_VERSION_VAL(5,5,5)
#error "Use ESP32C5 Dev Module with Arduino-ESP32 3.3.12"
#endif

extern "C" {
extern uint8_t phy_param[];
void phy_set_chanfreq(unsigned,unsigned);
void phy_set_rf_freq_offset(unsigned,unsigned,int);
void phy_stop_tx_tone(unsigned);
void phy_set_txclk_en(unsigned);
void phy_pbus_xpd_tx_on(unsigned,unsigned);
void phy_pbus_xpd_tx_off(void);
void phy_pbus_set_dco(const uint16_t*);
void phy_i2c_writeReg_Mask(unsigned,unsigned,unsigned,unsigned,unsigned,unsigned);
void phy_wifi_get_tx_tab_new(unsigned,int8_t*,uint16_t*,uint16_t*,unsigned);
unsigned phy_txbbgain_to_index(unsigned);
}

namespace {
constexpr uint32_t Tone1=0x600a041cu,Tone2=0x600a0420u,Fraction=0x600a0428u;
constexpr uint32_t Mux=0x600a040cu,ConstantI=0x600a0410u,ConstantQ=0x600a0414u;
constexpr uint32_t DacScale=0x600a0c04u,Txdc=0x600a0c08u;
constexpr uint32_t Gate=1u<<18,FineMode=1u<<29,Fields=0x0003ffffu;
constexpr uint16_t NativeRf[]={63,63,55,47,39,31,31,23,15,13,7,5,3,2,1,1,1,1};
constexpr uint16_t NativeBb[]={3,3,3,3,3,3,1,1,1,1,1,1,1,1,1,0,0,0};
constexpr int16_t NativePower[]={30,30,27,23,18,12,0,-9,-21,-29,-44,-53,-67,-77,-91,-103,-103,-103};
constexpr int ToneMargin=32;
static DRAM_ATTR uint32_t words[8],fractions[8];
static DRAM_ATTR uint16_t dco[4],rfGain,bbGain,ncoAttenuation;
static uint32_t savedTone1,savedTone2,savedFraction,savedMux,savedI,savedQ,savedScale,savedTxdc,loHz;
static bool owned,keyed;
inline __attribute__((always_inline)) void fence(){asm volatile("fence iorw,iorw":::"memory");}
}

extern "C" bool c5_tx_radio_prepare(uint32_t carrier_hz,uint8_t apwr) {
    if(carrier_hz<2401000000u || carrier_hz>2482500000u || apwr>191u)return false;
    c5_tx_radio_release();
    if(c5_radio_init()!=ESP_OK)return false;
    /* Match the original carrier K=4 and LO offset312500Hz. Unlike RX,
     * TX's LO may be312.5kHz below the nominal lower radio-band bound. */
    const uint32_t requestedLo=carrier_hz-312500u;
    const uint32_t khz=(requestedLo+500u)/1000u;
    loHz=khz*1000u;
    phy_set_chanfreq(2412u,0u);
    phy_set_rf_freq_offset(phy_param[49],khz/1000u,int(khz%1000u));
    savedTone1=REG_READ(Tone1);savedTone2=REG_READ(Tone2);savedFraction=REG_READ(Fraction);
    savedMux=REG_READ(Mux);savedI=REG_READ(ConstantI);savedQ=REG_READ(ConstantQ);
    savedScale=REG_READ(DacScale);savedTxdc=REG_READ(Txdc);
    owned=true;
    esp_phy_wifi_tx_tone(1u,(carrier_hz+500000u)/1000000u,80u);
    REG_WRITE(Tone1,REG_READ(Tone1)&~Gate);REG_WRITE(Tone2,REG_READ(Tone2)&~Gate);
    /* A digital tone gate is insufficient: close the analog PA while host
     * PCM is being primed and READY is sent. */
    phy_pbus_xpd_tx_off();
    int8_t digital[32];uint16_t bb[32],rf[32];
    phy_wifi_get_tx_tab_new((carrier_hz+500000u)/1000000u,digital,bb,rf,0u);
    int offset=0;bool found=false;
    for(unsigned n=0;n<18u;++n) {
        if(NativeRf[n]==(rf[0]&63u) && NativeBb[n]==bb[0]) {
            offset=int(digital[0])+NativePower[n];found=true;break;
        }
    }
    if(!found){c5_tx_radio_release();return false;}
    const int baseline=offset-NativePower[0]+ToneMargin;
    unsigned stage=0;
    for(unsigned n=0;n<18u;++n)
        if(baseline+int(apwr)+NativePower[n]-offset>=ToneMargin)stage=n;
    const int attenuation=baseline+int(apwr)+NativePower[stage]-offset;
    /* APWR0..191 is entirely inside the native coarse+NCO range. Reject
     * unexpected SDK/table values rather than silently flatten the knob. */
    if(attenuation<0 || attenuation>127){c5_tx_radio_release();return false;}
    rfGain=NativeRf[stage];bbGain=NativeBb[stage];ncoAttenuation=uint16_t(attenuation);
    const unsigned bbIndex=phy_txbbgain_to_index(bbGain);
    if(bbIndex>2u){c5_tx_radio_release();return false;}
    const unsigned dc=0x98u+4u*bbIndex;
    dco[0]=uint16_t(phy_param[dc])|(uint16_t(phy_param[dc+1u])<<8);
    dco[1]=uint16_t(phy_param[dc+2u])|(uint16_t(phy_param[dc+3u])<<8);
    dco[2]=dco[3]=256u;
    /* Exit the TX calibration mux completely; simple stop-tone left native
     * IQ stuck on a DC pair on the qualified C5. This is outside streaming. */
    esp_phy_wifi_tx_tone(0u,0u,0u);
    phy_pbus_xpd_tx_off();phy_set_txclk_en(0u);
    phy_set_rf_freq_offset(phy_param[49],khz/1000u,int(khz%1000u));
    REG_WRITE(Txdc,REG_READ(Txdc)&~(1u<<26));
    REG_WRITE(DacScale,REG_READ(DacScale)&0xff0000ffu);
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

extern "C" bool IRAM_ATTR c5_tx_radio_key(void) {
    if(!owned || keyed)return false;
    /* Native primitive opens the complete warm frontend and TX clock. */
    phy_pbus_xpd_tx_on(rfGain,bbGain);phy_pbus_set_dco(dco);phy_set_txclk_en(1u);
    phy_i2c_writeReg_Mask(110u,1u,13u,2u,2u,1u);
    REG_WRITE(ConstantI,0u);REG_WRITE(ConstantQ,0u);
    /* Native start_tx_tone clears these two bypass bits; merely gating the
     * NCO did not restore modulation after stop/work mode. */
    REG_WRITE(Mux,REG_READ(Mux)&~3u);fence();keyed=true;return true;
}

extern "C" void IRAM_ATTR c5_tx_radio_write_k(unsigned k) {
    if(!keyed || k>=8u)return;
    REG_WRITE(Fraction,fractions[k]);REG_WRITE(Tone1,words[k]);fence();
}

extern "C" void IRAM_ATTR c5_tx_radio_off(void) {
    if(!owned)return;
    REG_WRITE(Tone1,REG_READ(Tone1)&~Gate);REG_WRITE(Mux,REG_READ(Mux)|3u);fence();
    phy_set_txclk_en(0u);phy_pbus_xpd_tx_off();keyed=false;
}

extern "C" void c5_tx_radio_release(void) {
    if(!owned)return;
    c5_tx_radio_off();esp_phy_wifi_tx_tone(0u,0u,0u);phy_pbus_xpd_tx_off();phy_set_txclk_en(0u);
    REG_WRITE(Tone1,savedTone1&~Gate);REG_WRITE(Tone2,savedTone2&~Gate);
    REG_WRITE(Fraction,savedFraction);REG_WRITE(DacScale,savedScale);REG_WRITE(Txdc,savedTxdc);
    REG_WRITE(ConstantI,savedI);REG_WRITE(ConstantQ,savedQ);REG_WRITE(Mux,savedMux);
    fence();owned=false;
}
extern "C" uint32_t c5_tx_radio_lo_hz(void){return loHz;}
extern "C" void c5_tx_radio_power(uint16_t *rf,uint16_t *bb,uint16_t *attenuation) {
    if(rf)*rf=rfGain;if(bb)*bb=bbGain;if(attenuation)*attenuation=ncoAttenuation;
}
