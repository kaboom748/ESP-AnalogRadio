'use strict';

  async function updateDevices() {
    if (!navigator.mediaDevices?.enumerateDevices) return;
    const devices = await navigator.mediaDevices.enumerateDevices();
    for (const [id, kind, name] of [
      ['mic', 'audioinput', 'Microphone'],
      ['speaker', 'audiooutput', 'Speaker'],
    ]) {
      const select = $(id);
      const chosen = select.value;
      const choices = devices.filter(device => device.kind === kind && device.deviceId && device.deviceId !== 'default');
      const option = (title, value) => new Option(title, value);
      select.replaceChildren(option(id === 'mic' ? 'Default microphone' : 'Default output', ''));
      choices.forEach((device, index) => select.add(option(device.label || name + ' ' + (index + 1), device.deviceId)));
      if (choices.some(device => device.deviceId === chosen)) select.value = chosen;
    }
  }

  const micGainFactor = () => 10 ** (Number($('micGain').value) / 20);
  const rxVolumeFactor = () => Number($('rxVolume').value) / 100;

  function createStereoTap(context, source) {
    const splitter = context.createChannelSplitter(2);
    source.connect(splitter);
    return [0, 1].map(channel => {
      const analyser = context.createAnalyser();
      analyser.fftSize = 2048;
      splitter.connect(analyser, channel);
      return analyser;
    });
  }

  async function setupAudio() {
    if (audio) { await audio.context.resume(); return audio; }
    if (audioPending) return audioPending;
    if (!window.isSecureContext || !window.AudioContext || !window.AudioWorkletNode) {
      throw new Error('Open this file in Chrome or Edge, or serve it on localhost, to use audio.');
    }
    if (!AUDIO_WORKLET_SOURCE) throw new Error('The audio module is missing from this page.');
    const context = new AudioContext({latencyHint: 'interactive'});
    const resume = context.resume();
    audioPending = (async () => {
      // The same module must load from a local file or from localhost.
      const url = 'data:text/javascript;charset=utf-8,' + encodeURIComponent(AUDIO_WORKLET_SOURCE);
      try {
        await Promise.all([resume, context.audioWorklet.addModule(url)]);
        const node = new AudioWorkletNode(context, 'cb-radio', {
          numberOfInputs: 1, numberOfOutputs: 1, outputChannelCount: [2],
          channelCount: 2, channelCountMode: 'explicit', channelInterpretation: 'speakers',
        });
        node.port.onmessage = event => workletMessage(event.data);
        node.port.postMessage({type:'emphasis', us: window.cbAudioEmphasis?.value() ?? 75});
        const micInput = context.createGain();
        const micGain = context.createGain();
        micGain.channelCount = 2;
        micGain.channelCountMode = 'explicit';
        micGain.channelInterpretation = 'speakers';
        micInput.connect(micGain);
        micGain.gain.value = micGainFactor();
        micGain.connect(node);
        const txAnalysers = createStereoTap(context, micGain);
        const rxVolume = context.createGain();
        rxVolume.gain.value = rxVolumeFactor();
        node.connect(rxVolume);
        const rxAnalysers = createStereoTap(context, node);
        let element = null;
        if (typeof context.setSinkId === 'function' || typeof HTMLMediaElement.prototype.setSinkId !== 'function') {
          rxVolume.connect(context.destination);
        } else {
          const sink = context.createMediaStreamDestination();
          rxVolume.connect(sink);
          element = new Audio();
          element.srcObject = sink.stream;
        }
        audio = {context, node, element, micInput, micGain, rxVolume, txAnalysers, rxAnalysers};
        if (mic) attachMic();
        return audio;
      } catch (error) {
        await context.close().catch(() => {});
        throw error;
      }
    })().finally(() => { audioPending = null; });
    return audioPending;
  }
  async function chooseSpeaker() {
    const target = $('speaker').value;
    const {context, element} = await setupAudio();
    if (typeof context.setSinkId === 'function') {
      if (context.sinkId !== target) await context.setSinkId(target);
    } else if (element) {
      if (element.sinkId !== target) await element.setSinkId(target);
      await element.play();
    } else if (target) {
      throw new Error('This browser cannot select an audio output.');
    }
  }
  function attachMic() {
    if (micSource) { micSource.disconnect(); micSource = null; }
    if (mic && audio) {
      micSource = audio.context.createMediaStreamSource(mic);
      micSource.connect(audio.micInput);
    }
  }
  function enableMicTrack(enabled) {
    for (const track of mic?.getAudioTracks() || []) track.enabled = enabled;
  }
  function stopMic(invalidate = true) {
    if (invalidate) micGeneration++;
    if (micSource) { micSource.disconnect(); micSource = null; }
    for (const track of mic?.getTracks() || []) track.stop();
    mic = null;
    micChoice = '';
  }
  async function prepareMic() {
    const choice = $('mic').value;
    if (mic?.active && micChoice === choice) return;
    if (micPending) return micPending;
    if (!navigator.mediaDevices?.getUserMedia) throw new Error('The microphone is unavailable from this page.');
    const generation = ++micGeneration;
    const acquire = navigator.mediaDevices.getUserMedia({audio: {
      ...(choice ? {deviceId: {exact: choice}} : {}),
      channelCount: {ideal: 2}, echoCancellation: false, noiseSuppression: false, autoGainControl: false,
    }});
    micPending = (async () => {
      const next = await acquire;
      if (generation !== micGeneration) {
        next.getTracks().forEach(track => track.stop());
        throw new Error('The radio was disconnected.');
      }
      stopMic(false);
      mic = next;
      micChoice = choice;
      enableMicTrack(false);
      attachMic();
      const track = next.getAudioTracks()[0];
      track.addEventListener('ended', () => {
        if (mic !== next) return;
        releasePtt();
        stopMic();
        micMessage('Microphone unplugged.');
      });
      const channels = track.getSettings().channelCount;
      micMessage((track.label || 'Microphone') + (channels === 1 ? ' · centered mono' : ' · ready'));
      await updateDevices().catch(() => {});
    })().finally(() => { micPending = null; refreshControls(); });
    refreshControls();
    return micPending;
  }
  function audioMode(mode, profile = 'HQ') {
    epoch++;
    playback = null;
    audio?.node.port.postMessage({type: 'mode', mode, profile, epoch});
    window.cbRds?.modeChange(mode, profile, epoch, audio?.node);
  }
  const idleVu = {meterL: -60, meterR: -60, peakL: false, peakR: false};
  let vuSamples = null;
  let vuSource = null;
  let vuLevels = idleVu;
  let leftPeakUntil = 0;
  let rightPeakUntil = 0;
  let receiveAudioUntil = 0;

  function receivingAudio() {
    return (state === 'rx' || state === 'arming') && squelchOpen && audio?.context.state === 'running';
  }

  function activeVuSource() {
    if (audio?.context.state !== 'running') return null;
    if (state === 'tx' && txPrimed) return audio.txAnalysers;
    return receivingAudio() ? audio.rxAnalysers : null;
  }

  function measureChannel(analyser) {
    if (vuSamples?.length !== analyser.fftSize) vuSamples = new Float32Array(analyser.fftSize);
    analyser.getFloatTimeDomainData(vuSamples);
    let sum = 0;
    let energy = 0;
    let low = Infinity;
    let high = -Infinity;
    for (const sample of vuSamples) {
      sum += sample;
      energy += sample * sample;
      low = Math.min(low, sample);
      high = Math.max(high, sample);
    }
    const count = vuSamples.length;
    const mean = sum / count;
    return {power: Math.max(0, energy / count - mean ** 2), peak: Math.max(high - mean, mean - low)};
  }

  function meterLevel(power, previous) {
    // Fast attack and a 4 dB decay per 40 ms update.
    return Math.min(0, Math.max(-60, 10 * Math.log10(power), previous - 4));
  }

  function sampleAudioMeters() {
    const source = activeVuSource();
    if (source !== vuSource) {
      vuSource = source;
      vuLevels = idleVu;
      leftPeakUntil = rightPeakUntil = receiveAudioUntil = 0;
    }
    if (!source) return;

    const left = measureChannel(source[0]);
    const right = measureChannel(source[1]);
    const now = performance.now();
    if (left.peak >= .95) leftPeakUntil = now + 240;
    if (right.peak >= .95) rightPeakUntil = now + 240;
    vuLevels = {
      meterL: meterLevel(left.power, vuLevels.meterL),
      meterR: meterLevel(right.power, vuLevels.meterR),
      peakL: now < leftPeakUntil,
      peakR: now < rightPeakUntil,
    };
    if (source === audio.rxAnalysers && Math.max(left.power, right.power) > .004 ** 2) {
      receiveAudioUntil = now + 180;
    }
  }

  function getVuMeters() {
    return activeVuSource() === vuSource ? vuLevels : idleVu;
  }

  function receiveAudioActive() {
    return receivingAudio() && performance.now() < receiveAudioUntil;
  }

  setInterval(sampleAudioMeters, 40);

