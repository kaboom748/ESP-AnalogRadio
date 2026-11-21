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
void phy_param_track_tot(unsigned, unsigned);
void phy_txcal_debuge_mode_new();
void phy_txcal_work_mode();
void phy_pbus_xpd_tx_off();
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
constexpr unsigned amStep = 8;   // 8 × 39,062.5 Hz = +312.5 kHz.
constexpr unsigned cwStep = 16;  // 16 × 39,062.5 Hz = +625 kHz.
constexpr unsigned phyBandParam = 42;

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
constexpr unsigned maxAttenuation = 127;
constexpr unsigned maxApwr = 191;
constexpr unsigned minBaseAttenuation = 32;
constexpr unsigned maxBaseAttenuation = 80;

bool initialized = false;
bool wifiRunning = false;
bool phyInTestMode = false;
bool highBandSelected = false;
volatile bool amArmed = false;
volatile uint32_t amToneWord = 0;
volatile unsigned amBaseAttenuation = 0;

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

bool startRadio(uint8_t apwr, unsigned step, bool am) {
  amArmed = false;
  if (!phyInTestMode) {
    if (!radioInit() || esp_wifi_start() != ESP_OK) return false;
    wifiRunning = true;
    const Channel &channel = highBandSelected ? highChannel : lowChannel;
    if (esp_wifi_set_band_mode(channel.bandMode) != ESP_OK ||
        esp_wifi_set_ps(WIFI_PS_NONE) != ESP_OK ||
        esp_wifi_set_promiscuous(true) != ESP_OK ||
        esp_wifi_set_channel(channel.number, WIFI_SECOND_CHAN_NONE) != ESP_OK) {
      radioOff();
      return false;
    }

    // Match the C5 certification CW startup. Later CW/AM changes reuse the
    // selected channel and calibrated RF/BB stage while serial audio arrives.
    RFChannelSel(channel.mhz, 0);
    if ((phy_param[phyBandParam] != 0) != highBandSelected) {
      radioOff();
      return false;
    }
    phy_param_track_tot(0, 0);
    phy_txcal_debuge_mode_new();
    phyInTestMode = true;
  }

  // Prepare the tested C5 tone path with its gate initially closed.
  closeToneGates();
  const uint32_t prefix = (readRegister(tone1) & tonePreserved) | toneWideStep | (step >> 2);
  writeRegister(tone1, prefix);
  writeRegister(tone2, readRegister(tone2) & tonePreserved);
  writeRegister(toneStepLow, (readRegister(toneStepLow) & ~0xfu) | (step & 3u));
  writeRegister(txDc, readRegister(txDc) & ~txDcEnable);
  writeRegister(dacScale, readRegister(dacScale) & 0xff0000ffu);
  writeRegister(toneGainI, 0);
  writeRegister(toneGainQ, 0);
  writeRegister(toneRoute, readRegister(toneRoute) & ~3u);
  radioFence();

  const unsigned base = apwrAttenuation(apwr);
  if (am) {
    amToneWord = prefix;
    amBaseAttenuation = base;
    amArmed = true;
  } else {
    writeRegister(tone1, prefix | toneGate | attenuationBits(base));
    radioFence();
  }
  return true;
}
}  // namespace

bool radioInit() {
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

void radioSetBand(bool highBand) {
  if (highBandSelected == highBand) return;
  radioOff();
  highBandSelected = highBand;
}

bool radioCW(uint8_t apwr) {
  return startRadio(apwr, cwStep, false);
}

bool radioAM(uint8_t apwr) {
  return startRadio(apwr, amStep, true);
}

void IRAM_ATTR radioEnvelope(uint8_t ask) {
  if (!amArmed) return;
  const unsigned requested = amBaseAttenuation + ask;
  const unsigned qdb = requested <= maxAttenuation ? requested : maxAttenuation;
  writeRegister(tone1, amToneWord | toneGate | attenuationBits(qdb));
  radioFence();
}

void IRAM_ATTR radioMute() {
  if (!amArmed) return;
  writeRegister(tone1, amToneWord);
  radioFence();
}

void radioOff() {
  // Disarm writes from the timer before closing the gate and releasing RF.
  amArmed = false;
  if (phyInTestMode) {
    closeToneGates();
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
