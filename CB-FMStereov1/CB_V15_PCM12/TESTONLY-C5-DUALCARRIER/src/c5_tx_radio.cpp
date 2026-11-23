/* SPDX-License-Identifier: GPL-3.0-or-later
 * CB13 continuous FM: native C5 tone NCO and calibrated PA/BB stages.
 * Private ABI pinned to Arduino-ESP32 3.3.12 / IDF5.5.5, esp-phy-lib
 * e294ff039e26b3486d6c9e5853d24d98ee3300b2. The register/mux sequence
 * follows the qualified CB6 backend, without TDD or receiver gain control.
 */
#include "c5_tx_radio.h"
#include "c5_radio.h"
#include "c5_bands.h"
#include "c5_tx_5g_plan.h"
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
constexpr unsigned ToneCodes=16;
constexpr unsigned SecondToneSteps=2u*C5_RF_SECOND_CARRIER_OFFSET_HZ/C5_TX_NCO_STEP_HZ_X2;
static_assert(SecondToneSteps*C5_TX_NCO_STEP_HZ_X2==2u*C5_RF_SECOND_CARRIER_OFFSET_HZ,
              "Carrier spacing must be an exact NCO step");
constexpr uint16_t NativeRf[]={63,63,55,47,39,31,31,23,15,13,7,5,3,2,1,1,1,1};
constexpr uint16_t NativeBb[]={3,3,3,3,3,3,1,1,1,1,1,1,1,1,1,0,0,0};
constexpr int16_t NativePower[]={30,30,27,23,18,12,0,-9,-21,-29,-44,-53,-67,-77,-91,-103,-103,-103};
constexpr int ToneMargin=32;
constexpr unsigned DualToneBackoff=24; // Quarter-dB units. Two equal tones can add coherently.
struct alignas(16) ToneRegisters {
    uint32_t fraction;
    uint32_t primary;
    uint32_t secondary;
};
static DRAM_ATTR ToneRegisters toneRegisters[ToneCodes];
static DRAM_ATTR uint16_t dco[4],rfGain,bbGain,ncoAttenuation;
static uint32_t savedTone1,savedTone2,savedFraction,savedMux,savedI,savedQ,savedScale,savedTxdc;
static uint64_t loHz;
static bool owned,keyed,band5,useDco;
static unsigned chosenTableIndex;
static const char *prepareError="NOT_STARTED";
inline __attribute__((always_inline)) void fence(){
#if defined(__riscv)
    asm volatile("fence iorw,iorw":::"memory");
#else
    __atomic_thread_fence(__ATOMIC_SEQ_CST); /* host-only RF mock tests */
#endif
}
}

extern "C" bool c5_tx_radio_prepare(uint64_t carrier_hz,uint8_t apwr) {
    prepareError="TX_BAND";
    if(!c5_tx_frequency_valid(carrier_hz) || apwr>191u)return false;
    c5_tx_radio_release();
    band5=c5_frequency_is_5g(carrier_hz);
    chosenTableIndex=UINT32_MAX;
    useDco=false;
    /* The native fine-step carrier K=8 is still LO+312500Hz. All arithmetic
     * stays 64-bit until the private PHY MHz/kHz parameters. */
    const uint64_t requestedLo=carrier_hz-UINT64_C(312500);
    const uint64_t khz=(requestedLo+UINT64_C(500))/UINT64_C(1000);
    const unsigned mhz=(unsigned)(khz/1000u);
    loHz=khz*UINT64_C(1000);
    prepareError="TX_TUNE";
    /* Calibrate at the nominal carrier, then write the lower NCO LO directly.
     * At the lower band edges the LO is outside the user tuning range. */
    if(c5_radio_tune(carrier_hz)!=ESP_OK)return false;
    phy_set_rf_freq_offset(phy_param[49],mhz,(int)(khz%1000u));
    savedTone1=REG_READ(Tone1);savedTone2=REG_READ(Tone2);savedFraction=REG_READ(Fraction);
    savedMux=REG_READ(Mux);savedI=REG_READ(ConstantI);savedQ=REG_READ(ConstantQ);
    savedScale=REG_READ(DacScale);savedTxdc=REG_READ(Txdc);
    owned=true;
    /* C5's private RF-test tone is a PHY bring-up primitive; the 5G path
     * is still uncharacterized FM, despite 5G Wi-Fi TX being supported. */
    const unsigned carrierMHz=(unsigned)((carrier_hz+UINT64_C(500000))/UINT64_C(1000000));
    esp_phy_wifi_tx_tone(1u,carrierMHz,80u);
    REG_WRITE(Tone1,REG_READ(Tone1)&~Gate);REG_WRITE(Tone2,REG_READ(Tone2)&~Gate);
    phy_pbus_xpd_tx_off();
    int8_t digital[32]={};uint16_t bb[32]={},rf[32]={};
    phy_wifi_get_tx_tab_new(carrierMHz,digital,bb,rf,0u);
    if(band5) {
        /* This is the V15 TX migration: do NOT compare 5G PA/BB codes
         * against V13's 2.4G ladder (that caused ERR TX_RF in V14).
         * Take an actual 5 GHz calibrated PA/BB pair from the active PHY.
         * APWR uses NCO attenuation only until measured 5 GHz PA steps exist. */
        c5_tx_5g_plan_t p={};
        prepareError="TX_5G_NO_CAL";
        if(!c5_tx_5g_make_plan(apwr,digital,bb,rf,&p)) {
            c5_tx_radio_release();return false;
        }
        rfGain=p.rf_gain;bbGain=p.bb_gain;
        ncoAttenuation=p.attenuation;
        chosenTableIndex=p.table_index;
    } else {
        /* Preserve the characterized V13 2.4 GHz calibrated stage ladder. */
        int offset=0;bool found=false;
        for(unsigned n=0;n<18u;++n) {
            if(NativeRf[n]==(rf[0]&63u) && NativeBb[n]==bb[0]) {
                offset=int(digital[0])+NativePower[n];found=true;break;
            }
        }
        prepareError="TX_24_CAL_MISMATCH";
        if(!found){c5_tx_radio_release();return false;}
        const int baseline=offset-NativePower[0]+ToneMargin;
        unsigned stage=0;
        for(unsigned n=0;n<18u;++n)
            if(baseline+int(apwr)+NativePower[n]-offset>=ToneMargin)stage=n;
        const int attenuation=baseline+int(apwr)+NativePower[stage]-offset;
        prepareError="TX_24_ATTEN";
        if(attenuation<0 || attenuation>127){c5_tx_radio_release();return false;}
        rfGain=NativeRf[stage];bbGain=NativeBb[stage];
        ncoAttenuation=(uint16_t)attenuation;
        chosenTableIndex=0;
    }
    /* DAC DC trims are read only for BB indices understood by the pinned
     * PHY. Do not reject a 5 GHz gain code for having another BB index and
     * do not apply 2.4 GHz trim bytes in that case. */
    const unsigned bbIndex=phy_txbbgain_to_index(bbGain);
    if(bbIndex<=2u) {
        const unsigned dc=0x98u+4u*bbIndex;
        dco[0]=(uint16_t)phy_param[dc]|((uint16_t)phy_param[dc+1u]<<8);
        dco[1]=(uint16_t)phy_param[dc+2u]|((uint16_t)phy_param[dc+3u]<<8);
        dco[2]=dco[3]=256u;
        useDco=true;
    } else if(!band5) {
        prepareError="TX_24_BB_INDEX";
        c5_tx_radio_release();return false;
    }
    /* Stop the calibration CW tone before streaming MPX. Re-enter the
     * selected 5 GHz PHY channel after this call: do not leave a 2.4 GHz
     * test-mode frequency or its PA setting active by accident. */
    esp_phy_wifi_tx_tone(0u,0u,0u);
    phy_pbus_xpd_tx_off();phy_set_txclk_en(0u);
    if(band5) phy_set_chanfreq(carrierMHz,0u);
    phy_set_rf_freq_offset(phy_param[49],mhz,(int)(khz%1000u));
    REG_WRITE(Txdc,REG_READ(Txdc)&~(1u<<26));
    REG_WRITE(DacScale,REG_READ(DacScale)&0xff0000ffu);
    const unsigned dualAttenuation=ncoAttenuation+DualToneBackoff;
    ncoAttenuation=dualAttenuation<=127u?dualAttenuation:127u;
    const uint32_t gain=(uint32_t)(uint8_t)(0u-(unsigned)ncoAttenuation)<<10;
    const uint32_t common1=(REG_READ(Tone1)&~(Fields|Gate))|FineMode|gain;
    const uint32_t common2=(REG_READ(Tone2)&~(Fields|Gate))|FineMode|gain;
    const uint32_t fraction=REG_READ(Fraction)&~15u;
    for(unsigned k=0;k<ToneCodes;++k) {
        //const unsigned second=k+SecondToneSteps;
//100khz
const int primaryDelta = static_cast<int>(k) - C5_TX_NCO_CARRIER_CODE;
const int secondCenter = C5_TX_NCO_CARRIER_CODE + static_cast<int>(SecondToneSteps);
const int secondCode = secondCenter + 2 * primaryDelta;
const unsigned second = secondCode < 0 ? 0u : static_cast<unsigned>(secondCode);

        toneRegisters[k]={fraction|(k&3u)|((second&3u)<<2),
                          common1|Gate|(k>>2),common2|Gate|(second>>2)};
    }
    REG_WRITE(Tone1,common1);REG_WRITE(Tone2,common2);
    fence();keyed=false;prepareError="OK";return true;
}

extern "C" bool IRAM_ATTR c5_tx_radio_key(void) {
    if(!owned || keyed)return false;
    /* Native primitive opens the complete warm frontend and TX clock. */
    phy_pbus_xpd_tx_on(rfGain,bbGain);
    if(useDco)phy_pbus_set_dco(dco);
    phy_set_txclk_en(1u);
    /* The V13 I2C bit sequence was characterized only on 2.4 GHz.
     * The 5 GHz PA is activated through its calibrated PHY TX pair. */
    if(!band5)phy_i2c_writeReg_Mask(110u,1u,13u,2u,2u,1u);
    REG_WRITE(ConstantI,0u);REG_WRITE(ConstantQ,0u);
    /* Native start_tx_tone clears these two bypass bits; merely gating the
     * NCO did not restore modulation after stop/work mode. */
    REG_WRITE(Mux,REG_READ(Mux)&~3u);fence();keyed=true;return true;
}

extern "C" void IRAM_ATTR c5_tx_radio_write_k(unsigned k) {
    if(!keyed || k>=ToneCodes)return;
    const ToneRegisters &tone=toneRegisters[k];
    REG_WRITE(Fraction,tone.fraction);
    REG_WRITE(Tone1,tone.primary);
    REG_WRITE(Tone2,tone.secondary);
    fence();
}

extern "C" void IRAM_ATTR c5_tx_radio_off(void) {
    if(!owned)return;
    REG_WRITE(Tone1,REG_READ(Tone1)&~Gate);REG_WRITE(Tone2,REG_READ(Tone2)&~Gate);
    REG_WRITE(Mux,REG_READ(Mux)|3u);fence();
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
extern "C" uint64_t c5_tx_radio_lo_hz(void){return loHz;}
extern "C" void c5_tx_radio_power(uint16_t *rf,uint16_t *bb,uint16_t *attenuation) {
    if(rf)*rf=rfGain;
    if(bb)*bb=bbGain;
    if(attenuation)*attenuation=ncoAttenuation;
}

extern "C" const char *c5_tx_radio_last_error(void){return prepareError;}
extern "C" bool c5_tx_radio_is_5g(void){return band5;}
extern "C" unsigned c5_tx_radio_table_index(void){return chosenTableIndex;}
