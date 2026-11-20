'use strict';

  function setSquelch(open) {
    if (open === squelchOpen) return;
    squelchOpen = open;
    audio?.node.port.postMessage({type: 'squelch', open});
  }

  function noiseReferenceKey() {
    return `${activeTune.lo}:${activeTune.profile}`;
  }
  function noisePercentile(levels) {
    levels.sort((a, b) => a - b);
    return levels[Math.floor(levels.length * .65)];
  }
  function beginNoiseMeasurement() {
    if (state !== 'rx' || !serial || !activeTune || noiseMeasurement) return;
    noiseMeasurement = {levels: [], deadline: performance.now() + 2200,
      frames: Math.ceil(activeTune.rate * .65 / payloadSize)};
    showRxStatus();
  }
  function freshStereoPilot() {
    return activeTune?.profile !== 'MONO' && playback?.pilotLocked && performance.now() - playback.observedAt < 450;
  }
  function updateSignal(magnitude) {
    const level = Math.max(-85, 20 * Math.log10(Math.max(1, magnitude) / 30000));
    signalDb = signalDb === null ? level : signalDb + (level - signalDb) * .08;
    lastSignalAt = performance.now();
    if (noiseDb === null) {
      if (!freshStereoPilot()) {
        const framesPerSecond = (activeTune?.rate ?? profiles.HQ) / payloadSize;
        if (++noiseWarmup.seen >= .08 * framesPerSecond) {
          noiseWarmup.levels.push(signalDb);
          if (noiseWarmup.levels.length >= .4 * framesPerSecond) {
            noiseDb = noisePercentile(noiseWarmup.levels);
            noiseWarmup = {seen: 0, levels: []};
          }
        }
      }
    } else if (signalDb < noiseDb) {
      noiseDb += (signalDb - noiseDb) * .07;
    } else if (!squelchOpen && signalDb < noiseDb + 5) {
      noiseDb += (signalDb - noiseDb) * .0005;
    }
    if (noiseMeasurement && state === 'rx') {
      const {levels, frames} = noiseMeasurement;
      levels.push(signalDb);
      if (levels.length === frames) {
        noiseDb = noisePercentile(levels);
        (serial.noiseReferences ??= new Map()).set(noiseReferenceKey(), noiseDb);
        noiseMeasurement = null;
      }
    }
    updateSquelch(true);
  }
  function updateSquelch(freshFrame = false) {
    const threshold = Number($('squelch').value);
    const aboveNoise = signalDb === null || noiseDb === null ? 0 : Math.max(0, signalDb - noiseDb);
    const now = performance.now();
    if (!['rx', 'arming'].includes(state) || now - lastSignalAt > 180) {
      carrierSeenAt = 0;
      carrierSamples = 0;
      setSquelch(false);
    } else if (!threshold) {
      carrierSamples = 0;
      setSquelch(true);
    } else if (noiseDb === null) {
      // A station already on air is a valid stereo signal, not a noise reference.
      setSquelch(Boolean(freshStereoPilot()));
    } else {
      const releaseThreshold = threshold - Math.min(3, threshold / 2);
      const qualifyingSamples = Math.ceil((activeTune?.rate ?? profiles.HQ) * .024);
      if (aboveNoise < (squelchOpen ? releaseThreshold : threshold)) carrierSamples = 0;
      else if (freshFrame) {
        // Count RF sample time so a burst of buffered USB data keeps its duration.
        carrierSamples = Math.min(qualifyingSamples, carrierSamples + payloadSize);
        if (carrierSamples >= qualifyingSamples) {
          carrierSeenAt = now;
          if (aboveNoise >= threshold) setSquelch(true);
        }
      }
      if (now - carrierSeenAt > 160) setSquelch(false);
    }
  }
  function showRxStatus() {
    if (noiseMeasurement && (state !== 'rx' || performance.now() > noiseMeasurement.deadline)) {
      noiseMeasurement = null;
    }
    const measure = $('calibrateNoise');
    measure.disabled = state !== 'rx' || !!noiseMeasurement;
    measure.textContent = noiseMeasurement ? 'Measuring channel noise…' : 'Measure clear-channel noise';
    const active = ['rx', 'arming'].includes(state) && signalDb !== null;
    const level = active && noiseDb !== null ? Math.round(clamp(signalDb - noiseDb, 0, 60)) : 0;
    $('signal').value = level;
    const threshold = Number($('squelch').value);
    $('squelchValue').value = threshold ? threshold + ' dB above noise' : 'Always open';
    const audioModeLabel = activeTune?.profile === 'MONO' ? 'Mono' : playback?.pilotLocked ? 'Stereo' : 'Mono · waiting for stereo pilot';
    $('rxStatus').textContent = (active ? 'Signal +' + level + ' dB · ' + audioModeLabel : 'Signal —') +
      ' · Squelch ' + (squelchOpen ? 'open' : 'closed') +
      (active && !squelchOpen && threshold ? ' · Set SQ to 0 to monitor' : '') +
      (active && missingFrames ? ' · Missing frames ' + missingFrames : '');
  }
  function resetReceiver(keepNoise = false) {
    expectedIndex = 0;
    missingFrames = 0;
    rxUsed = 0;
    signalDb = null;
    noiseWarmup = {seen: 0, levels: []};
    noiseMeasurement = null;
    if (!keepNoise) {
      noiseDb = serial?.noiseReferences?.get(noiseReferenceKey()) ?? null;
    }
    lastSignalAt = 0;
    carrierSeenAt = 0;
    carrierSamples = 0;
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
    if (gap) carrierSamples = 0;
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
    if (serial) serial.recoveries = 0;
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
    if (data.type === 'rx-status') {
      playback = {...data, observedAt: performance.now()};
      if (noiseDb === null && freshStereoPilot() && noiseWarmup.seen) noiseWarmup = {seen: 0, levels: []};
      updateSquelch();
      return;
    }
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
