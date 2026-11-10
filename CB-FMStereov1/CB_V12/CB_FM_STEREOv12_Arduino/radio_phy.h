#pragma once

bool phyReady = false;
bool receiveRequested = false;
bool transmitting = false;
bool receivedAudio = false;
bool streamStarted = false;
uint32_t frequencyHz = defaultFrequencyHz;
int32_t rxFineHz = 0;
const Profile* profile = &profiles[0];
uint32_t lastAudioAt = 0;
uint32_t lastReadyAt = 0;

bool startPhy() {
  if (phyReady) return true;
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  wifi_station_set_auto_connect(0);
  if (!WiFi.mode(WIFI_STA)) return false;
  wifi_station_set_auto_connect(0);
  wifi_station_set_reconnect_policy(false);
  wifi_station_disconnect();
  if (!wifi_set_sleep_type(NONE_SLEEP_T)) return false;
  delay(100);
  phyReady = WiFi.status() != WL_CONNECTED;
  return phyReady;
}

void stopPhy() {
  gateOff();
  WiFi.mode(WIFI_OFF);
  phyReady = false;
}

// Choisir le canal PHY le plus proche, puis régler le PLL à environ 977 Hz.
uint32_t tune(uint32_t targetHz) {
  const int64_t channelFromHz = (static_cast<int64_t>(targetHz) - 2407000000LL + 2500000LL) / 5000000LL;
  const uint8_t channel = channelFromHz < 1 ? 1 : channelFromHz > 13 ? 13 : channelFromHz;
  const int mhz = 2407 + 5 * channel;
  const int64_t baseHz = static_cast<int64_t>(mhz) * 1000000;
  const int64_t deltaHz = targetHz - baseHz;
  const int32_t offset = (deltaHz * 1024 + (deltaHz < 0 ? -500000 : 500000)) / 1000000;
  if (!startPhy() || !wifi_set_channel(channel)) return 0;
  delay(40);
  if (wifi_get_channel() != channel || WiFi.status() == WL_CONNECTED) return 0;
  set_rf_freq_offset(chip6_phy_init_ctrl[1], mhz, offset);
  return baseHz + static_cast<int64_t>(offset) * 1000000 / 1024;
}

void listen() {
  const uint32_t lo = tune(static_cast<int64_t>(frequencyHz) + rxFineHz);
  if (!lo) {
    receiveRequested = false;
    stopPhy();
    Serial.println("ERR RX_RF");
    return;
  }
  Serial.printf("OK RX %lu %lu %s\n", static_cast<unsigned long>(lo),
                static_cast<unsigned long>(profile->audioRate), profile->name);
  Serial.flush();
  Stats stats = {};
  const uint32_t processorStatus = xt_rsil(15);
  // L'UART réveille le lecteur de commandes sans consommer le premier octet.
  runReceiver(*profile, stats);
  xt_wsr_ps(processorStatus);
  Serial.flush();
}

