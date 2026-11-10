'use strict';

  function activeDtmfKey() { return dtmfHeld?.key ?? null; }

  function startDtmf(key) {
    if (typeof key !== 'string' || key.length !== 1 || !dtmfKeys.includes(key) ||
        dtmfHeld || dtmfQueue.length >= 16) return false;
    if (serial && state === 'rx' && selectedApwr() === null) {
      message('TX POWER: enter an integer from 0 to 191.', true);
      return false;
    }
    const radio = serial && ['rx', 'arming', 'keying', 'tx', 'returning'].includes(state) ? serial : null;
    const tone = {key, radio, released: false, rfDone: !radio};
    dtmfHeld = tone;
    dtmfQueue.push(tone);
    if (!dtmfClock) dtmfClock = setInterval(updateDtmf, 15);
    updateDtmf();
    return true;
  }

  function stopDtmf(key) {
    if (dtmfHeld?.key !== key) return;
    dtmfHeld.released = true;
    dtmfHeld = null;
    updateDtmf();
  }

  function dtmfGain(node, level) {
    node.gain.setTargetAtTime(level, node.context.currentTime, .003);
  }

  async function beginDtmfTone(tone) {
    try {
      if (!audio) await chooseSpeaker();
      else await setupAudio();
      if (dtmfTone !== tone) return;
      const {context, rxVolume, micGain} = audio;
      tone.local = context.createGain();
      tone.rf = context.createGain();
      tone.local.gain.value = tone.rf.gain.value = 0;
      tone.local.connect(rxVolume);
      tone.rf.connect(micGain);
      const index = dtmfKeys.indexOf(tone.key);
      tone.oscillators = [dtmfRows[Math.floor(index / 3)], dtmfColumns[index % 3]].map(frequency => {
        const oscillator = context.createOscillator();
        oscillator.frequency.value = frequency;
        oscillator.connect(tone.local);
        oscillator.connect(tone.rf);
        oscillator.start();
        return oscillator;
      });
      tone.localAt = performance.now();
      dtmfGain(tone.local, .13);
      updateDtmf();
    } catch (error) {
      if (dtmfTone !== tone) return;
      cancelDtmf();
      micMessage('DTMF audio unavailable · ' + error.message);
    }
  }

  function stopDtmfRf(tone) {
    if (tone.rfEnabled) {
      dtmfGain(tone.rf, 0);
      dtmfGain(audio.micInput, 1);
    }
    tone.rfEnabled = false;
    tone.rfDone = true;
  }

  function disposeDtmfTone(tone, immediate = false) {
    if (!tone?.oscillators) return;
    const remove = () => {
      tone.local.disconnect();
      tone.rf.disconnect();
      tone.oscillators.forEach(oscillator => oscillator.disconnect());
    };
    if (immediate) remove();
    else tone.oscillators[0].onended = remove;
    const when = tone.local.context.currentTime + (immediate ? 0 : .02);
    tone.oscillators.forEach(oscillator => oscillator.stop(when));
  }

  function cancelDtmf(release = true) {
    if (dtmfTone?.oscillators) {
      stopDtmfRf(dtmfTone);
      disposeDtmfTone(dtmfTone, true);
    }
    dtmfHeld = dtmfTone = null;
    dtmfQueue.length = 0;
    clearInterval(dtmfClock);
    dtmfClock = nextToneAt = dtmfTailUntil = 0;
    if (release && pttHold === 'dtmf') releasePtt();
  }

  function updateDtmf() {
    const now = performance.now();
    if (!dtmfTone) {
      if (dtmfQueue.length) {
        if (now >= nextToneAt) void beginDtmfTone(dtmfTone = dtmfQueue.shift());
      } else {
        if (pttHold === 'dtmf' && now < dtmfTailUntil) return;
        cancelDtmf();
      }
      return;
    }
    const tone = dtmfTone;
    if (!tone.oscillators) return;
    if (!tone.rfDone) {
      if (serial !== tone.radio || (tone.rfEnabled && state !== 'keying' && state !== 'tx')) {
        stopDtmfRf(tone);
      } else {
        if (state === 'rx' && !pttHold) void pressPtt('dtmf');
        if ((state === 'keying' || state === 'tx') && pttHold) {
          if (!tone.rfEnabled) {
            tone.rfEnabled = true;
            dtmfGain(audio.micInput, 0);
            dtmfGain(tone.rf, .16);
          }
          if (state === 'tx' && txPrimed) tone.rfAt ??= now;
        }
      }
    }
    if (!tone.released) return;
    if (!tone.localDone && now - tone.localAt >= 90) {
      dtmfGain(tone.local, 0);
      tone.localDone = true;
    }
    if (tone.rfEnabled && tone.rfAt !== undefined && now - tone.rfAt >= 90) {
      stopDtmfRf(tone);
      // Allow the last generated samples to pass through the transmit preroll.
      if (pttHold === 'dtmf') dtmfTailUntil = now + 140;
    }
    if (tone.localDone && tone.rfDone) {
      disposeDtmfTone(tone);
      dtmfTone = null;
      nextToneAt = now + 50;
    }
  }
