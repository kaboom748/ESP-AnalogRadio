#pragma once

bool prepareTxRf(uint8_t apwr) {
  // La porte RF reste fermée tant que le tampon audio ne déclenche pas Timer1.
  if (!tune(frequencyHz - toneOffsetHz)) return false;
  saveRfState();
  enterManual();
  if (!txPathOn()) return false;
  set_ana_scale(apwr);
  toneProgramOff();
  set_txclk(1);
  return true;
}

void beginTx(uint8_t requestedApwr) {
  if (requestedApwr > maxApwr) {
    Serial.println("ERR TX_APWR");
    return;
  }
  if (!receiveRequested) {
    Serial.println("ERR SELECT_RX");
    return;
  }
  if (!prepareTxRf(requestedApwr)) {
    if (!releaseTxRf()) stopPhy();
    Serial.println("ERR TX_RF");
    return;
  }
  currentApwr = requestedApwr;
  head = tail = 0;
  receivedAudio = streamStarted = false;
  // Une trame tronquée ne doit pas absorber READY dans le lecteur série.
  static const uint8_t rxBoundary[frameSize] = {};
  Serial.write(rxBoundary, sizeof(rxBoundary));
  Serial.flush();
  uartEnterStream();
  transmitting = true;
  lastAudioAt = millis();
  lastReadyAt = lastAudioAt - READY_REPEAT_MS;
}

void endTx(const char* error) {
  stopFM();
  transmitting = false;
  if (!releaseTxRf()) {
    stopPhy();
    error = "TX_RF";
  }
  uartReturnCommands();
  if (error) {
    Serial.printf("ERR %s\n", error);
    // Les derniers octets audio en vol appartiennent à l'ancienne session.
    const uint32_t drainAt = millis();
    do {
      while (Serial.available() > 0) Serial.read();
      delay(0);
    } while (millis() - drainAt < TX_FAULT_DRAIN_MS);
  }
}

void pollTx() {
  const uint32_t raw = rd32(UART_INT_RAW);
  const uint32_t errors = raw & (UART_RX_OVERFLOW_RAW | UART_RX_FRAME_RAW | UART_RX_PARITY_RAW);
  if (errors) wr32(UART_INT_CLR, errors);
  if (timerFinished) {
    endTx(timerError ? timerError : "TX_TIMING");
    return;
  }

  const uint32_t now = millis();
  const uint32_t count = fifoCount();
  if (count) {
    receivedAudio = true;
    lastAudioAt = now;
  }
  for (uint32_t n = 0; n < count; ++n) {
    const uint8_t sample = rd32(UART_FIFO) & 0xffu;
    const uint32_t position = head;
    if (position - tail >= RAM_SIZE) continue;
    audioRing[position & RING_MASK] = sample == 0x80 ? 0 : sample;
    memw();
    head = position + 1u;
  }

  if (!streamStarted && occupancyBytes() >= START_LEVEL) {
    startFM();
    streamStarted = true;
  }
  if (now - lastAudioAt >= (receivedAudio ? UART_TIMEOUT_MS : TX_ARM_TIMEOUT_MS)) {
    endTx(nullptr);
    return;
  }
  if (!receivedAudio && now - lastReadyAt >= READY_REPEAT_MS && txCount() <= 123) {
    for (uint8_t i = 0; i < 4; ++i) uartSend(streamReady);
    lastReadyAt = now;
  }
}

// Valider avant toute conversion en uint8_t : ni signe, ni décimale, ni débordement.
