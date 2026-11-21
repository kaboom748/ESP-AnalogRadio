/* SPDX-License-Identifier: GPL-3.0-or-later
 * Warm TDD PHY for the original ESP32. The private PHY ABI and native PA
 * calibration follow the qualified ESP_PICTURE_V2 classic backend.
 */
#include "e32_cb_radio.h"
#include "arduino_runtime.h"
#include "e32_radio.h"
#include "esp_cpu.h"
#include "esp_idf_version.h"
#include "esp_phy_cert_test.h"
#include "esp_rom_sys.h"
#include "esp_wifi.h"
#include "soc/dport_access.h"
#include "soc/soc.h"

#if !CONFIG_IDF_TARGET_ESP32 || ESP_IDF_VERSION != ESP_IDF_VERSION_VAL(5,5,5)
#error "Use ESP32 Dev Module with Arduino-ESP32 3.3.12"
#endif

extern "C" {
extern void **g_phyFuns;
extern uint32_t chip7_sleep_params[];
extern uint16_t tx_rf_ana_gain[2];
unsigned ram_txbbgain_to_index(unsigned gain);
unsigned rom_pbus_rd(unsigned selector, unsigned bank);
void rom_pbus_debugmode();
void rom_pbus_workmode();
void rom_pbus_xpd_rx_on(unsigned enabled);
void rom_pbus_xpd_tx_off();
void rom_stop_tx_tone(unsigned);
void ram_set_txclk_en(unsigned);
void ram_set_rxclk_en(unsigned);
}

namespace {
constexpr uint32_t Tone1=0x600060b8u, Tone2=0x600060bcu, Fraction=0x600050a8u;
constexpr uint32_t ToneGate=1u<<18, FineMode=1u<<29, ToneFields=0x0fffffffu;
// Addresses and command fields match ram_pbus_force_test in the pinned SDK.
constexpr uint32_t PbusCommand=0x3ff46094u, PbusStatus=0x3ff460a0u;
constexpr uint32_t ClockMask=0x0003c000u, AdcForce=(1u<<9)|(1u<<8);
constexpr uint32_t ReceiveGain=0x3ff5c02cu;
constexpr uint32_t ReceiveAgc=0x3ff5c030u, GainForce=1u<<23, AgcDisable=1u<<8;
constexpr uint32_t GainMaximumMask=127u<<8;
constexpr unsigned AutoGainMaximum=52u;
struct BusAddress { uint8_t selector, bank; };
// Exact selector/bank pairs enumerated by the classic SDK's pbus_print().
// The classic ROM dispatch is sparse; e.g. selector6 in bank1 is invalid.
constexpr BusAddress BusAddresses[]={
    {0,1},{1,1},{1,2},{4,1},{5,1},{2,1},{3,1},{2,2},{3,2}
};
constexpr unsigned BusWords=sizeof(BusAddresses)/sizeof(BusAddresses[0]);
constexpr unsigned TxControlWords=5;
constexpr uint16_t NativeRf[]={0x7f,0x6f,0x5f,0x7a,0x5a,0x69,0x75,0x55,0x74,0x54,0x44,0x60,0x40,0x20,0x10,0};
constexpr int16_t NativePower[]={10,5,0,-8,-18,-25,-34,-44,-55,-65,-71,-84,-96,-113,-127,-151};
constexpr unsigned StageCount=sizeof(NativeRf)/sizeof(NativeRf[0]);
constexpr int ToneMargin=12;

static DRAM_ATTR uint32_t toneWords[3], frequencyWords[3], mutedTone;
static DRAM_ATTR uint32_t rxTone1, rxTone2, rxFraction, rxAdcForce;
static DRAM_ATTR uint32_t autoGainMaximumBits;
static DRAM_ATTR CbRadioBusWord bus[BusWords];
static DRAM_ATTR uint8_t changed[BusWords];
static DRAM_ATTR unsigned changes, txClocks;
static DRAM_ATTR CbRadioStats stats;
static CbRadioTxPower txPower;
static uint32_t savedTone1, savedTone2, savedFraction, savedAdc;
static unsigned firstStage;
static uint16_t transmitDco[4];
static int gainOverride=-1;
static uint8_t lastBbGain=16;
static bool initialized, ownsRadio, gainAuto, txCalibrated;
static DRAM_ATTR bool configured, listenOnly, transmitting, fault;

inline __attribute__((always_inline)) void fence() { asm volatile("memw" ::: "memory"); }
void setTxClock(unsigned enabled) { ((void (*)(unsigned))g_phyFuns[0x3cu/4u])(enabled); }

bool initTransmit(const CbRadioConfig& cfg) {
    unsigned channel=(cfg.loKHz<2412000u) ? 1u :
                     1u + (cfg.loKHz-2412000u+2500u)/5000u;
    if (channel>13u) channel=13u;
    esp_phy_wifi_tx_tone(1u, channel, 80u);
    REG_WRITE(Tone1, DPORT_REG_READ(Tone1)&~ToneGate);
    REG_WRITE(Tone2, DPORT_REG_READ(Tone2)&~ToneGate);
    rom_pbus_xpd_tx_off();

    const int8_t calibrated=int8_t(43u-DPORT_REG_READ(0x3ff46004u));
    const int baseline=(calibrated<0 ? 0 : int(calibrated))+ToneMargin;
    unsigned initial=0;
    while (initial<StageCount && NativeRf[initial]!=tx_rf_ana_gain[0]) ++initial;
    bool success=false;
    if (initial<StageCount) {
        const int requested=int(cfg.power)+int(cfg.ask);
        unsigned stage=initial;
        for (unsigned i=initial; i<StageCount; ++i) {
            if (baseline+requested+NativePower[i]-NativePower[initial]>=ToneMargin) stage=i;
        }
        int attenuation=baseline+requested+NativePower[stage]-NativePower[initial];
        if (attenuation>127) { attenuation=127; ++stats.attenuationClipped; }
        const unsigned bbIndex=ram_txbbgain_to_index(tx_rf_ana_gain[1]);
        if (bbIndex<=4u) {
            // Native txcal_debuge_mode() selects one of five four-u16 DCO rows.
            const auto* calibration=reinterpret_cast<const uint8_t*>(chip7_sleep_params);
            const unsigned off=12u+8u*bbIndex;
            for (unsigned i=0; i<4; ++i) {
                transmitDco[i]=uint16_t(calibration[off+2u*i]) |
                               (uint16_t(calibration[off+2u*i+1u])<<8);
            }
            firstStage=initial;
            txPower={uint16_t(requested),uint16_t(baseline),uint16_t(attenuation),
                     uint8_t(stage),uint8_t(stage),
                     {int16_t(baseline+NativePower[stage]-NativePower[initial]),
                      NativeRf[stage],tx_rf_ana_gain[1]}};
            success=true;
        }
    }
    esp_phy_wifi_tx_tone(0u,0u,0u);
    rom_pbus_xpd_tx_off();
    setTxClock(0u);
    return success;
}

void keyCalibration() {
    // Vendor calls can lock. Execute once here before parking the other CPU.
    ((void (*)(void))g_phyFuns[0xc0u/4u])();
    ((void (*)(unsigned,unsigned))g_phyFuns[0xd8u/4u])(txPower.gain.rf,txPower.gain.bb);
    ((void (*)(const uint16_t*))g_phyFuns[0xdcu/4u])(transmitDco);
    setTxClock(1u);
    ((void (*)(void))g_phyFuns[0x11cu/4u])();
}

void snapshot(bool tx) {
    // ROM PBUS reads make raw DPORT accesses. No RTOS/vendor table calls
    // are made while the peer is parked.
    const uint32_t token=e32_stream_enter();
    if (tx) txClocks=(DPORT_SEQUENCE_REG_READ(PbusStatus)&ClockMask&~0xc000u)|0x30000u;
    for (unsigned index=0; index<BusWords; ++index) {
        const auto& address=BusAddresses[index];
        const uint16_t value=uint16_t(rom_pbus_rd(address.selector,address.bank))&0x1ffu;
        if (tx) bus[index].tx=value;
        else bus[index]={address.selector,address.bank,value,0};
    }
    e32_stream_leave(token);
}

inline __attribute__((always_inline)) void setClocks(uint32_t value) {
    uint32_t state=DPORT_SEQUENCE_REG_READ(PbusStatus);
    REG_WRITE(PbusStatus,(state&~ClockMask)|value);
    fence();
}

bool IRAM_ATTR writePbus(const CbRadioBusWord& word, bool tx) {
    const uint32_t value=tx ? word.tx : word.rx;
    const uint32_t payload=((value<<6)|(uint32_t(word.selector)<<2)|
                            (uint32_t(word.bank)<<15))&0x1fffcu;
    const uint32_t command=(DPORT_SEQUENCE_REG_READ(PbusCommand)&0xfffe0001u)|payload;
    REG_WRITE(PbusCommand,command|2u);
    fence();
    const uint32_t start=esp_cpu_get_cycle_count();
    while (DPORT_SEQUENCE_REG_READ(PbusStatus)&0x80000000u) {
        if (esp_cpu_get_cycle_count()-start>1200u) {
            REG_WRITE(PbusCommand,command);
            ++stats.busTimeouts;
            fault=true;
            return false;
        }
    }
    REG_WRITE(PbusCommand,command);
    fence();
    return true;
}

void IRAM_ATTR receivePath() {
    REG_WRITE(Tone1,mutedTone);
    fence();
    // Explicitly power down the PA even when a PBUS write faulted. The ROM
    // routines below use only the classic ESP32's on-chip PBUS peripherals.
    for (unsigned i=changes; i>0; --i) {
        if (!writePbus(bus[changed[i-1u]],false)) break;
    }
    rom_pbus_xpd_tx_off();

    // Mirror the linked SDK's ram_txcal_work_mode() (g_phyFuns slots
    // 0x4c, 0x3c, 0xd0, 0xc4). Its wrapper links in flash. The native
    // callees are ROM/IRAM and can run with the peer CPU parked.
    rom_stop_tx_tone(1u);
    ram_set_txclk_en(0u);
    rom_pbus_xpd_rx_on(0u);
    rom_pbus_workmode();

    // The TX tone gate alone does not reset the fine-mode/NCO route.
    REG_WRITE(Tone1,rxTone1&~ToneGate);
    REG_WRITE(Tone2,rxTone2&~ToneGate);
    REG_WRITE(Fraction,rxFraction);
    fence();
    rom_pbus_xpd_rx_on(1u);
    ram_set_rxclk_en(1u);
    const uint32_t path=DPORT_SEQUENCE_REG_READ(PbusStatus);
    REG_WRITE(PbusStatus,(path&~AdcForce)|rxAdcForce);
    if (gainAuto) {
        const uint32_t gain=DPORT_SEQUENCE_REG_READ(ReceiveGain);
        REG_WRITE(ReceiveGain,(gain&~(GainForce|GainMaximumMask))|autoGainMaximumBits);
        // Equivalent to SDK ram_enable_agc(), using the already parked CPU.
        REG_WRITE(ReceiveAgc,DPORT_SEQUENCE_REG_READ(ReceiveAgc)&~AgcDisable);
        fence();
        return;
    }
    const uint32_t gain=(DPORT_SEQUENCE_REG_READ(ReceiveGain)&0x007fffffu)|
                        (e32_radio_rx_gain_index()<<24);
    REG_WRITE(ReceiveGain,gain);
    fence();
    esp_rom_delay_us(2);
    REG_WRITE(ReceiveGain,gain|GainForce);
    fence();
}
}

bool cbRadioInit() {
    if (initialized) return true;
    if (e32_radio_init()!=ESP_OK) return false;
    wifi_promiscuous_filter_t quiet={};
    if (esp_wifi_set_promiscuous_filter(&quiet)!=ESP_OK) return false;
    initialized=true;
    return true;
}

bool cbRadioConfigure(const CbRadioConfig& cfg) {
    cbRadioRelease();
    if (!cbRadioInit()) return false;
    stats={};
    txPower={};
    changes=0;
    fault=false;
    listenOnly=cfg.listenOnly;
    txCalibrated=false;
    ownsRadio=true;
    cbReportStartup(StartupStage::Saving);
    savedTone1=DPORT_REG_READ(Tone1);
    savedTone2=DPORT_REG_READ(Tone2);
    savedFraction=DPORT_REG_READ(Fraction);
    savedAdc=DPORT_REG_READ(PbusStatus)&AdcForce;
    if (!listenOnly) {
        cbReportStartup(StartupStage::TxCalibration);
        if (!initTransmit(cfg)) { cbRadioRelease(); return false; }
    }

    gainAuto=cfg.rfGain==7u && gainOverride<0;
    if (cfg.bbGain<32u) lastBbGain=cfg.bbGain;
    const int gain=gainOverride>=0 ? gainOverride : gainAuto ? -1 :
                   24+int(lastBbGain)+6*int(cfg.rfGain)+int(cfg.vga);
    e32_radio_set_rx_gain(gain);
    const uint32_t lo=cfg.loKHz*1000u-78125u*cfg.ifK;
    cbReportStartup(StartupStage::RxTuning);
    if (e32_radio_receive_lo(lo)!=ESP_OK) { cbRadioRelease(); return false; }
    if (gainAuto) {
        // SDK set_rx_gain_table programs the Wi-Fi table limit at bits 14:8.
        // Restrict the table range while the PHY still selects its own row.
        const unsigned stockMaximum=e32_radio_rx_gain_maximum();
        autoGainMaximumBits=(stockMaximum<AutoGainMaximum?stockMaximum:AutoGainMaximum)<<8;
        const uint32_t current=DPORT_REG_READ(ReceiveGain);
        REG_WRITE(ReceiveGain,(current&~(GainForce|GainMaximumMask))|autoGainMaximumBits);
    }
    REG_WRITE(PbusStatus,DPORT_REG_READ(PbusStatus)|AdcForce);
    cbReportStartup(StartupStage::RxReady);
    if (listenOnly) { configured=true; return true; }

    // Snapshot the same receive route used in the passive path, before
    // either the warm TX calibration or forced PBUS modifies it.
    rxTone1=DPORT_REG_READ(Tone1);
    rxTone2=DPORT_REG_READ(Tone2);
    rxFraction=DPORT_REG_READ(Fraction);
    rxAdcForce=DPORT_REG_READ(PbusStatus)&AdcForce;

    // Only active stations switch RF. A passive listener keeps the native
    // receive configuration with no PBUS snapshot or speculative TX setup.
    cbReportStartup(StartupStage::ForcedMode);
    const uint32_t prepareToken=e32_stream_enter();
    rom_pbus_debugmode();
    rom_pbus_xpd_tx_off();
    rom_pbus_xpd_rx_on(1u);
    e32_stream_leave(prepareToken);
    cbReportStartup(StartupStage::RxSnapshot);
    snapshot(false);
    keyCalibration();
    cbReportStartup(StartupStage::TxSnapshot);
    snapshot(true);
    // These calibrated TX writes are explicit in the SDK's ram_pbus_xpd_tx_on
    // rather than optional differences in the PBUS readback.
    bus[0].tx=1u;
    bus[1].tx=124u;
    bus[2].tx=txPower.gain.bb&0x1ffu;
    bus[3].tx=127u;
    bus[4].tx=txPower.gain.rf&0x1ffu;
    for (unsigned i=0; i<BusWords; ++i) {
        if (i<TxControlWords || bus[i].rx!=bus[i].tx)
            changed[changes++]=uint8_t(i);
    }

    const uint32_t toneBase=(DPORT_REG_READ(Tone1)&~(ToneFields|ToneGate))|FineMode;
    mutedTone=toneBase|(uint32_t(uint8_t(0u-txPower.ncoAttenuation))<<10);
    const uint32_t fractionBase=DPORT_REG_READ(Fraction)&~3u;
    for (int direction=-1; direction<=1; ++direction) {
        const uint32_t code=uint32_t(2*(int(cfg.ifK)+direction))&4095u;
        toneWords[direction+1]=mutedTone|ToneGate|(code>>2);
        frequencyWords[direction+1]=fractionBase|(code&3u);
    }
    REG_WRITE(Tone2,DPORT_REG_READ(Tone2)&~ToneGate);
    cbReportStartup(StartupStage::RestoringRx);
    const uint32_t token=e32_stream_enter();
    receivePath();
    e32_stream_leave(token);
    if (fault) { cbRadioRelease(); return false; }
    stats.ncoMode=FineMode;
    stats.ncoStep=2u;
    stats.switchWords=changes;
    txCalibrated=configured=true;
    return true;
}

void cbRadioSetGainOverride(int index) { gainOverride=index; }
unsigned IRAM_ATTR cbRadioCurrentGain() { return e32_radio_rx_gain_index(); }
unsigned IRAM_ATTR cbRadioMaxGain() { return e32_radio_rx_gain_maximum(); }
bool IRAM_ATTR cbRadioAutoGain() { return gainAuto; }

bool IRAM_ATTR cbRadioTxBegin() {
    if (!configured || listenOnly || fault || transmitting) return false;
    const uint32_t start=esp_cpu_get_cycle_count();
    REG_WRITE(CbIq::control1,DPORT_SEQUENCE_REG_READ(CbIq::control1)&~(CbIq::done|3u));
    REG_WRITE(Tone1,mutedTone);
    fence();
    rom_pbus_debugmode();
    setClocks(txClocks);
    for (unsigned i=0; i<changes; ++i) {
        if (!writePbus(bus[changed[i]],true)) { receivePath(); return false; }
    }
    transmitting=true;
    const uint32_t spent=esp_cpu_get_cycle_count()-start;
    if (spent>stats.maxTxBeginCycles) stats.maxTxBeginCycles=spent;
    return true;
}

void IRAM_ATTR cbRadioTone(int direction) {
    const unsigned index=unsigned(direction+1);
    if (!transmitting || index>2u) return;
    REG_WRITE(Fraction,frequencyWords[index]);
    REG_WRITE(Tone1,toneWords[index]);
    fence();
}

void IRAM_ATTR cbRadioTxEnd() {
    if (!transmitting) return;
    const uint32_t start=esp_cpu_get_cycle_count();
    receivePath();
    transmitting=false;
    const uint32_t spent=esp_cpu_get_cycle_count()-start;
    if (spent>stats.maxTxEndCycles) stats.maxTxEndCycles=spent;
}

void cbRadioRelease() {
    if (!ownsRadio) return;
    // The timed loop has already closed TX and released the peer CPU.
    e32_radio_idle();
    REG_WRITE(Tone1,savedTone1&~ToneGate);
    REG_WRITE(Tone2,savedTone2&~ToneGate);
    REG_WRITE(Fraction,savedFraction);
    REG_WRITE(PbusStatus,(DPORT_REG_READ(PbusStatus)&~AdcForce)|savedAdc);
    fence();
    transmitting=configured=ownsRadio=false;
}

bool IRAM_ATTR cbRadioHealthy() { return !fault; }
void cbRadioGetStats(CbRadioStats& result) { result=stats; }
bool cbRadioGetBusWord(unsigned index, CbRadioBusWord& result) {
    if (index>=BusWords) return false;
    result=bus[index];
    return true;
}
bool cbRadioGetTxGainRow(unsigned index, CbRadioTxGainRow& result) {
    if (!txCalibrated || firstStage+index>=StageCount) return false;
    result={int16_t(int(txPower.baselineQuarterDb)+NativePower[firstStage+index]-NativePower[firstStage]),
            NativeRf[firstStage+index],txPower.gain.bb};
    return true;
}
void cbRadioGetTxPower(CbRadioTxPower& result) { result=txPower; }
