/* SPDX-License-Identifier: GPL-3.0-or-later
 * C5 PHY bring-up and hardware I/Q accumulator adapted from the
 * ESP-AnalogRadio CB_V14 C5 receiver and ESPARGOS ESP-SDR.
 */
#include "rx_radio.h"

#include "esp_cpu.h"
#include "esp_event.h"
#include "esp_rom_sys.h"
#include "esp_wifi.h"
#include "sdkconfig.h"

#if !CONFIG_IDF_TARGET_ESP32C5
#error "This receiver backend targets ESP32-C5."
#endif

extern "C" {
extern uint8_t phy_param[];
void phy_set_chanfreq(unsigned mhz, unsigned mode);
void phy_set_rf_freq_offset(unsigned crystal, unsigned mhz, int offsetKHz);
void phy_stop_tx_tone(unsigned);
void phy_pbus_workmode();
void phy_pbus_xpd_rx_on(unsigned);
void phy_pbus_xpd_tx_off();
void phy_set_rxclk_en(unsigned);
void phy_force_rx_gain(unsigned force, unsigned index);
void phy_enable_agc();
}

namespace {
constexpr uint32_t lowIfKHz = 24;
constexpr uint32_t lowMinKHz = 2412000;
constexpr uint32_t lowMaxKHz = 2472000;
constexpr uint32_t highMinKHz = 5180000;
constexpr uint32_t highMaxKHz = 5885000;
constexpr unsigned crystalParam = 49;
constexpr unsigned bandParam = 42;
constexpr int maxManualGain = 55;

constexpr uint32_t estimatorInput = 0x600a044cu;
constexpr uint32_t estimatorControl = 0x600a0450u;
constexpr uint32_t estimatorI = 0x600a0464u;
constexpr uint32_t estimatorQ = 0x600a0468u;
constexpr uint32_t estimatorStatus = 0x600a047cu;
constexpr uint32_t gainControl = 0x600a702cu;
constexpr uint32_t estimatorDone = 1u << 16;
constexpr uint32_t estimatorTrigger = 1u << 1;
constexpr uint32_t estimatorEnable = 1u;
constexpr unsigned iqWindowPoints = 64;
constexpr unsigned iqSumShift = 8;
constexpr unsigned timeoutCycles = 1200;  // 5 us at the fixed 240 MHz CPU clock.

bool wifiInitialized = false;
bool wifiRunning = false;
bool receiverPowered = false;
bool estimatorOwned = false;
bool filterSaved = false;
wifi_promiscuous_filter_t savedFilter{};
volatile bool active = false;
uint32_t savedInput = 0;
uint32_t savedControl = 0;
uint32_t estimator = 0;

__attribute__((always_inline)) inline uint32_t readRegister(uint32_t address) {
  return *reinterpret_cast<volatile uint32_t*>(address);
}

__attribute__((always_inline)) inline void writeRegister(uint32_t address, uint32_t word) {
  *reinterpret_cast<volatile uint32_t*>(address) = word;
}

__attribute__((always_inline)) inline void control(uint32_t word) {
  writeRegister(estimatorControl, word);
  asm volatile("fence iorw, iorw" ::: "memory");
}

bool initWifi() {
  if (wifiInitialized) return true;
  const esp_err_t eventStatus = esp_event_loop_create_default();
  if (eventStatus != ESP_OK && eventStatus != ESP_ERR_INVALID_STATE) return false;
  wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
  config.static_rx_buf_num = 4;
  config.ampdu_rx_enable = 0;
  config.ampdu_tx_enable = 0;
  if (esp_wifi_init(&config) != ESP_OK) return false;
  wifiInitialized = true;
  return esp_wifi_set_storage(WIFI_STORAGE_RAM) == ESP_OK &&
         esp_wifi_set_mode(WIFI_MODE_NULL) == ESP_OK;
}

bool setGain(int index) {
  phy_force_rx_gain(0, 0);
  phy_enable_agc();
  if (index == -1) return true;
  const unsigned maximum = (readRegister(gainControl) >> 8) & 127;
  if (static_cast<unsigned>(index) > maximum) return false;
  phy_force_rx_gain(0, static_cast<unsigned>(index));
  esp_rom_delay_us(2);
  phy_force_rx_gain(1, static_cast<unsigned>(index));
  return true;
}

void prepareEstimator() {
  savedInput = readRegister(estimatorInput);
  savedControl = readRegister(estimatorControl);
  estimatorOwned = true;
  control(savedControl & ~estimatorTrigger);
  esp_rom_delay_us(1);
  control(savedControl & ~(estimatorEnable | estimatorTrigger));
  writeRegister(estimatorInput, (savedInput & 0xf3ffffffu) | 0x04000000u);
  estimator = (savedControl & ~(0x00180000u | 0x0001fffcu | 3u)) |
              0x00100000u | ((iqWindowPoints - 1u) << 2) | estimatorEnable;
  control(estimator);
}

__attribute__((always_inline)) inline int16_t readIq(uint32_t address) {
  const int32_t value = static_cast<int32_t>(readRegister(address)) >> iqSumShift;
  if (value < -15000) return -15000;
  if (value > 15000) return 15000;
  return static_cast<int16_t>(value);
}
}  // namespace

bool rxStart(uint32_t carrierKHz, int gainIndex, uint64_t* programmedCarrierHz) {
  const bool low = carrierKHz >= lowMinKHz && carrierKHz <= lowMaxKHz;
  const bool high = carrierKHz >= highMinKHz && carrierKHz <= highMaxKHz;
  if ((!low && !high) || gainIndex < -1 || gainIndex > maxManualGain) return false;

  rxStop();
  if (!initWifi() || esp_wifi_start() != ESP_OK) {
    rxStop();
    return false;
  }
  wifiRunning = true;
  const wifi_band_mode_t band = high ? WIFI_BAND_MODE_5G_ONLY : WIFI_BAND_MODE_2G_ONLY;
  const unsigned seedMHz = high ? 5180u : 2412u;
  const uint8_t channel = high ? 36 : 1;
  const wifi_promiscuous_filter_t quiet{};
  filterSaved = esp_wifi_get_promiscuous_filter(&savedFilter) == ESP_OK;
  if (esp_wifi_set_band_mode(band) != ESP_OK ||
      esp_wifi_set_ps(WIFI_PS_NONE) != ESP_OK ||
      esp_wifi_set_promiscuous_filter(&quiet) != ESP_OK ||
      esp_wifi_set_promiscuous(true) != ESP_OK ||
      esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE) != ESP_OK) {
    rxStop();
    return false;
  }

  // Calibrate the desired frontend on its standard anchor. Set exact kHz
  // through the PLL primitive so channel conversion cannot round the low IF.
  phy_set_chanfreq(seedMHz, 0);
  if ((phy_param[bandParam] != 0) != high) {
    rxStop();
    return false;
  }
  const uint32_t loKHz = carrierKHz - lowIfKHz;
  phy_set_rf_freq_offset(phy_param[crystalParam], loKHz / 1000, loKHz % 1000);
  phy_stop_tx_tone(1);
  phy_pbus_workmode();
  phy_pbus_xpd_tx_off();
  phy_pbus_xpd_rx_on(1);
  phy_set_rxclk_en(1);
  receiverPowered = true;
  if (!setGain(gainIndex)) {
    rxStop();
    return false;
  }

  esp_rom_delay_us(1000);
  prepareEstimator();
  active = true;
  if (programmedCarrierHz) *programmedCarrierHz = uint64_t(carrierKHz) * 1000;
  return true;
}

bool IRAM_ATTR rxSample(int16_t* i, int16_t* q) {
  if (!active) return false;
  const uint32_t start = esp_cpu_get_cycle_count();
  control(estimator | estimatorTrigger);
  uint32_t status;
  do {
    status = readRegister(estimatorStatus);
    if (status & estimatorDone) {
      *i = readIq(estimatorI);
      *q = readIq(estimatorQ);
      break;
    }
  } while (esp_cpu_get_cycle_count() - start < timeoutCycles);
  control(estimator);
  return (status & estimatorDone) != 0;
}

void rxStop() {
  active = false;
  if (estimatorOwned) {
    control(readRegister(estimatorControl) & ~estimatorTrigger);
    esp_rom_delay_us(1);
    control(readRegister(estimatorControl) & ~(estimatorEnable | estimatorTrigger));
    writeRegister(estimatorInput, savedInput);
    control(savedControl);
    estimatorOwned = false;
  }
  if (receiverPowered) {
    phy_force_rx_gain(0, 0);
    phy_enable_agc();
    phy_stop_tx_tone(1);
    phy_pbus_xpd_tx_off();
    phy_pbus_xpd_rx_on(0);
    phy_set_rxclk_en(0);
    receiverPowered = false;
  }
  if (filterSaved) {
    (void)esp_wifi_set_promiscuous_filter(&savedFilter);
    filterSaved = false;
  }
  if (wifiRunning) {
    (void)esp_wifi_set_promiscuous(false);
    (void)esp_wifi_stop();
    wifiRunning = false;
  }
  if (wifiInitialized) {
    (void)esp_wifi_deinit();
    wifiInitialized = false;
  }
}
