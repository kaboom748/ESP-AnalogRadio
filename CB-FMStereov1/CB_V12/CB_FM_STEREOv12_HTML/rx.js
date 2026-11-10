'use strict';

  function setSquelch(open) {
    if (open === squelchOpen) return;
    squelchOpen = open;
    audio?.node.port.postMessage({type: 'squelch', open});
  }

  function updateSignal(magnitude) {
    const level = Math.max(-85, 20 * Math.log10(Math.max(1, magnitude) / 30000));
    signalDb = signalDb === null ? level : signalDb + (level - signalDb) * .08;
    lastSignalAt = performance.now();
    if (noiseSamples < 150) {
      noiseDb = noiseDb === null ? level : Math.min(noiseDb + .005, signalDb);
      noiseSamples++;
    } else if (signalDb < noiseDb) {
      noiseDb += (signalDb - noiseDb) * .07;
    } else if (!squelchOpen && signalDb < noiseDb + 5) {
      noiseDb += (signalDb - noiseDb) * .0005;
    }
    updateSquelch();
  }
  function updateSquelch() {
    const threshold = Number($('squelch').value);
    const aboveNoise = signalDb === null ? 0 : Math.max(0, signalDb - noiseDb);
    if (!['rx', 'arming'].includes(state) || performance.now() - lastSignalAt > 180) {
      carrierSeenAt = 0;
      setSquelch(false);
    } else if (!threshold) {
      setSquelch(true);
    } else if (noiseSamples >= 100) {
      if (aboveNoise >= threshold - (squelchOpen ? 3 : 0)) carrierSeenAt = performance.now();
      if (aboveNoise >= threshold) setSquelch(true);
      else if (performance.now() - carrierSeenAt > 160) setSquelch(false);
    }
  }
  function showRxStatus() {
    const active = ['rx', 'arming'].includes(state) && signalDb !== null;
    const level = active ? Math.round(clamp(signalDb - noiseDb, 0, 60)) : 0;
    $('signal').value = level;
    const threshold = Number($('squelch').value);
    $('squelchValue').value = threshold ? threshold + ' dB above noise' : 'Always open';
    const audioModeLabel = activeTune?.profile === 'MONO' ? 'Mono' : playback?.pilotLocked ? 'Stereo' : 'Mono · waiting for stereo pilot';
    $('rxStatus').textContent = (active ? 'Signal +' + level + ' dB · ' + audioModeLabel : 'Signal —') +
      ' · Squelch ' + (squelchOpen ? 'open' : 'closed') +
      (active && missingFrames ? ' · Missing frames ' + missingFrames : '');
  }
  function resetReceiver(keepNoise = false) {
    expectedIndex = 0;
    missingFrames = 0;
    rxUsed = 0;
    signalDb = null;
    if (!keepNoise) {
      noiseDb = null;
      noiseSamples = 0;
    }
    lastSignalAt = 0;
    carrierSeenAt = 0;
    setSquelch(false);
    audioMode('rx', activeTune.profile);
  }
  function flushRx() {
    if (!rxUsed || !audio) return;
    const samples = rxBuffer.slice(0, rxUsed);
    audio.node.port.postMessage({type: 'rx', epoch, samples}, [samples.buffer]);
    rxUsed = 0;
  }
  function radioFrame(index, magnitude, payload) {
    if (state === 'returning' && returnAfterReady && activeTune && index < activeTune.rate) {
      resumeReceiver(activeTune, activeTune.lo, true);
    }
    if (!['rx', 'arming'].includes(state) || !activeTune) return;
    const gap = (index - expectedIndex) >>> 0;
    if (gap >= 0x80000000) return;
    if (gap > 2 * activeTune.rate && performance.now() - lastSignalAt < 180) return;
    expectedIndex = (index + payloadSize) >>> 0;
    updateSignal(magnitude);
    if (gap) {
      missingFrames += Math.ceil(gap / payloadSize);
      if (gap > 3600) {
        rxUsed = 0;
        audioMode('rx', activeTune.profile);
        audio?.node.port.postMessage({type: 'squelch', open: squelchOpen});
      } else {
        if (rxUsed + gap + payloadSize > rxBuffer.length) flushRx();
        rxBuffer.fill(0x80, rxUsed, rxUsed + gap);
        rxUsed += gap;
      }
    }
    if (activeTune.profile === 'HQ') window.cbRds?.feedRx(index, payload);
    if (rxUsed + payloadSize > rxBuffer.length) flushRx();
    rxBuffer.set(payload, rxUsed);
    rxUsed += payloadSize;
    if (rxUsed >= activeTune.rate / 150) flushRx();
  }

  function resumeReceiver(tune, lo, keepNoise) {
    const unexpected = state === 'tx' || state === 'keying';
    if (unexpected) cancelDtmf(false);
    cancelTimer();
    clearTxQueue();
    enableMicTrack(false);
    pttHold = null;
    txApwr = null;
    txRequest++;
    returnAfterReady = false;
    activeTune = {...tune, lo};
    pendingTune = null;
    resetReceiver(keepNoise);
    $('actual').textContent = 'RX tuned to ' + frequencyFormat.format(lo / 1e6) + ' MHz';
    setState('rx', returnMessage || (unexpected ? 'The radio stopped transmitting.' : 'Listening · ' + tune.profile),
      returnError || unexpected);
    returnMessage = '';
    returnError = false;
  }
  function workletMessage(data) {
    if (data.epoch !== epoch) return;
    if (window.cbRds?.workletMessage(data)) return;
    if (data.type === 'rx-status') { playback = data; return; }
    if (data.type !== 'tx' || !['keying', 'tx'].includes(state) || !pttHold) return;
    const samples = data.samples;
    if (!(samples instanceof Uint8Array) || samples.length !== txChunkBytes) {
      if (state === 'tx') finishTransmit('Invalid audio block. Returning to receive.', true);
      return;
    }
    if (queuedTxBytes >= txQueueLimit) {
      // A 5 ms block contains an integer number of stereo pilot cycles.
      txQueue.shift();
      queuedTxBytes -= txChunkBytes;
    }
    queuedTxBytes += samples.length;
    txQueue.push(samples);
    pumpTx();
  }
