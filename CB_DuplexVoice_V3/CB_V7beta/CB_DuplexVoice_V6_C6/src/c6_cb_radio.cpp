/* SPDX-License-Identifier: GPL-3.0-or-later
 * CB_V6 TDD using the native ESP32-C6 PLL, IQ estimator and tone generator.
 * Adapted from the C6 native RF backend in ESP-AnalogRadio/CB-V14 and the
 * CB_V6 C5 timed-radio interface. Private PHY ABI pinned to ESP-IDF 5.5.5.
 */
#include "c6_cb_radio.h"
#include "esp_cpu.h"
#include "esp_event.h"
#include "esp_idf_version.h"
#include "esp_phy_cert_test.h"
#include "esp_rom_sys.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "soc/soc.h"

#if !CONFIG_IDF_TARGET_ESP32C6 || ESP_IDF_VERSION != ESP_IDF_VERSION_VAL(5,5,5)
#error "Use ESP32C6 Dev Module with Arduino-ESP32 3.3.12"
#endif

extern "C" {
extern uint8_t phy_param[];
void chip_v7_set_chan(unsigned, unsigned);
void phy_set_freq(unsigned, int);
void ram_stop_tx_tone(unsigned);
void pbus_debugmode(void);
void pbus_workmode(void);
void txcal_work_mode(void);
void ram_pbus_xpd_tx_on(unsigned, unsigned);
void rom_pbus_xpd_tx_off(void);
void ram_pbus_xpd_rx_on(unsigned);
void pbus_set_dco(const uint16_t*);
void set_txclk_en(unsigned);
void set_rxclk_en(unsigned);
void phy_force_rx_gain(unsigned, unsigned);
void enable_agc(void);
unsigned rom_chip_i2c_readReg(unsigned, unsigned, unsigned);
void rom_chip_i2c_writeReg(unsigned, unsigned, unsigned, unsigned);
void wifi_get_tx_tab(unsigned, int8_t*, uint16_t*, uint16_t*, unsigned);
unsigned txbbgain_to_index(unsigned);

/* The RF test archive references this parser; no RF test shell is exposed. */
__attribute__((weak)) int cmd_parse(char*, char*, int*, char**) { return -1; }
}

namespace {
constexpr uint32_t tone1 = 0x600a0420u, tone2 = 0x600a0424u, fraction = 0x600a042cu;
constexpr uint32_t mux = 0x600a0414u, constantI = 0x600a0418u, constantQ = 0x600a041cu;
constexpr uint32_t gainControl = 0x600a702cu;
constexpr uint32_t gainCeilingPrimary = 0x600a713cu, gainCeilingSecondary = 0x600a7094u;
constexpr uint32_t gainCeilingSignal = 0x600a0958u;
constexpr unsigned agcMaxRows = 55;  // vendor ceiling word; companion fields use 54
constexpr uint32_t toneGate = 1u << 18, fineMode = 1u << 29, toneFields = 0x0fffffffu;
constexpr unsigned toneStep = 2;
constexpr int toneMargin = 12;
constexpr uint16_t nativeRF[] = {63,63,47,39,31,23,14,13,12,7,5,3,2,1,0,0};
constexpr uint16_t nativeBB[] = {128,0,0,0,0,0,0,0,0,0,0,0,0,0,128,0};
constexpr int16_t nativePower[] = {34,26,20,15,9,0,-16,-22,-28,-35,-45,-59,-69,-83,-99,-107};
constexpr unsigned gainStages = sizeof(nativeRF) / sizeof(nativeRF[0]);

struct SavedRadio { uint32_t tone1, tone2, fraction, mux, i, q; };
SavedRadio saved;
DRAM_ATTR SavedRadio receiveBaseline;
bool initialized, tableValid;
int gainOverride = -1;
uint8_t lastBbGain = 16;
int8_t digitalGain[32];
uint16_t rfGain[32], bbGain[32];
CbRadioTxPower power;
DRAM_ATTR bool configured, listener, transmitting, autoGain;
DRAM_ATTR bool gainCeilingEnabled;
DRAM_ATTR unsigned currentGain, maxGain;
DRAM_ATTR uint32_t txTone[3], txFraction[3];
DRAM_ATTR uint16_t txDco[4], txRF, txBB;
DRAM_ATTR CbRadioStats stats;

inline __attribute__((always_inline)) void fence() {
    asm volatile("fence iorw,iorw" ::: "memory");
}

void IRAM_ATTR normalLO() {
    /* C6 analog I2C bit3 selects the alternate 5/6 LO divider. Both slots
     * use the same ordinary LO and must leave the alternate route disabled. */
    const unsigned route = rom_chip_i2c_readReg(0x62u, 1u, 16u);
    if (route & 8u) rom_chip_i2c_writeReg(0x62u, 1u, 16u, route & ~8u);
}

void IRAM_ATTR setGainCeiling(unsigned rows) {
    if (((REG_READ(gainControl) >> 8) & 127u) == rows) return;
    const unsigned last = rows - 1;
    // Ceiling fields written by SDK agc_reg_init_new. Preserve the adjacent
    // calibrated detector parameter and do not reset the rest of the AGC.
    REG_WRITE(gainCeilingPrimary, (REG_READ(gainCeilingPrimary) & ~0x01fc0000u) | (last << 18));
    REG_WRITE(gainCeilingSecondary, (REG_READ(gainCeilingSecondary) & ~0x000001fcu) | (last << 2));
    REG_WRITE(gainControl, (REG_READ(gainControl) & ~0x00007f00u) | (rows << 8));
    REG_WRITE(gainCeilingSignal, (REG_READ(gainCeilingSignal) & ~0x00000ff0u) | (rows << 4));
    fence();
}

void IRAM_ATTR applyReceiveGain() {
    if (autoGain) {
        // Use the SDK/ROM RX path and release the forced gain table index.
        if (gainCeilingEnabled) setGainCeiling(agcMaxRows);
        phy_force_rx_gain(0, 0);
        enable_agc();
        fence();
        return;
    }
    /* TX overwrites the analog DCO banks. Reload the selected calibration
     * row on manual RX, even if the requested index is unchanged. */
    phy_force_rx_gain(0, currentGain);
    fence();
    esp_rom_delay_us(2);
    phy_force_rx_gain(1, currentGain);
    fence();
}

void IRAM_ATTR restoreReceive() {
    // Disable the PA while the TX debug controls still own the analog bus.
    rom_pbus_xpd_tx_off();
    // Use the same hardware exit sequence as the SDK carrier-test stop.
    txcal_work_mode();
    ram_pbus_xpd_rx_on(1);
    set_rxclk_en(1);
    normalLO();
    applyReceiveGain();
}

bool configureTransmit(const CbRadioConfig& config) {
    /* Calibrate the nearest physical Wi-Fi channel once. The custom shared
     * LO is restored outside the timed loop after calibration. */
    long channel = (long(config.loKHz) - 2407000 + 2500) / 5000;
    if (channel < 1) channel = 1;
    if (channel > 13) channel = 13;
    esp_phy_wifi_tx_tone(1, unsigned(channel), 80);
    REG_WRITE(tone1, REG_READ(tone1) & ~toneGate);
    REG_WRITE(tone2, REG_READ(tone2) & ~toneGate);
    rom_pbus_xpd_tx_off();
    wifi_get_tx_tab(unsigned(channel), digitalGain, bbGain, rfGain, 0);

    unsigned first = 0;
    while (first < gainStages && (nativeRF[first] != (rfGain[0] & 63u) || nativeBB[first] != bbGain[0]))
        ++first;
    if (first == gainStages) return false;
    const int calibration = int(digitalGain[0]) + nativePower[first];
    const int baseline = calibration - nativePower[0] + toneMargin;
    const unsigned requested = unsigned(config.power) + config.ask;
    unsigned stage = 0;
    for (unsigned n = 0; n < gainStages; ++n)
        if (baseline + int(requested) + nativePower[n] - calibration >= toneMargin) stage = n;
    int attenuation = baseline + int(requested) + nativePower[stage] - calibration;
    if (attenuation < 0) return false;
    if (attenuation > 127) { attenuation = 127; ++stats.attenuationClipped; }

    txRF = nativeRF[stage];
    txBB = nativeBB[stage];
    const unsigned bbIndex = txbbgain_to_index(txBB);
    if (bbIndex > 1u) return false;
    const unsigned dc = 0xa8u + 8u * bbIndex;
    for (unsigned n = 0; n < 4; ++n)
        txDco[n] = uint16_t(phy_param[dc + 2 * n]) | (uint16_t(phy_param[dc + 2 * n + 1]) << 8);
    power.requestedQuarterDb = uint16_t(requested);
    power.baselineQuarterDb = uint16_t(baseline);
    power.ncoAttenuation = uint16_t(attenuation);
    power.row = 255;
    for (unsigned n = 0; n < 32; ++n)
        if ((rfGain[n] & 63u) == txRF && bbGain[n] == txBB) { power.row = uint8_t(n); break; }
    power.stage = uint8_t(stage);
    power.gain = {int16_t(calibration - nativePower[stage]), txRF, txBB};
    tableValid = true;

    /* Exit the vendor TX calibration mux before opening the receiver. */
    esp_phy_wifi_tx_tone(0, 0, 0);
    rom_pbus_xpd_tx_off();
    set_txclk_en(0);
    return true;
}
}

bool cbRadioInit() {
    if (initialized) return true;
    esp_err_t error = nvs_flash_init();
    if (error == ESP_ERR_NVS_NO_FREE_PAGES || error == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        error = nvs_flash_erase();
        if (error == ESP_OK) error = nvs_flash_init();
    }
    if (error != ESP_OK) return false;
    error = esp_event_loop_create_default();
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) return false;
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    config.static_rx_buf_num = 4;
    config.ampdu_rx_enable = 0;
    config.ampdu_tx_enable = 0;
    if (esp_wifi_init(&config) != ESP_OK || esp_wifi_set_storage(WIFI_STORAGE_RAM) != ESP_OK ||
        esp_wifi_set_mode(WIFI_MODE_NULL) != ESP_OK || esp_wifi_start() != ESP_OK ||
        esp_wifi_set_ps(WIFI_PS_NONE) != ESP_OK || esp_wifi_set_promiscuous(true) != ESP_OK ||
        esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE) != ESP_OK) return false;
    wifi_promiscuous_filter_t quiet = {};
    if (esp_wifi_set_promiscuous_filter(&quiet) != ESP_OK) return false;
    initialized = true;
    return true;
}

bool cbRadioConfigure(const CbRadioConfig& config) {
    if (config.loKHz < 2400000 || config.loKHz > 2483500 || config.power > 191 ||
        config.rfGain > 7 || config.vga > 7 || config.ask > 127 || config.ifK > 1 || config.bbGain > 32)
        return false;
    cbRadioRelease();
    if (!cbRadioInit()) return false;
    stats = {};
    power = {};
    tableValid = false;
    listener = config.listenOnly;
    chip_v7_set_chan(2412, 0);
    saved = {REG_READ(tone1), REG_READ(tone2), REG_READ(fraction),
             REG_READ(mux), REG_READ(constantI), REG_READ(constantQ)};
    configured = true;
    if (!listener && !configureTransmit(config)) { cbRadioRelease(); return false; }

    const uint32_t loKHz = config.loKHz - unsigned(config.ifK) * 78u;
    phy_set_freq(loKHz / 1000u, int(loKHz % 1000u));
    normalLO();
    ram_stop_tx_tone(1);
    pbus_workmode();
    rom_pbus_xpd_tx_off();
    ram_pbus_xpd_rx_on(1);
    set_rxclk_en(1);

    maxGain = (REG_READ(gainControl) >> 8) & 127u;
    if (!maxGain || maxGain >= 80) { cbRadioRelease(); return false; }
    autoGain = config.rfGain == 7 && gainOverride < 0;
    gainCeilingEnabled = autoGain && maxGain > agcMaxRows;
    if (config.bbGain < 32) lastBbGain = config.bbGain;
    /* C6 gain codes index calibrated gain/DC rows. RF/BB/VGA preserve
     * relative order but are hardware steps, not calibrated dB readouts. */
    unsigned requestedGain = 24u + lastBbGain + 6u * config.rfGain + config.vga;
    if (gainOverride >= 0) requestedGain = unsigned(gainOverride);
    currentGain = 0;
    if (!autoGain) {
        currentGain = requestedGain < maxGain ? requestedGain : maxGain;
        phy_force_rx_gain(0, 0);
        enable_agc();
    }
    applyReceiveGain();
    esp_rom_delay_us(20000);
    receiveBaseline = {REG_READ(tone1), REG_READ(tone2), REG_READ(fraction),
                       REG_READ(mux), REG_READ(constantI), REG_READ(constantQ)};

    if (!listener) {
        const uint32_t base = (REG_READ(tone1) & ~(toneFields | toneGate)) | fineMode |
                              (uint32_t(uint8_t(0u - power.ncoAttenuation)) << 10);
        const uint32_t highFraction = REG_READ(fraction) & ~3u;
        for (int direction = -1; direction <= 1; ++direction) {
            // Two 39.0625 kHz steps reproduce the upstream ±78.125 kHz pulses.
            const uint32_t code = uint32_t((int(config.ifK) + direction) * int(toneStep)) & 4095u;
            txTone[direction + 1] = base | toneGate | (code >> 2);
            txFraction[direction + 1] = highFraction | (code & 3u);
        }
        REG_WRITE(tone1, base);
        REG_WRITE(tone2, REG_READ(tone2) & ~toneGate);
        stats.ncoMode = fineMode;
        stats.ncoStep = toneStep;
    }
    fence();
    return true;
}

void cbRadioSetGainOverride(int index) { gainOverride = index; }
unsigned IRAM_ATTR cbRadioCurrentGain() { return currentGain; }
unsigned IRAM_ATTR cbRadioMaxGain() { return autoGain && maxGain > agcMaxRows ? agcMaxRows : maxGain; }
bool IRAM_ATTR cbRadioAutoGain() { return autoGain; }

bool IRAM_ATTR cbRadioTxBegin() {
    if (!configured || listener || transmitting) return false;
    const uint32_t started = esp_cpu_get_cycle_count();
    REG_WRITE(CbIq::control1, REG_READ(CbIq::control1) & ~3u);
    REG_WRITE(tone1, REG_READ(tone1) & ~toneGate);
    set_rxclk_en(0);
    pbus_debugmode();
    ram_pbus_xpd_tx_on(txRF, txBB);
    pbus_set_dco(txDco);
    set_txclk_en(1);
    normalLO();
    REG_WRITE(constantI, 0);
    REG_WRITE(constantQ, 0);
    REG_WRITE(mux, REG_READ(mux) & ~3u);
    fence();
    transmitting = true;
    const uint32_t elapsed = esp_cpu_get_cycle_count() - started;
    if (elapsed > stats.maxTxBeginCycles) stats.maxTxBeginCycles = elapsed;
    return true;
}

void IRAM_ATTR cbRadioTone(int direction) {
    const unsigned index = unsigned(direction + 1);
    if (!transmitting || index > 2) return;
    REG_WRITE(fraction, txFraction[index]);
    REG_WRITE(tone1, txTone[index]);
    fence();
}

void IRAM_ATTR cbRadioTxEnd() {
    if (!transmitting) return;
    const uint32_t started = esp_cpu_get_cycle_count();
    ram_stop_tx_tone(1);
    REG_WRITE(tone1, receiveBaseline.tone1 & ~toneGate);
    REG_WRITE(tone2, receiveBaseline.tone2 & ~toneGate);
    REG_WRITE(fraction, receiveBaseline.fraction);
    REG_WRITE(mux, receiveBaseline.mux);
    REG_WRITE(constantI, receiveBaseline.i);
    REG_WRITE(constantQ, receiveBaseline.q);
    fence();
    set_txclk_en(0);
    restoreReceive();
    transmitting = false;
    const uint32_t elapsed = esp_cpu_get_cycle_count() - started;
    if (elapsed > stats.maxTxEndCycles) stats.maxTxEndCycles = elapsed;
}

void cbRadioRelease() {
    if (!configured) return;
    cbRadioTxEnd();
    if (!listener) esp_phy_wifi_tx_tone(0, 0, 0);
    ram_stop_tx_tone(1);
    pbus_workmode();
    rom_pbus_xpd_tx_off();
    ram_pbus_xpd_rx_on(0);
    set_txclk_en(0);
    set_rxclk_en(0);
    if (gainCeilingEnabled) {
        setGainCeiling(maxGain);
        gainCeilingEnabled = false;
    }
    phy_force_rx_gain(0, 0);
    enable_agc();
    REG_WRITE(tone1, saved.tone1 & ~toneGate);
    REG_WRITE(tone2, saved.tone2 & ~toneGate);
    REG_WRITE(fraction, saved.fraction);
    REG_WRITE(constantI, saved.i);
    REG_WRITE(constantQ, saved.q);
    REG_WRITE(mux, saved.mux);
    fence();
    configured = false;
}

bool IRAM_ATTR cbRadioHealthy() { return configured; }
void cbRadioGetStats(CbRadioStats& result) { result = stats; }
// PBUS snapshots describe only the C5 backend; keep the command's terminator.
bool cbRadioGetBusWord(unsigned, CbRadioBusWord&) { return false; }
bool cbRadioGetTxGainRow(unsigned index, CbRadioTxGainRow& result) {
    if (!tableValid || index >= 32) return false;
    result = {digitalGain[index], uint16_t(rfGain[index] & 63u), bbGain[index]};
    return true;
}
void cbRadioGetTxPower(CbRadioTxPower& result) { result = power; }
