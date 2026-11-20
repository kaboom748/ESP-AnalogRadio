'use strict';

  const asciiLimit = 120;
  const rxReply = /^OK RX (\d+) (\d+) (HQ|ROBUST|MONO)$/;
  const statusReply = /^OK STATUS CB_FM_STEREO (\d+)$/;
  const legacyBands = [[2401000000, 2482500000]];
  const selectableBands = [...legacyBands, [5180000000, 5885000000]];

  class SerialParser {
    constructor(frame, line, ready, transition = () => '', matchRx = line => rxReply.test(line), receiving = () => false) {
      this.frame = frame;
      this.line = line;
      this.ready = ready;
      this.transition = transition;
      this.matchRx = matchRx;
      this.receiving = receiving;
      this.reset();
    }
    reset() {
      this.rest = new Uint8Array(0);
      this.zeroTail = 0;
    }
    startsWith(bytes, offset, prefix) {
      return offset + prefix.length <= bytes.length && prefix.every((value, i) => value === bytes[offset + i]);
    }
    textAt(bytes, offset) {
      if (![79, 69].includes(bytes[offset])) return null;
      const prefix = bytes[offset] === 79 ? startBytes[1] : startBytes[2];
      const available = Math.min(prefix.length, bytes.length - offset);
      for (let i = 0; i < available; i++) if (bytes[offset + i] !== prefix[i]) return null;
      if (available < prefix.length) return {partial: true};
      let end = offset + prefix.length;
      while (end < bytes.length && end - offset <= asciiLimit && bytes[end] >= 32 && bytes[end] <= 126) end++;
      if (end - offset > asciiLimit) return null;
      if (end === bytes.length || (bytes[end] === 13 && end + 1 === bytes.length)) return {partial: true};
      if (bytes[end] !== 10 && !(bytes[end] === 13 && bytes[end + 1] === 10)) return null;
      return {line: decoder.decode(bytes.subarray(offset, end)), next: end + (bytes[end] === 13 ? 2 : 1)};
    }
    frameIndex(bytes, view, offset, aligned = true) {
      if (offset + headerSize > bytes.length || !this.startsWith(bytes, offset, startBytes[0])) return null;
      const index = view.getUint32(offset + 4, true);
      return !aligned || index % payloadSize === 0 ? index : null;
    }
    isRxSuccessor(index, next) {
      if (next === null) return false;
      const gap = (next - index) >>> 0;
      return gap >= payloadSize && gap <= 2 * profiles.HQ;
    }
    shortenedFrameEnd(bytes, view, offset, index) {
      const nominal = offset + frameSize;
      for (let start = offset + startBytes[0].length; start < nominal; start++) {
        if (bytes[start] !== startBytes[0][0]) continue;
        const nextIndex = this.frameIndex(bytes, view, start, false);
        if (nextIndex === null) continue;
        const expected = (nextIndex + payloadSize) >>> 0;
        if (this.isRxSuccessor(index, nextIndex) ||
            this.frameIndex(bytes, view, start + frameSize, false) === expected) return start;
      }
      return null;
    }
    boundary(bytes, view, offset, index) {
      const following = this.frameIndex(bytes, view, offset);
      const gap = (following - index) >>> 0;
      if (following !== null && (following === 0 || (gap > 0 && gap < 0x80000000))) return true;
      const text = this.textAt(bytes, offset);
      if (text?.line && /^(?:OK (?:RX |STOP$|STATUS CB_FM_STEREO |CAPABILITIES )|ERR [A-Z0-9_]+$)/.test(text.line)) return true;
      return this.startsWith(bytes, offset, startBytes[3]);
    }
    restartAfter(bytes, view, offset) {
      const index = this.frameIndex(bytes, view, offset);
      return index === 0 && this.frameIndex(bytes, view, offset + frameSize) === payloadSize;
    }
    zeroesBefore(bytes, offset) {
      let preceding = offset;
      while (preceding > 0 && bytes[preceding - 1] === 0 && offset - preceding < frameSize) preceding--;
      return Math.min(frameSize, offset - preceding + (preceding === 0 ? this.zeroTail : 0));
    }
    readyOffset(bytes, first) {
      for (let i = first; i + startBytes[3].length <= bytes.length; i++) {
        // All four V13 builds pad the transition with one frame of zeroes.
        // This fence prevents a sequence of four signed PCM bytes from keying the radio.
        if (this.startsWith(bytes, i, startBytes[3]) && this.zeroesBefore(bytes, i) === frameSize) return i;
      }
      return null;
    }
    recoveryOffset(bytes, view, frame, state, quiet) {
      const nominal = frame + frameSize;
      for (let start = frame + headerSize; start < Math.min(nominal, bytes.length); start++) {
        if (bytes[start] !== 79) continue;
        const first = this.textAt(bytes, start);
        if (!first?.line) continue;
        if (first.line === 'OK STOP' && ['sync', 'trace'].includes(state)) {
          const fence = this.textAt(bytes, first.next);
          if (!fence?.line || !statusReply.test(fence.line)) continue;
          const after = this.textAt(bytes, fence.next);
          if (after?.line && rxReply.test(after.line) && this.restartAfter(bytes, view, after.next)) return start;
          // In idle, accept the expected STOP + STATUS fence only at the end
          // of the drained transition, never from inside an intact frame.
          if (quiet && fence.next === bytes.length && fence.next !== nominal) return start;
        }
        if (state === 'probe' && statusReply.test(first.line)) {
          const after = this.textAt(bytes, first.next);
          if (after?.line && rxReply.test(after.line) && this.restartAfter(bytes, view, after.next)) return start;
        }
        if (['tuning', 'keying', 'returning', 'trace'].includes(state) && this.matchRx(first.line) &&
            this.restartAfter(bytes, view, first.next)) return start;
      }
      return ['keying', 'returning'].includes(state) ? this.readyOffset(bytes, frame + headerSize) : null;
    }
    feed(chunk, quiet = false) {
      let bytes = chunk;
      if (this.rest.length) {
        bytes = new Uint8Array(this.rest.length + chunk.length);
        bytes.set(this.rest);
        bytes.set(chunk, this.rest.length);
      }
      const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
      let offset = 0;
      while (offset < bytes.length) {
        const first = bytes[offset];
        const prefix = startBytes.find(item => item[0] === first);
        if (!prefix) { offset++; continue; }
        const available = Math.min(prefix.length, bytes.length - offset);
        let matches = true;
        for (let i = 1; i < available; i++) {
          if (bytes[offset + i] !== prefix[i]) { matches = false; break; }
        }
        if (!matches) { offset++; continue; }
        if (available < prefix.length) break;
        if (first === 77) {
          if (bytes.length - offset < headerSize) break;
          const receiving = this.receiving();
          const index = this.frameIndex(bytes, view, offset, !receiving);
          if (index === null) { offset++; continue; }
          const transition = this.transition();
          const nominal = offset + frameSize;
          if (transition && !this.boundary(bytes, view, nominal, index)) {
            const recover = this.recoveryOffset(bytes, view, offset, transition, quiet);
            if (recover !== null) { offset = recover; continue; }
            if (!quiet && bytes.length < nominal + 2 * asciiLimit + frameSize) break;
          }
          if (receiving && !this.isRxSuccessor(index, this.frameIndex(bytes, view, nominal, false))) {
            // Prefer a valid header at the nominal boundary. Only look inside
            // the payload when that boundary is missing, with enough bytes to
            // corroborate a shifted header even when this index is damaged.
            if (!quiet && bytes.length < nominal + frameSize + headerSize) break;
            const earlier = this.shortenedFrameEnd(bytes, view, offset, index);
            // The missing bytes cannot be placed safely within the payload.
            // The next index carries the gap to the audio and RDS decoders.
            if (earlier !== null) { offset = earlier; continue; }
          }
          if (bytes.length - offset < frameSize) break;
          this.frame(index, view.getUint16(offset + 8, true),
            bytes.subarray(offset + headerSize, offset + frameSize));
          offset += frameSize;
        } else if (first === readyByte) {
          if (['keying', 'returning'].includes(this.transition()) && this.zeroesBefore(bytes, offset) === frameSize) this.ready();
          offset += prefix.length;
        } else {
          const text = this.textAt(bytes, offset);
          if (text?.partial) break;
          if (!text) { offset++; continue; }
          this.line(text.line);
          offset = text.next;
        }
      }
      this.zeroTail = this.zeroesBefore(bytes, offset);
      this.rest = bytes.slice(offset);
    }
    drain() { if (this.rest.length) this.feed(new Uint8Array(0), true); }
  }
  function radioBands(kind) {
    return serial?.bands?.[kind] ?? selectableBands;
  }
  function inBands(hz, bands) {
    return Number.isSafeInteger(hz) && bands.some(([low, high]) => hz >= low && hz <= high);
  }
  function frequencyError(hz) {
    const bands = radioBands('rx');
    if (inBands(hz, bands)) return '';
    const ranges = bands.map(([low, high]) => `${low / 1e6}–${high / 1e6} MHz`).join(' or ');
    return 'Enter a frequency in the available range: ' + ranges + '.';
  }
  function canTransmitTune() {
    return activeTune !== null && inBands(activeTune.hz, radioBands('tx'));
  }
  function parseBands(raw) {
    if (!/^\d{10}-\d{10}(?:,\d{10}-\d{10})?$/.test(raw)) return null;
    const bands = raw.split(',').map(pair => pair.split('-').map(Number));
    return bands.every(([low, high]) => low <= high && selectableBands.some(([first, last]) => low >= first && high <= last)) ? bands : null;
  }
  function config() {
    const hz = Math.round(Number($('frequency').value.trim().replace(',', '.')) * 1e6);
    const fine = Number($('fine').value);
    const profile = $('profile').value;
    const invalid = frequencyError(hz);
    if (invalid) throw new Error(invalid);
    if (!Number.isInteger(fine) || Math.abs(fine) > 50000) {
      throw new Error('Enter a whole-number RX correction from −50,000 to +50,000 Hz.');
    }
    return {hz, fine, profile};
  }
  function send(bytes, own = serial) {
    if (!own?.writer) return Promise.reject(new Error('Serial port unavailable.'));
    const data = typeof bytes === 'string' ? encoder.encode(bytes) : bytes;
    const written = own.tail.then(() => own.writer.write(data));
    own.tail = written.catch(() => {});
    return written;
  }
  function sendOrFail(bytes) {
    const own = serial;
    void send(bytes, own).catch(error => {
      if (serial === own) fail('Serial write interrupted · ' + error.message);
    });
  }
  function tuneRadio() {
    let next;
    try { next = config(); }
    catch (error) {
      if (state !== 'rx') linkIdle(error.message);
      else message(error.message, true);
      return;
    }
    pendingTune = next;
    audioMode('idle');
    setSquelch(false);
    setState('tuning', 'Tuning receiver…');
    expectReply('The firmware did not confirm receive mode.');
    sendOrFail('RX ' + next.hz + ' ' + next.profile + ' ' + next.fine + '\n');
  }
  function linkIdle(text) {
    cancelTimer();
    cancelDtmf(false);
    enableMicTrack(false);
    clearTxQueue();
    pttHold = null;
    txApwr = null;
    returnAfterReady = false;
    audioMode('idle');
    setSquelch(false);
    activeTune = pendingTune = null;
    $('actual').textContent = 'RX tuned to —';
    setState('linked', text, true);
  }
  function synchronize(text = 'Synchronizing radio…') {
    if (!serial || serial.closing) return;
    serial.stopSeen = false;
    audioMode('idle');
    setSquelch(false);
    setState('sync', text);
    expectReply('The radio did not confirm STOP and STATUS.', 1100);
    // STOP leaves the modem idle before STATUS, giving the parser a paired
    // control fence even if the final outgoing MPX frame was cut short.
    sendOrFail('\nSTOP\nSTATUS\n');
  }
  function protocolTimeout(text) {
    const own = serial;
    if (!own || own.closing) return;
    if (!own.protocolKnown || own.recoveries >= 2) {
      linkIdle(text + ' The port is open. Use Tune to retry.');
      return;
    }
    own.recoveries++;
    if (['keying', 'returning'].includes(state)) {
      // Every V13 TX arms for at most 1.2 seconds. A reply timeout occurs
      // after that, so we only then send ASCII again after ceasing PCM.
      cancelDtmf(false);
      enableMicTrack(false);
      pttHold = null;
      txApwr = null;
      txRequest++;
      clearTxQueue();
      returnAfterReady = false;
    }
    synchronize('Serial reply interrupted. Resynchronizing the radio…');
  }
  function finishCapabilities(rx = legacyBands, tx = legacyBands) {
    if (!serial || state !== 'capabilities') return;
    serial.bands = {rx, tx};
    tuneRadio();
  }
  function probeRadio(own, startupDelay = 0) {
    setState('probe', 'Checking the CB V13 firmware at 1.5 Mbaud…');
    expectReply('No reply from the CB firmware. Check the port and the firmware version.');
    void (async () => {
      await delay(startupDelay);
      for (let attempt = 0; attempt < 3 && serial === own && state === 'probe'; attempt++) {
        await send('STATUS\n', own);
        await delay(350);
      }
    })().catch(error => { if (serial === own) fail('Serial write interrupted · ' + error.message); });
  }
  function retryLink() {
    if (!serial || state !== 'linked') return;
    serial.recoveries = 0;
    if (serial.protocolKnown) synchronize();
    else probeRadio(serial);
  }
  function validRxReply(line) {
    const received = rxReply.exec(line);
    if (!received) return null;
    const lo = Number(received[1]), rate = Number(received[2]), profile = received[3];
    const next = pendingTune || activeTune;
    if (!next || profiles[profile] !== rate || profile !== next.profile || Math.abs(lo - next.hz - next.fine) > 4000) return null;
    return {lo, rate, next};
  }
  function radioLine(line) {
    if (line === 'ERR CPU160' && state === 'probe') {
      linkIdle('This ESP8266 firmware requires a 160 MHz CPU clock. The serial port is open.');
      return;
    }
    const firmware = statusReply.exec(line);
    if (firmware && state === 'probe') {
      if (firmware[1] !== '4') {
        linkIdle('The radio must run CB_FM_STEREO protocol 4. The serial port is open.');
      } else {
        serial.protocolKnown = true;
        synchronize();
      }
      return;
    }
    if (line === 'OK STOP' && state === 'sync') {
      serial.stopSeen = true;
      return;
    }
    if (line === 'OK STATUS CB_FM_STEREO 4' && state === 'sync' && serial.stopSeen) {
      if (serial.bands) tuneRadio();
      else {
        setState('capabilities', 'Checking the receiver bands…');
        expectReply('', 450, finishCapabilities);
        sendOrFail('CAPABILITIES?\n');
      }
      return;
    }
    if (state === 'capabilities') {
      if (line === 'ERR COMMAND') finishCapabilities();
      else {
        const entry = /^OK CAPABILITIES RX=(\S+) TX=(\S+)$/.exec(line);
        if (entry) {
          const [rx, tx] = entry.slice(1).map(parseBands);
          if (rx && tx) finishCapabilities(rx, tx);
        }
      }
      return;
    }
    const received = validRxReply(line);
    if (received && ['tuning', 'returning', 'tx', 'keying'].includes(state)) {
      const {lo, rate, next} = received;
      resumeReceiver({...next, rate}, lo, !pendingTune && Boolean(activeTune));
      return;
    }
    if (/^ERR /.test(line) && ['keying', 'tx', 'returning'].includes(state)) {
      finishTransmit('Transmission interrupted by the firmware · ' + line.slice(4), true);
    } else if (/^ERR /.test(line) && state === 'tuning') {
      linkIdle('The radio reported an error · ' + line.slice(4) + '. Tune to retry.');
    }
  }
  function radioReady() {
    if (state === 'returning') returnAfterReady = true;
    if (state !== 'keying' || !pttHold) return;
    cancelTimer();
    setState('tx', 'APWR ' + txApwr + ' applied. Preparing transmit audio…');
    pumpTx();
  }
  async function readSerial(own) {
    const parser = new SerialParser(radioFrame, radioLine, radioReady, () =>
      ['probe', 'sync', 'capabilities', 'tuning', 'keying', 'returning'].includes(state) ? state : '', validRxReply,
      () => ['rx', 'arming'].includes(state));
    let drainTimer;
    try {
      while (serial === own && !own.closing && own.port.readable) {
        const reader = own.port.readable.getReader();
        own.reader = reader;
        try {
          while (serial === own && !own.closing) {
            const {value, done} = await reader.read();
            if (done) return;
            if (value?.length) {
              clearTimeout(drainTimer);
              parser.feed(value);
              if (parser.rest.length) drainTimer = setTimeout(() => {
                if (serial === own && !own.closing) parser.drain();
              }, 35);
            }
          }
        } catch (error) {
          if (!own.port.readable) throw error;
          clearTimeout(drainTimer);
          parser.reset();
        } finally {
          reader.releaseLock();
          if (own.reader === reader) own.reader = null;
        }
      }
    } catch (error) {
      if (serial === own && !own.closing) fail('Serial connection interrupted · ' + error.message);
    } finally {
      clearTimeout(drainTimer);
      if (serial === own && !own.closing) fail('The serial port disconnected.');
    }
  }
  async function connect() {
    if (!navigator.serial) {
      message('Web Serial is unavailable. Open this file in Chrome or Edge, or serve it on localhost.', true);
      return;
    }
    try { config(); }
    catch (error) { message(error.message, true); return; }
    setState('connecting', 'Choose your radio’s serial port.');
    let chosen = null;
    try {
      const choice = navigator.serial.requestPort();
      const ready = setupAudio();
      const result = await Promise.all([choice, ready]);
      chosen = result[0];
      try { await chooseSpeaker(); }
      catch (error) { micMessage('Audio output · ' + error.message); }
      await chosen.open({baudRate: 1500000, dataBits: 8, stopBits: 1, parity: 'none',
        flowControl: 'none', bufferSize: 65536});
      const own = {port: chosen, writer: chosen.writable.getWriter(), reader: null,
        tail: Promise.resolve(), closing: false, readTask: null, protocolKnown: false,
        recoveries: 0, stopSeen: false, bands: null};
      serial = own;
      own.readTask = readSerial(own);
      probeRadio(own, 420);
    } catch (error) {
      if (serial) fail('Could not connect to the serial port · ' + error.message);
      else {
        if (chosen) await chosen.close().catch(() => {});
        setState('off', error.name === 'NotFoundError' ? 'No serial port selected.' : error.message,
          error.name !== 'NotFoundError');
      }
    }
  }
  async function disconnect(text = 'Serial port closed.', error = false) {
    const own = serial;
    if (state === 'closing') return;
    const lastState = state;
    txRequest++;
    txApwr = null;
    cancelDtmf(false);
    cancelTimer();
    pttHold = null;
    holdingPointer = null;
    audioMode('idle');
    setSquelch(false);
    clearTxQueue();
    stopMic();
    micMessage('Prepare the microphone before your first call.');
    txWriting = false;
    returnAfterReady = false;
    activeTune = null;
    pendingTune = null;
    returnMessage = '';
    returnError = false;
    $('actual').textContent = 'RX tuned to —';
    setState('closing', text, error);
    if (own) {
      own.closing = true;
      try {
        if (['rx', 'arming', 'sync', 'tuning', 'capabilities', 'linked'].includes(lastState) && own.protocolKnown) {
          await Promise.race([send('\nSTOP\n', own), delay(180)]);
        }
      } catch (_) {}
      serial = null;
      try { await own.reader?.cancel(); } catch (_) {}
      try { await own.readTask; } catch (_) {}
      try { await Promise.race([own.writer.abort(), delay(180)]); } catch (_) {}
      try { own.writer.releaseLock(); } catch (_) {}
      try { await own.port.close(); } catch (_) {}
    }
    setState('off', text, error);
    showRxStatus();
  }
  function fail(text) { if (state !== 'closing') void disconnect(text, true); }

