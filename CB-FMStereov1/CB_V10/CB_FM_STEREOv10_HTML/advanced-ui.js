'use strict';

  $('txPowerButton').onclick = () => {
    const panel = $('txPowerPanel');
    panel.hidden = !panel.hidden;
    $('txPowerButton').setAttribute('aria-expanded', String(!panel.hidden));
    if (!panel.hidden && !$('apwr').disabled) $('apwr').focus();
  };
  $('apwr').oninput = () => { if (!$('apwr').disabled) refreshControls(); };
  $('apwrRange').oninput = () => {
    if ($('apwrRange').disabled) return;
    $('apwr').value = $('apwrRange').value;
    refreshControls();
  };
  $('serial').onclick = () => state === 'off' ? void connect() : void disconnect();
  $('enableMic').onclick = () => {
    void prepareMic().catch(error => micMessage('Microphone unavailable · ' + error.message));
  };
  $('mic').onchange = () => {
    void prepareMic().catch(error => micMessage('Microphone unavailable · ' + error.message));
  };
  $('speaker').onchange = () => {
    void chooseSpeaker().catch(error => {
      $('speaker').value = '';
      micMessage('Audio output · ' + error.message);
    });
  };
  $('preset').onchange = () => {
    if ($('preset').value !== 'manual') {
      $('frequency').value = $('preset').value;
      if (state === 'rx') tuneRadio();
    } else $('frequency').focus();
  };
  $('frequency').oninput = () => { $('preset').value = 'manual'; };
  $('frequency').onkeydown = event => {
    if (event.key === 'Enter' && state === 'rx') { event.preventDefault(); tuneRadio(); }
  };
  $('profile').onchange = () => { if (state === 'rx') tuneRadio(); };
  $('tune').onclick = () => { if (state === 'rx') tuneRadio(); };
  $('squelch').oninput = () => { updateSquelch(); showRxStatus(); };
  $('micGain').oninput = () => {
    const db = Number($('micGain').value);
    const label = (db > 0 ? '+' : '') + db + ' dB';
    $('micGainValue').value = label;
    $('micGain').setAttribute('aria-valuetext', label);
    if (audio) audio.micGain.gain.setTargetAtTime(micGainFactor(), audio.context.currentTime, .01);
  };
  $('rxVolume').oninput = () => {
    $('rxVolumeValue').value = $('rxVolume').value + ' %';
    if (audio) audio.rxVolume.gain.setTargetAtTime(rxVolumeFactor(), audio.context.currentTime, .01);
  };
  $('ptt').onpointerdown = event => {
    if (event.button !== 0 || state !== 'rx' || pttHold) return;
    event.preventDefault();
    holdingPointer = event.pointerId;
    $('ptt').setPointerCapture(event.pointerId);
    void pressPtt('pointer');
  };
  function endPointer(event) {
    if (holdingPointer !== event.pointerId) return;
    holdingPointer = null;
    if (pttHold === 'pointer') releasePtt();
  }
  $('ptt').onpointerup = endPointer;
  $('ptt').onpointercancel = endPointer;
  $('ptt').onlostpointercapture = endPointer;
  document.addEventListener('keydown', event => {
    if (event.key === 'Escape' && (pttHold || dtmfTone)) {
      cancelDtmf();
      if (pttHold) releasePtt();
      return;
    }
    if (event.code !== 'Space' || (event.target !== $('ptt') && event.target?.closest?.('input, select, textarea, button, [contenteditable]'))) return;
    if (state === 'rx' || pttHold === 'keyboard') {
      event.preventDefault();
      if (!event.repeat) void pressPtt('keyboard');
    }
  });
  document.addEventListener('keyup', event => {
    if (event.code === 'Space' && pttHold === 'keyboard') { event.preventDefault(); releasePtt(); }
  });
  window.addEventListener('blur', () => {
    cancelDtmf();
    if (pttHold && pttHold !== 'continuous') releasePtt();
  });
  document.addEventListener('visibilitychange', () => {
    if (document.hidden) {
      cancelDtmf();
      if (pttHold && pttHold !== 'continuous') releasePtt();
    }
  });
  window.addEventListener('pagehide', () => {
    cancelDtmf();
    if (serial) void disconnect();
    else stopMic();
  });
  navigator.serial?.addEventListener('disconnect', event => {
    if (serial && (event.target === serial.port || event.port === serial.port)) fail('The serial cable was unplugged.');
  });
  navigator.mediaDevices?.addEventListener('devicechange', () => { void updateDevices().catch(() => {}); });
  void updateDevices().catch(() => {});
  setInterval(() => { updateSquelch(); showRxStatus(); }, 100);
  refreshControls();

  window.cbFrontPanel = {
    snapshot: () => ({state, pttHold, txPrimed, apwr: selectedApwr(), txApwr, signalDb, noiseDb, squelchOpen, playback, activeTune,
      ...getVuMeters(), rxAudioActive: receiveAudioActive(), dtmfKey: activeDtmfKey(),
      rxStatus: $('rxStatus').textContent, status: $('status').textContent, statusError: $('status').dataset.error === 'true'}),
    press: () => pressPtt('front'),
    pressContinuous: () => pressPtt('continuous'),
    release: () => { if (pttHold) releasePtt(); },
    dtmfPress: startDtmf,
    dtmfRelease: stopDtmf,
    dtmfCancel: cancelDtmf,
  };
