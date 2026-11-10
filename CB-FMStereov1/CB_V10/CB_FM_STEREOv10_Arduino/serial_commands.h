#pragma once

bool parseApwr(const char* text, uint8_t& value) {
  if (!text || !*text) return false;
  uint16_t parsed = 0;
  for (const char* digit = text; *digit; ++digit) {
    if (*digit < '0' || *digit > '9') return false;
    parsed = parsed * 10u + static_cast<uint8_t>(*digit - '0');
    if (parsed > maxApwr) return false;
  }
  value = static_cast<uint8_t>(parsed);
  return true;
}

void command(char* line) {
  const char* name = strtok(line, " \t");
  if (!name) return;
  const char* value = strtok(nullptr, " \t");
  if (!value && strcmp(name, "STATUS") == 0) {
    if (ESP.getCpuFreqMHz() == 160) Serial.println("OK STATUS CB_FM_STEREO 4");
    else Serial.println("ERR CPU160");
    return;
  }
  if (!value && strcmp(name, "STOP") == 0) {
    receiveRequested = false;
    stopPhy();
    Serial.println("OK STOP");
    return;
  }
  if (ESP.getCpuFreqMHz() != 160) {
    Serial.println("ERR CPU160");
    return;
  }
  if (strcmp(name, "TX") == 0) {
    uint8_t selectedApwr;
    if (!parseApwr(value, selectedApwr) || strtok(nullptr, " \t")) {
      Serial.println("ERR TX_APWR");
      return;
    }
    // La consigne et le démarrage sont atomiques pour toutes les sources audio.
    beginTx(selectedApwr);
    return;
  }
  if (strcmp(name, "RX") != 0 || !value) {
    Serial.println("ERR COMMAND");
    return;
  }

  char* end;
  uint32_t selected = strtoul(value, &end, 10);
  if (*value < '0' || *value > '9' || *end) selected = 0;
  if (selected >= 1 && selected <= 13) selected = (2407UL + 5UL * selected) * 1000000UL + toneOffsetHz;
  if (selected < minFrequencyHz || selected > maxFrequencyHz) {
    Serial.println("ERR FREQUENCY");
    return;
  }

  const char* mode = strtok(nullptr, " \t");
  const Profile* selectedProfile = nullptr;
  for (const auto& candidate : profiles) {
    if (mode && strcmp(mode, candidate.name) == 0) selectedProfile = &candidate;
  }
  if (!selectedProfile) {
    Serial.println("ERR MODE");
    return;
  }

  const char* fine = strtok(nullptr, " \t");
  const int32_t offset = fine ? strtol(fine, &end, 10) : 0;
  if ((fine && (*end || end == fine)) || offset < -maxRxFineHz || offset > maxRxFineHz || strtok(nullptr, " \t")) {
    Serial.println("ERR FINE");
    return;
  }

  frequencyHz = selected;
  rxFineHz = offset;
  profile = selectedProfile;
  receiveRequested = true;
}
