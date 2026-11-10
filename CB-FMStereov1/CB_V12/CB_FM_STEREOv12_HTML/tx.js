'use strict';

  function clearTxQueue() { txQueue = []; queuedTxBytes = 0; txPrimed = false; }
  function pumpTx() {
    if (state !== 'tx' || !pttHold || txWriting) return;
    if (!txPrimed) {
      if (queuedTxBytes < txPrerollBytes) return;
      txPrimed = true;
      setState('tx', 'Transmitting stereo FM · APWR ' + txApwr + '.');
    } else if (queuedTxBytes < txWriteBytes) {
      return;
    }
    const samples = new Uint8Array(txWriteBytes);
    for (let offset = 0; offset < txWriteBytes; offset += txChunkBytes) {
      samples.set(txQueue.shift(), offset);
    }
    queuedTxBytes -= txWriteBytes;
    const own = serial;
    txWriting = true;
    send(samples, own).catch(error => {
      if (serial === own) fail('Audio transmission failed · ' + error.message);
    }).finally(() => {
      if (serial === own) { txWriting = false; pumpTx(); }
    });
  }
  async function pressPtt(hold) {
    if (state !== 'rx' || pttHold) return;
    let requestedApwr;
    try { requestedApwr = readApwr(); }
    catch (error) { message(error.message, true); refreshControls(); return; }
    pttHold = hold;
    txApwr = requestedApwr;
    const request = ++txRequest;
    const own = serial;
    setState('arming');
    try {
      if (hold !== 'dtmf') await prepareMic();
      await window.cbRds.prepareTx(); // Ensure RDS is ready before any PTT/DTMF transmission.
      if (serial !== own || pttHold !== hold || state !== 'arming' || request !== txRequest) return;
      setSquelch(false);
      clearTxQueue();
      enableMicTrack(hold !== 'dtmf');
      audioMode('tx');
      setState('keying', 'Applying APWR ' + requestedApwr + ' before transmit…');
      expectReply('The radio did not apply TX POWER and signal that it was ready to transmit.');
      // A single command binds the validated setting to the start of this TX session.
      sendOrFail('TX ' + requestedApwr + '\n');
    } catch (error) {
      if (serial !== own || pttHold !== hold || request !== txRequest) return;
      pttHold = null;
      txApwr = null;
      setState('rx', 'Microphone unavailable · ' + error.message, true);
    }
  }
  function finishTransmit(text = '', error = false) {
    if (!['keying', 'tx', 'returning'].includes(state)) return;
    if (error) cancelDtmf(false);
    if (state === 'tx') returnAfterReady = true;
    pttHold = null;
    clearTxQueue();
    enableMicTrack(false);
    audioMode('idle');
    returnMessage = text;
    returnError = error;
    setState('returning', text || 'Returning to receive…', error);
    expectReply('The radio did not confirm the return to receive.');
  }
  function releasePtt() {
    pttHold = null;
    txRequest++;
    if (state === 'arming') { txApwr = null; setState('rx'); }
    else if (state === 'tx' || state === 'keying') finishTransmit();
  }

