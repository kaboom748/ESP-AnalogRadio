/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "radio.h"

#include "esp_event.h"
#include "esp_wifi.h"
#include "sdkconfig.h"

#if !CONFIG_IDF_TARGET_ESP32C5
#error "This radio backend targets ESP32-C5."
#endif

// Arduino's ESP32-C5 core includes these PHY routines in libphy.a.
// Its SDK build omits the public certification-test wrapper.
extern "C" {
void RFChannelSel(unsigned mhz, unsigned primary);
void phy_set_rf_freq_offset(unsigned crystal, unsigned mhz, int offsetKHz);
void phy_param_track_tot(unsigned, unsigned);
void phy_txcal_debuge_mode_new();
void phy_txcal_work_mode();
void phy_pbus_xpd_tx_off();
void phy_stop_tx_tone(unsigned);
void phy_start_tx_tone_step(unsigned enabled, unsigned step, unsigned backoff,
                            unsigned enabled2, unsigned step2, unsigned backoff2);
extern uint8_t phy_param[];
}

namespace {
struct Channel {
  uint8_t number;
  unsigned mhz;
  wifi_band_mode_t bandMode;
};

constexpr Channel lowChannel{7, 2442, WIFI_BAND_MODE_2G_ONLY};
constexpr Channel highChannel{36, 5180, WIFI_BAND_MODE_5G_ONLY};
constexpr unsigned cwStep = 10;  // +390.625 kHz, 78.125 kHz above the SSB reference.
constexpr unsigned phyBandParam = 42;
constexpr unsigned crystalParam = 49;
constexpr int pllOffsetKHz = 294;

constexpr uint32_t toneRoute = 0x600a040cu;
constexpr uint32_t toneGainI = 0x600a0410u;
constexpr uint32_t toneGainQ = 0x600a0414u;
constexpr uint32_t tone1 = 0x600a041cu;
constexpr uint32_t tone2 = 0x600a0420u;
constexpr uint32_t toneStepLow = 0x600a0428u;
constexpr uint32_t dacScale = 0x600a0c04u;
constexpr uint32_t txDc = 0x600a0c08u;

constexpr uint32_t toneGate = 1u << 18;
constexpr uint32_t toneWideStep = 1u << 29;
constexpr uint32_t txDcEnable = 1u << 26;
constexpr uint32_t tonePreserved = 0xf0000000u;
constexpr unsigned scaleShift = 10;
constexpr uint32_t txDCFields = 0x000fffffu;
constexpr unsigned maxApwr = 191;
constexpr unsigned minBaseAttenuation = 32;
constexpr unsigned maxBaseAttenuation = 80;

bool initialized = false;
bool wifiRunning = false;
bool phyInTestMode = false;
bool highBandSelected = false;
uint32_t iqControl = 0;

__attribute__((always_inline)) inline uint32_t readRegister(uint32_t address) {
  return *reinterpret_cast<volatile uint32_t*>(address);
}

__attribute__((always_inline)) inline void writeRegister(uint32_t address, uint32_t word) {
  *reinterpret_cast<volatile uint32_t*>(address) = word;
}

__attribute__((always_inline)) inline void radioFence() {
  asm volatile("fence iorw, iorw" ::: "memory");
}

__attribute__((always_inline)) inline uint32_t attenuationBits(unsigned qdb) {
  // The tone generator takes an 8-bit two's-complement scale.
  return static_cast<uint32_t>(static_cast<uint8_t>(0u - qdb)) << scaleShift;
}

unsigned apwrAttenuation(uint8_t apwr) {
  const unsigned bounded = apwr <= maxApwr ? apwr : maxApwr;
  return minBaseAttenuation +
         (bounded * (maxBaseAttenuation - minBaseAttenuation) + maxApwr / 2) / maxApwr;
}

void closeToneGates() {
  writeRegister(tone1, readRegister(tone1) & ~toneGate);
  writeRegister(tone2, readRegister(tone2) & ~toneGate);
  radioFence();
}

bool initWifi();

bool startRadio() {
  if (!phyInTestMode) {
    if (!initWifi() || esp_wifi_start() != ESP_OK) return false;
    wifiRunning = true;
    const Channel &channel = highBandSelected ? highChannel : lowChannel;
    if (esp_wifi_set_band_mode(channel.bandMode) != ESP_OK ||
        esp_wifi_set_ps(WIFI_PS_NONE) != ESP_OK ||
        esp_wifi_set_promiscuous(true) != ESP_OK ||
        esp_wifi_set_channel(channel.number, WIFI_SECOND_CHAN_NONE) != ESP_OK) {
      radioOff();
      return false;
    }

    // Keep the vendor's calibrated RF stage and band-dependent I/Q routing.
    RFChannelSel(channel.mhz, 0);
    if ((phy_param[phyBandParam] != 0) != highBandSelected) {
      radioOff();
      return false;
    }
    phy_param_track_tot(0, 0);
    phy_txcal_debuge_mode_new();
    phyInTestMode = true;
  }
  return true;
}

bool initWifi() {
  if (initialized) return true;
  const esp_err_t eventStatus = esp_event_loop_create_default();
  if (eventStatus != ESP_OK && eventStatus != ESP_ERR_INVALID_STATE) return false;
  wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
  if (esp_wifi_init(&config) != ESP_OK) return false;
  if (esp_wifi_set_storage(WIFI_STORAGE_RAM) != ESP_OK ||
      esp_wifi_set_mode(WIFI_MODE_NULL) != ESP_OK) {
    (void)esp_wifi_deinit();
    return false;
  }
  initialized = true;
  return true;
}
}  // namespace

void radioSetBand(bool highBand) {
  if (highBandSelected == highBand) return;
  radioOff();
  highBandSelected = highBand;
}

bool radioCW(uint8_t apwr) {
  if (!startRadio()) return false;
  const Channel &channel = highBandSelected ? highChannel : lowChannel;
  closeToneGates();
  writeRegister(txDc, readRegister(txDc) & ~(txDcEnable | txDCFields));
  phy_set_rf_freq_offset(phy_param[crystalParam], channel.mhz, 0);
  const uint32_t prefix = (readRegister(tone1) & tonePreserved) | toneWideStep | (cwStep >> 2);
  writeRegister(tone1, prefix);
  writeRegister(tone2, readRegister(tone2) & tonePreserved);
  writeRegister(toneStepLow, (readRegister(toneStepLow) & ~0xfu) | (cwStep & 3u));
  writeRegister(dacScale, readRegister(dacScale) & 0xff0000ffu);
  writeRegister(toneGainI, 0);
  writeRegister(toneGainQ, 0);
  writeRegister(toneRoute, readRegister(toneRoute) & ~3u);
  writeRegister(tone1, prefix | toneGate | attenuationBits(apwrAttenuation(apwr)));
  radioFence();
  return true;
}

bool radioSSB() {
  if (!startRadio()) return false;
  const Channel &channel = highBandSelected ? highChannel : lowChannel;
  closeToneGates();
  phy_stop_tx_tone(1);
  phy_set_rf_freq_offset(phy_param[crystalParam], channel.mhz, pllOffsetKHz);
  // Signed ten-bit I and Q feed the same calibrated converter in both bands.
  // Preserve the PHY's band-specific I/Q swap bit.
  iqControl = (readRegister(txDc) & ~txDCFields) | txDcEnable;
  writeRegister(txDc, iqControl);
  radioFence();
  return true;
}

void IRAM_ATTR radioIQ(int16_t i, int16_t q) {
  const uint32_t packed = (uint32_t(i) & 0x3ffu) | ((uint32_t(q) & 0x3ffu) << 10);
  writeRegister(txDc, iqControl | packed);
}

void radioOff() {
  if (phyInTestMode) {
    closeToneGates();
    writeRegister(txDc, readRegister(txDc) & ~(txDcEnable | txDCFields));
    radioFence();
    phy_start_tx_tone_step(0, 0, 0, 0, 0, 0);
    phy_txcal_work_mode();
    phy_pbus_xpd_tx_off();
    phyInTestMode = false;
  }
  if (wifiRunning) {
    (void)esp_wifi_set_promiscuous(false);
    (void)esp_wifi_stop();
    wifiRunning = false;
  }
}
