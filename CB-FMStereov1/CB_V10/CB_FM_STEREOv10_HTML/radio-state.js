'use strict';

  const $ = id => document.getElementById(id);
  const encoder = new TextEncoder();
  const decoder = new TextDecoder();
  const delay = ms => new Promise(resolve => setTimeout(resolve, ms));
  const clamp = (value, lo, hi) => Math.min(hi, Math.max(lo, value));
  const frequencyFormat = new Intl.NumberFormat('en-CA', {minimumFractionDigits: 3, maximumFractionDigits: 6});
  const profiles = {HQ: 120000, ROBUST: 96000, MONO: 48000};
  const payloadSize = 128;
  const headerSize = 10;
  const frameSize = headerSize + payloadSize;
  const readyByte = 0xef;
  const txChunkBytes = 600;
  const txWriteBytes = 4 * txChunkBytes;
  const txPrerollBytes = 9600; // 80 ms of MPX to absorb initial USB latency.
  const txQueueLimit = 28800; // 240 ms maximum en attente dans le navigateur.
  const startBytes = [
    Uint8Array.of(77, 80, 88, 51), // MPX3
    Uint8Array.of(79, 75, 32),     // OK
    Uint8Array.of(69, 82, 82, 32), // ERR
    Uint8Array.of(readyByte, readyByte, readyByte, readyByte),
  ];


  let state = 'off';
  let serial = null;
  let timer = 0;
  let activeTune = null;
  let pendingTune = null;
  let returnMessage = '';
  let returnError = false;
  let pttHold = null;
  let txApwr = null;
  let txRequest = 0;
  let holdingPointer = null;
  let epoch = 0;
  let audio = null;
  let audioPending = null;
  let mic = null;
  let micSource = null;
  let micPending = null;
  let micChoice = '';
  let micGeneration = 0;
  let signalDb = null;
  let noiseDb = null;
  let noiseSamples = 0;
  let lastSignalAt = 0;
  let squelchOpen = false;
  let carrierSeenAt = 0;
  let expectedIndex = 0;
  let missingFrames = 0;
  let rxBuffer = new Uint8Array(4096);
  let rxUsed = 0;
  let playback = null;
  let txQueue = [];
  let queuedTxBytes = 0;
  let txWriting = false;
  let txPrimed = false;
  let returnAfterReady = false;
  const dtmfKeys = '123456789*0#';
  const dtmfRows = [697, 770, 852, 941];
  const dtmfColumns = [1209, 1336, 1477];
  const dtmfQueue = [];
  let dtmfHeld = null;
  let dtmfTone = null;
  let dtmfClock = 0;
  let nextToneAt = 0;
  let dtmfTailUntil = 0;

  const stateLabels = {
    off: 'Disconnected', connecting: 'Connecting', probe: 'Checking', sync: 'Checking', tuning: 'Tuning',
    rx: 'Listening', arming: 'Microphone', keying: 'Switching to transmit',
    tx: 'Transmitting', returning: 'Returning to receive', closing: 'Disconnecting',
  };
  function message(text, error = false) {
    $('status').textContent = text;
    $('status').dataset.error = String(error);
  }
  function micMessage(text) { $('audioStatus').textContent = text; }
  function readApwr() {
    const raw = $('apwr').value.trim();
    if (!/^\d{1,3}$/.test(raw) || Number(raw) > 191) {
      throw new Error('TX POWER: enter an integer from 0 to 191.');
    }
    return Number(raw);
  }
  function selectedApwr() {
    try { return readApwr(); }
    catch (_) { return null; }
  }
  function refreshTxPower() {
    const value = selectedApwr();
    const valid = value !== null;
    const canEdit = state === 'off' || state === 'rx';
    $('apwr').disabled = $('apwrRange').disabled = !canEdit;
    $('apwr').setAttribute('aria-invalid', String(!valid));
    $('txPowerError').hidden = valid;
    $('txPowerValue').value = valid ? 'APWR ' + value : 'APWR —';
    if (valid) {
      $('apwrRange').value = String(value);
      $('apwrRange').setAttribute('aria-valuetext', 'APWR ' + value + (value === 0 ? ', maximum TX power' : value === 191 ? ', minimum TX power' : ''));
    }
    let text = valid ? 'APWR ' + value + ' will be applied before each transmission.' : 'Invalid APWR value: transmission unavailable.';
    if (['arming', 'keying'].includes(state)) text = 'Preparing TX · APWR ' + txApwr + ' · setting locked.';
    if (state === 'tx') text = 'TX · APWR ' + txApwr + ' applied · setting locked.';
    if (state === 'returning') text = 'Returning to RX · APWR ' + txApwr + ' · setting locked.';
    $('txPowerState').textContent = text;
  }
  function refreshControls() {
    const canTune = state === 'off' || state === 'rx';
    const pttStates = ['rx', 'arming', 'keying', 'tx'];
    $('state').textContent = state === 'tx' && !txPrimed ? 'Preparing TX' : stateLabels[state];
    $('state').dataset.state = state === 'arming' ? 'rx' : state;
    $('serial').textContent = state === 'off' ? 'Connect serial port' : 'Disconnect';
    $('serial').disabled = state === 'connecting' || state === 'closing';
    for (const id of ['preset', 'frequency', 'fine', 'profile']) $(id).disabled = !canTune;
    $('tune').disabled = state !== 'rx';
    $('ptt').disabled = !pttStates.includes(state) || (state === 'rx' && selectedApwr() === null);
    $('ptt').dataset.held = String(Boolean(pttHold));
    $('ptt').textContent = state === 'tx' && txPrimed ? 'TRANSMITTING · RELEASE TO LISTEN' : 'PUSH TO TALK';
    $('mic').disabled = Boolean(pttHold || micPending);
    $('enableMic').disabled = Boolean(pttHold || micPending);
    refreshTxPower();
  }
  function setState(next, text, error = false) {
    state = next;
    refreshControls();
    if (text) message(text, error);
  }
  function cancelTimer() { clearTimeout(timer); timer = 0; }
  function expectReply(text, ms = 2600) {
    cancelTimer();
    timer = setTimeout(() => fail(text), ms);
  }

