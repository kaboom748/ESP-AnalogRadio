/* SPDX-License-Identifier: GPL-3.0-or-later
 * Native ESP32-C5 PHY adaptation of the ESP8266 CB_V6 radio.
 * Calibration runs once before READY. TDD uses native warm PHY switches,
 * the NCO/baseband mux and calibrated gain/DC entries. No retune, full
 * calibration or RTOS delay occurs in a slot; the synthesizer stays warm.
 *
 * PBUS/NCO ABI inspected in Espressif esp-phy-lib e294ff039e26b3486d6c9e5853d24d98ee3300b2,
 * libphy.a SHA256 dbf33c418c8d408d4005c849d12a1432deea82e2e5e57de3c8ddf914d104fffb.
 */
#include "c5_cb_radio.h"
#include <string.h>
#include "esp_cpu.h"
#include "esp_event.h"
#include "esp_idf_version.h"
#include "esp_phy_cert_test.h"
#include "esp_rom_sys.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "soc/soc.h"

#if !CONFIG_IDF_TARGET_ESP32C5 || ESP_IDF_VERSION != ESP_IDF_VERSION_VAL(5,5,5)
#error "Select ESP32C5 Dev Module using Espressif Arduino-ESP32 3.3.12"
#endif

extern "C" {
extern uint8_t phy_param[];
void phy_set_chanfreq(unsigned, unsigned);
void phy_set_rf_freq_offset(unsigned, unsigned, int);
void phy_stop_tx_tone(unsigned);
void phy_set_txclk_en(unsigned);
void phy_set_rxclk_en(unsigned);
void phy_pbus_workmode();
void phy_pbus_debugmode();
void phy_pbus_xpd_tx_off();
void phy_pbus_xpd_tx_on(unsigned, unsigned);
void phy_pbus_xpd_rx_on(unsigned);
void phy_pbus_set_dco(const uint16_t*);
void phy_pbus_set_rxgain(unsigned);
unsigned phy_pbus_rd(unsigned, unsigned);
void phy_force_rx_gain(unsigned, unsigned);
void phy_disable_agc();
void phy_enable_agc();
void phy_i2c_writeReg_Mask(unsigned, unsigned, unsigned, unsigned, unsigned, unsigned);
void phy_wifi_get_tx_tab_new(unsigned, int8_t*, uint16_t*, uint16_t*, unsigned);
unsigned phy_txbbgain_to_index(unsigned);
}

namespace {
constexpr uint32_t tone1 = 0x600a041cu, tone2 = 0x600a0420u;
constexpr uint32_t toneFraction = 0x600a0428u;
constexpr uint32_t pbusCmd = 0x600a0884u, pbusCtrl = 0x600a088cu;
constexpr uint32_t pbusStatus = 0x600a0890u;
constexpr uint32_t dacScale = 0x600a0c04u, txdc = 0x600a0c08u;
constexpr uint32_t gain0 = 0x600a702cu, gain1 = 0x600a7030u;
constexpr uint32_t basebandMux = 0x600a040cu;
constexpr uint32_t basebandConstantI = 0x600a0410u, basebandConstantQ = 0x600a0414u;
constexpr uint32_t toneGate = 1u << 18, fineMode = 1u << 29;
constexpr uint32_t ncoFields = 0x0003ffffu;
constexpr uint32_t clockFields = 0x0003c000u;
/* The vendor start_tx_tone wrapper uses 25.6 phase steps per MHz in
 * fineMode. A 12-bit step is therefore 39.0625kHz: two steps reproduce
 * the ESP8266's 78.125kHz centered frequency-dither pulses. */
#ifndef C5_CB_NCO_STEP
#define C5_CB_NCO_STEP 2u
#endif
constexpr uint32_t ncoStep = C5_CB_NCO_STEP;
/* Native2.4GHz PA/baseband coarse stages from the pinned SDK's
 * phy_tx_gain.o rodata (LANCHOR2+132,+168,+204). They extend below the
 * normal Wi-Fi32-row power window without inventing analog gain words. */
constexpr uint16_t nativeTxRf[] = {63,63,55,47,39,31,31,23,15,13,7,5,3,2,1,1,1,1};
constexpr uint16_t nativeTxBb[] = {3,3,3,3,3,3,1,1,1,1,1,1,1,1,1,0,0,0};
constexpr int16_t nativeTxPower[] = {30,30,27,23,18,12,0,-9,-21,-29,-44,-53,-67,-77,-91,-103,-103,-103};
constexpr int nativeToneMargin = 32; // vendor CW also includes32quarter-dB backoff
using BusWord = CbRadioBusWord;
static DRAM_ATTR BusWord changes[22];
static DRAM_ATTR BusWord capturedWords[22];
/* The native TX-on sequence explicitly asserts these PA controls in 2.4GHz.
 * Closing only the NCO leaves an LO carrier if either remains enabled. */
static DRAM_ATTR BusWord paControls[] = {{5u,1u,0u,3u},{6u,1u,0u,487u}};
static DRAM_ATTR uint16_t transmitDco[4];
static DRAM_ATTR uint32_t transmitBaseband[2];
static DRAM_ATTR uint16_t transmitRfGain, transmitBbGain;
static DRAM_ATTR int8_t txDigital[32];
static DRAM_ATTR uint16_t txRf[32], txBb[32];
static DRAM_ATTR CbRadioTxPower txPower;
static bool txTableValid;
static DRAM_ATTR uint32_t toneWords[3], fractionWords[3];
static DRAM_ATTR uint32_t rxCommand, rxControl, rxClocks, txClocks, rxGain0, rxGain1;
static DRAM_ATTR uint32_t savedTone1, savedTone2, savedFraction, savedScale, savedTxdc;
static DRAM_ATTR uint32_t savedBasebandMux, savedBasebandI, savedBasebandQ;
static DRAM_ATTR uint8_t changeCount;
static DRAM_ATTR bool configured, transmitting, fault, listener;
static DRAM_ATTR CbRadioStats stats;
static bool initialized;
static int gainOverride = -1;
static uint8_t lastBbGain = 16u;
static DRAM_ATTR uint8_t receiveGainIndex;
static DRAM_ATTR uint8_t receiveGainMaximum;
static DRAM_ATTR bool receiveGainForced;
static DRAM_ATTR bool receiveGainAutomatic;

static inline __attribute__((always_inline)) void fence() {
    asm volatile("fence iorw,iorw" ::: "memory");
}

static void IRAM_ATTR applyReceiveGain() {
    if (!receiveGainForced) {
        REG_WRITE(gain0,rxGain0);
        REG_WRITE(gain1,rxGain1);
        return;
    }
    /* The edge loads a complete calibrated gain/DC entry. Allow the force
     * signal to cross the RX clock domain before and after the assertion. */
    const uint32_t word = (REG_READ(gain0) & 0x007fffffu) |
                          (uint32_t(receiveGainIndex) << 24);
    REG_WRITE(gain0,word);
    fence();
    esp_rom_delay_us(2);
    REG_WRITE(gain0,word | (1u << 23));
    fence();
    esp_rom_delay_us(2);
}

static bool IRAM_ATTR writeBus(const BusWord& item, bool tx) {
    const uint32_t value = tx ? item.tx : item.rx;
    const uint32_t payload = ((value << 6) | (uint32_t(item.selector) << 2) |
                             (uint32_t(item.bank) << 15)) & 0x1fffcu;
    REG_WRITE(pbusCmd, (REG_READ(pbusCmd) & 0xfffe0001u) | payload | 2u);
    fence();
    const uint32_t start = esp_cpu_get_cycle_count();
    while (REG_READ(pbusStatus) & 0x80000000u) {
        if (esp_cpu_get_cycle_count() - start > 2400u) {
            REG_WRITE(pbusCmd, REG_READ(pbusCmd) & ~2u);
            ++stats.busTimeouts;
            fault = true;
            return false;
        }
    }
    REG_WRITE(pbusCmd, REG_READ(pbusCmd) & ~2u);
    fence();
    return true;
}

static void receiveSetup(const CbRadioConfig& cfg) {
    phy_stop_tx_tone(1);
    /* After TX calibration the analog PBUS still contains TX-on controls.
     * Apply the explicit RX sequence while the bus is forced; issuing those
     * writes in workmode left TX values visible on the qualified board.
     * Preserve the separately qualified passive initialization sequence. */
    if (cfg.listenOnly) phy_pbus_workmode();
    else phy_pbus_debugmode();
    phy_pbus_xpd_tx_off();
    phy_pbus_xpd_rx_on(1);
    phy_set_rxclk_en(1);
    /* The free-running packet AGC hunts across narrowband TDD gaps.
     * RF7 instead lets the application hold one calibrated index per burst
     * and adapt that index between bursts using these same hardware entries. */
    receiveGainAutomatic = cfg.rfGain == 7u && gainOverride < 0;
    receiveGainForced = true;
    if (receiveGainForced) {
        unsigned maximum = (REG_READ(gain0) >> 8) & 127u;
        receiveGainMaximum = uint8_t(maximum);
        unsigned chosen;
        if (gainOverride >= 0) chosen = unsigned(gainOverride);
        else if (receiveGainAutomatic) chosen = 30u;
        else {
            if (cfg.bbGain < 32u) lastBbGain = cfg.bbGain;
            /* Project the original controls onto calibrated C5 gain indices.
             * This preserves their ordering and makes the measured default
             * RF0/BB16/VGA0 select index30. Codes are not calibrated dB. */
            chosen = 14u + unsigned(lastBbGain) + 6u*cfg.rfGain + cfg.vga;
        }
        receiveGainIndex = uint8_t(chosen > maximum ? maximum : chosen);
        /* A calibrated index updates analog gain and its DC compensation
         * together. Give the gain-force clock-domain crossing a real pulse. */
        phy_force_rx_gain(0,receiveGainIndex);
        esp_rom_delay_us(2);
        phy_force_rx_gain(1,receiveGainIndex);
    }
    esp_rom_delay_us(20000);
}
}

bool cbRadioInit() {
    if (initialized) return true;
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        e = nvs_flash_erase();
        if (e == ESP_OK) e = nvs_flash_init();
    }
    if (e != ESP_OK) return false;
    e = esp_event_loop_create_default();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return false;
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    init.static_rx_buf_num = 4;
    init.ampdu_rx_enable = 0;
    init.ampdu_tx_enable = 0;
    if (esp_wifi_init(&init) != ESP_OK || esp_wifi_set_storage(WIFI_STORAGE_RAM) != ESP_OK ||
        esp_wifi_set_mode(WIFI_MODE_NULL) != ESP_OK || esp_wifi_start() != ESP_OK ||
        esp_wifi_set_ps(WIFI_PS_NONE) != ESP_OK || esp_wifi_set_promiscuous(true) != ESP_OK ||
        esp_wifi_set_channel(1,WIFI_SECOND_CHAN_NONE) != ESP_OK) return false;
    wifi_promiscuous_filter_t quiet = {};
    if (esp_wifi_set_promiscuous_filter(&quiet) != ESP_OK) return false;
    initialized = true;
    return true;
}

void cbRadioSetGainOverride(int index) { gainOverride = index; }

bool IRAM_ATTR cbRadioSetRuntimeGain(unsigned index) {
    if (index > receiveGainMaximum) index = receiveGainMaximum;
    if (!configured || transmitting || index == receiveGainIndex) return false;
    receiveGainIndex = uint8_t(index);
    applyReceiveGain();
    rxGain0 = REG_READ(gain0);
    rxGain1 = REG_READ(gain1);
    return true;
}

unsigned IRAM_ATTR cbRadioCurrentGain() { return receiveGainIndex; }
unsigned IRAM_ATTR cbRadioMaxGain() { return receiveGainMaximum; }
bool IRAM_ATTR cbRadioAutoGain() { return receiveGainAutomatic; }

bool cbRadioConfigure(const CbRadioConfig& cfg) {
    if (cfg.loKHz < 2400000u || cfg.loKHz > 2483500u || cfg.power > 191u ||
        cfg.rfGain > 7u || cfg.vga > 7u || cfg.ask > 127u || cfg.ifK > 1u || cfg.bbGain > 32u)
        return false;
    cbRadioRelease();
    if (!cbRadioInit()) return false;
    memset(&stats,0,sizeof(stats));
    memset(&txPower,0,sizeof(txPower));
    txTableValid = false;
    fault = false;
    listener = cfg.listenOnly;
    phy_set_chanfreq(2412,0);
    uint32_t khz = cfg.loKHz - 78u * cfg.ifK;
    phy_set_rf_freq_offset(phy_param[49],khz / 1000u,int(khz % 1000u));
    savedTone1 = REG_READ(tone1);
    savedTone2 = REG_READ(tone2);
    savedFraction = REG_READ(toneFraction);
    savedScale = REG_READ(dacScale);
    savedTxdc = REG_READ(txdc);
    savedBasebandMux = REG_READ(basebandMux);
    savedBasebandI = REG_READ(basebandConstantI);
    savedBasebandQ = REG_READ(basebandConstantQ);
    // From here every exit owns a complete saved state and must close RF.
    configured = true;
    uint16_t transmitWords[22] = {};
    if (!listener) {
        /* Initialize calibrated TX state once, then select native PA/BB
         * gain and NCO attenuation for the requested APWR. */
        esp_phy_wifi_tx_tone(1,(cfg.loKHz + 500u) / 1000u,80);
        REG_WRITE(tone1,REG_READ(tone1) & ~toneGate);
        REG_WRITE(tone2,REG_READ(tone2) & ~toneGate);
        REG_WRITE(txdc,REG_READ(txdc) & ~(1u << 26));
        fence();
        for (unsigned s=0;s<11;++s)
            for (unsigned b=1;b<=2;++b) transmitWords[2*s+b-1] = uint16_t(phy_pbus_rd(s,b));
        transmitDco[0] = uint16_t(phy_pbus_rd(2u,1u));
        transmitDco[1] = uint16_t(phy_pbus_rd(3u,1u));
        transmitDco[2] = uint16_t(phy_pbus_rd(2u,2u));
        transmitDco[3] = uint16_t(phy_pbus_rd(3u,2u));
        transmitBaseband[0] = REG_READ(basebandConstantI);
        transmitBaseband[1] = REG_READ(basebandConstantQ);
        txClocks = REG_READ(pbusStatus) & clockFields;
        /* The native tone bypasses the TXDC DAC-scale multiplier. Select a
         * calibrated PA/baseband stage instead; its signed digital residual
         * is later applied by the NCO's own logarithmic attenuation field.
         * Each table row decreases the target by4quarter-dB units (1dB). */
        phy_wifi_get_tx_tab_new((cfg.loKHz+500u)/1000u,txDigital,txBb,txRf,0u);
        txTableValid = true;
        int calibrationOffset = 0;
        bool haveCalibrationOffset = false;
        for (unsigned n=0;n<18u;++n) {
            if (nativeTxRf[n] == (txRf[0]&63u) && nativeTxBb[n] == txBb[0]) {
                calibrationOffset = int(txDigital[0])+nativeTxPower[n];
                haveCalibrationOffset = true;
                break;
            }
        }
        if (!haveCalibrationOffset) {
            cbRadioRelease();
            return false;
        }
        const int baseline = calibrationOffset-nativeTxPower[0]+nativeToneMargin;
        const unsigned requested = unsigned(cfg.power)+unsigned(cfg.ask);
        unsigned stage = 0;
        /* Hold at least the CW headroom margin, choosing the lowest native
         * analog stage that can still reach the requested level. Across a
         * stage transition the NCO residual exactly cancels the coarse jump.
         * RFspan133 + NCOspan95 quarter-dB covers all192 APWR settings
         * without clipping; extreme combined APWR+ASK may hit native limits. */
        for (unsigned n=0;n<18u;++n)
            if (baseline+int(requested)+nativeTxPower[n]-calibrationOffset >= nativeToneMargin)
                stage=n;
        int attenuation = baseline+int(requested)+nativeTxPower[stage]-calibrationOffset;
        if (attenuation < 0) { attenuation=0; ++stats.attenuationClipped; }
        if (attenuation > 127) { attenuation=127; ++stats.attenuationClipped; }
        transmitRfGain = nativeTxRf[stage];
        transmitBbGain = nativeTxBb[stage];
        /* Exactly the selected-BB DCO layout used by the vendor's
         * phy_txcal_debuge_mode_new in2.4GHz. All BB stages were calibrated
         * by PHY initialization; no calibration is run in timed slots. */
        const unsigned bbIndex = phy_txbbgain_to_index(transmitBbGain);
        if (bbIndex > 2u) {
            cbRadioRelease();
            return false;
        }
        const unsigned dc = 0x98u+4u*bbIndex;
        transmitDco[0] = uint16_t(phy_param[dc]) | (uint16_t(phy_param[dc+1u])<<8);
        transmitDco[1] = uint16_t(phy_param[dc+2u]) | (uint16_t(phy_param[dc+3u])<<8);
        transmitDco[2] = transmitDco[3] = 256u;
        txPower.requestedQuarterDb = uint16_t(requested);
        txPower.baselineQuarterDb = uint16_t(baseline);
        txPower.ncoAttenuation = uint16_t(attenuation);
        txPower.row = 255u;
        for (unsigned n=0;n<32u;++n)
            if ((txRf[n]&63u)==transmitRfGain && txBb[n]==transmitBbGain) {
                txPower.row=uint8_t(n);
                break;
            }
        txPower.stage=uint8_t(stage);
        /* The selected native stage's residual at the row0 target. */
        txPower.gain = {int16_t(calibrationOffset-nativeTxPower[stage]),transmitRfGain,transmitBbGain};
        /* Stop the calibration ROUTE, not only its digital tone. The vendor
         * work-mode routine clears analog I2C(110,1,13) bit2; leaving the
         * calibration mux selected made native IQ return a constant DC pair
         * instead of received RF after an A/B session's TX calibration. */
        esp_phy_wifi_tx_tone(0,0,0);
        /* Calibration may tune to an integer MHz; restore the exact shared
         * low-IF LO before creating the RX baseline and before streaming. */
        phy_set_rf_freq_offset(phy_param[49],khz / 1000u,int(khz % 1000u));
    }
    receiveSetup(cfg);
    changeCount = 0;
    for (unsigned s=0;s<11;++s) {
        for (unsigned b=1;b<=2;++b) {
            uint16_t r = uint16_t(phy_pbus_rd(s,b));
            uint16_t t = transmitWords[2*s+b-1];
            const BusWord item = {uint8_t(s),uint8_t(b),r,t};
            capturedWords[2*s+b-1] = item;
            if (!listener && r != t) changes[changeCount++] = item;
        }
    }
    rxCommand = REG_READ(pbusCmd) & ~2u;
    rxControl = REG_READ(pbusCtrl);
    rxClocks = REG_READ(pbusStatus) & clockFields;
    rxGain0 = REG_READ(gain0);
    rxGain1 = REG_READ(gain1);
    if (listener) {
        /* Passive receive must leave all transmitter NCO/DAC controls in
         * the exact state installed by the qualified RX initialization. */
        configured = true;
        return true;
    }
    /* Match native start_tx_tone_step: baseband/DAC contributions are zero;
     * APWR and ASK are entirely represented by calibrated RF/BB stages and
     * the native tone's digital residual, never by the bypassed TXDC scale. */
    REG_WRITE(dacScale,REG_READ(dacScale) & 0xff0000ffu);
    uint32_t common = (REG_READ(tone1) & ~(ncoFields | toneGate)) | fineMode |
                      (uint32_t(uint8_t(0u-uint32_t(txPower.ncoAttenuation))) << 10);
    uint32_t frac = REG_READ(toneFraction) & ~3u;
    for (int direction=-1;direction<=1;++direction) {
        uint32_t word = uint32_t((int(cfg.ifK)+direction)*int(ncoStep)) & 4095u;
        toneWords[direction+1] = common | toneGate | (word >> 2);
        fractionWords[direction+1] = frac | (word & 3u);
    }
    REG_WRITE(tone1,common);
    REG_WRITE(tone2,REG_READ(tone2) & ~toneGate);
    /* A digital tone gate does not close the C5's analog PA. Confirm both
     * PA words are off before READY, including after initial calibration. */
    for (const auto& item:paControls) {
        if (!writeBus(item,false)) {
            cbRadioRelease();
            return false;
        }
    }
    fence();
    stats.ncoMode = fineMode;
    stats.ncoStep = ncoStep;
    stats.switchWords = changeCount;
    configured = true;
    return true;
}

bool IRAM_ATTR cbRadioTxBegin() {
    if (!configured || listener || fault || transmitting) return false;
    const uint32_t start = esp_cpu_get_cycle_count();
    REG_WRITE(CbIq::control1,REG_READ(CbIq::control1) & ~3u);
    REG_WRITE(tone1,REG_READ(tone1) & ~toneGate);
    /* The generic PBUS readback is not a physical RX/TX snapshot on this
     * C5: PA controls remained reported as TX-on after changing modes.
     * Use the complete qualified vendor warm-TX sequence, preserving only
     * the four genuinely readable/calibrated DCO words across the switch. */
    phy_pbus_debugmode();
    phy_pbus_xpd_tx_on(transmitRfGain,transmitBbGain);
    phy_pbus_set_dco(transmitDco);
    phy_set_txclk_en(1u);
    /* Match start_tx_tone_step's DIGITAL routing too. stop_tx_tone sets
     * these mux bits to3, selecting ordinary baseband instead of the NCO;
     * opening only the analog PA then emits no modulation. */
    REG_WRITE(basebandConstantI,transmitBaseband[0]);
    REG_WRITE(basebandConstantQ,transmitBaseband[1]);
    REG_WRITE(basebandMux,REG_READ(basebandMux) & ~3u);
    /* Match the calibrated analog TX route, then release this measurement
     * mux on RX. A full calibration is forbidden inside a slot. */
    phy_i2c_writeReg_Mask(110u,1u,13u,2u,2u,1u);
    fence();
    transmitting = true;
    uint32_t elapsed = esp_cpu_get_cycle_count()-start;
    if (elapsed > stats.maxTxBeginCycles) stats.maxTxBeginCycles = elapsed;
    return true;
}

void IRAM_ATTR cbRadioTone(int direction) {
    unsigned index = unsigned(direction+1);
    if (!transmitting || index > 2u) return;
    REG_WRITE(toneFraction,fractionWords[index]);
    REG_WRITE(tone1,toneWords[index]);
    fence();
}

void IRAM_ATTR cbRadioTxEnd() {
    if (!configured || !transmitting) return;
    const uint32_t start = esp_cpu_get_cycle_count();
    REG_WRITE(tone1,REG_READ(tone1) & ~toneGate);
    REG_WRITE(basebandMux,REG_READ(basebandMux) | 3u);
    fence();
    phy_set_txclk_en(0u);
    phy_pbus_xpd_tx_off();
    phy_pbus_xpd_rx_on(1u);
    phy_set_rxclk_en(1u);
    phy_i2c_writeReg_Mask(110u,1u,13u,2u,2u,0u);
    applyReceiveGain();
    fence();
    transmitting = false;
    uint32_t elapsed = esp_cpu_get_cycle_count()-start;
    if (elapsed > stats.maxTxEndCycles) stats.maxTxEndCycles = elapsed;
}

void cbRadioRelease() {
    if (!configured) return;
    cbRadioTxEnd();
    phy_pbus_xpd_tx_off();
    esp_phy_wifi_tx_tone(0,0,0);
    phy_set_txclk_en(0);
    phy_force_rx_gain(0,0);
    phy_enable_agc();
    REG_WRITE(tone1,savedTone1 & ~toneGate);
    REG_WRITE(tone2,savedTone2 & ~toneGate);
    REG_WRITE(toneFraction,savedFraction);
    REG_WRITE(dacScale,savedScale);
    REG_WRITE(txdc,savedTxdc);
    REG_WRITE(basebandConstantI,savedBasebandI);
    REG_WRITE(basebandConstantQ,savedBasebandQ);
    REG_WRITE(basebandMux,savedBasebandMux);
    fence();
    configured = false;
}

bool IRAM_ATTR cbRadioHealthy() { return !fault; }
void cbRadioGetStats(CbRadioStats& result) { result = stats; }
bool cbRadioGetBusWord(unsigned index, CbRadioBusWord& result) {
    if (index >= 22u) return false;
    result = capturedWords[index];
    return true;
}
bool cbRadioGetTxGainRow(unsigned index, CbRadioTxGainRow& result) {
    if (!txTableValid || index >= 32u) return false;
    result = {txDigital[index],uint16_t(txRf[index]&63u),txBb[index]};
    return true;
}
void cbRadioGetTxPower(CbRadioTxPower& result) { result=txPower; }
