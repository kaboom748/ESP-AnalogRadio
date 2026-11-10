'use strict';

  class SerialParser {
    constructor(frame, line, ready) {
      this.rest = new Uint8Array(0);
      this.frame = frame;
      this.line = line;
      this.ready = ready;
    }
    feed(chunk) {
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
          if (bytes.length - offset < frameSize) break;
          this.frame(view.getUint32(offset + 4, true), view.getUint16(offset + 8, true),
            bytes.subarray(offset + headerSize, offset + frameSize));
          offset += frameSize;
        } else if (first === readyByte) {
          this.ready();
          offset += prefix.length;
        } else {
          let end = offset + prefix.length;
          while (end < bytes.length && bytes[end] >= 32 && bytes[end] <= 126 && end - offset <= 120) end++;
          if (end === bytes.length) break;
          if (bytes[end] === 10 || (bytes[end] === 13 && bytes[end + 1] === 10)) {
            this.line(decoder.decode(bytes.subarray(offset, end)));
            offset = end + (bytes[end] === 13 ? 2 : 1);
          } else if (bytes[end] === 13 && end + 1 === bytes.length) {
            break;
          } else {
            offset++;
          }
        }
      }
      this.rest = bytes.slice(offset);
    }
  }
  function config() {
    const hz = Math.round(Number($('frequency').value.trim().replace(',', '.')) * 1e6);
    const fine = Number($('fine').value);
    const profile = $('profile').value;
    if (!Number.isInteger(hz) || hz < 2401000000 || hz > 2482500000) {
      throw new Error('Enter a frequency between 2,401 and 2,482.5 MHz.');
    }
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
      if (state === 'probe') fail(error.message);
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
  function radioLine(line) {
    const firmware = /^OK STATUS CB_FM_STEREO (\d+)$/.exec(line);
    if (firmware && state === 'probe' && firmware[1] !== '4') {
      fail('TX POWER requires the matching CB_FM_STEREO firmware (protocol 4). Upload the supplied sketch.');
      return;
    }
    if (line === 'OK STATUS CB_FM_STEREO 4' && state === 'probe') {
      setState('sync', 'Synchronizing radio…');
      expectReply('The firmware did not respond during startup.');
      sendOrFail('STOP\n');
      return;
    }
    if (line === 'OK STOP' && state === 'sync') {
      tuneRadio();
      return;
    }
    const received = /^OK RX (\d+) (\d+) (HQ|ROBUST|MONO)$/.exec(line);
    if (received && ['tuning', 'returning', 'tx', 'keying'].includes(state)) {
      const [lo, rate, profile] = [Number(received[1]), Number(received[2]), received[3]];
      const next = pendingTune || activeTune;
      if (!next || profiles[profile] !== rate || profile !== next.profile ||
          Math.abs(lo - next.hz - next.fine) > 4000) return;
      resumeReceiver({...next, rate}, lo, !pendingTune && Boolean(activeTune));
      return;
    }
    if (/^ERR /.test(line) && ['keying', 'tx', 'returning'].includes(state)) {
      finishTransmit('Transmission interrupted by the firmware · ' + line.slice(4), true);
    } else if (/^ERR /.test(line) && ['sync', 'tuning'].includes(state)) {
      fail('The radio reported an error · ' + line.slice(4));
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
    const parser = new SerialParser(radioFrame, radioLine, radioReady);
    try {
      while (serial === own && !own.closing && own.port.readable) {
        const reader = own.port.readable.getReader();
        own.reader = reader;
        try {
          while (serial === own && !own.closing) {
            const {value, done} = await reader.read();
            if (done) return;
            if (value?.length) parser.feed(value);
          }
        } catch (error) {
          if (!own.port.readable) throw error;
          parser.rest = new Uint8Array(0);
        } finally {
          reader.releaseLock();
          if (own.reader === reader) own.reader = null;
        }
      }
    } catch (error) {
      if (serial === own && !own.closing) fail('Serial connection interrupted · ' + error.message);
    } finally {
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
      await chosen.open({baudRate: 2000000, dataBits: 8, stopBits: 1, parity: 'none',
        flowControl: 'none', bufferSize: 65536});
      const own = {port: chosen, writer: chosen.writable.getWriter(), reader: null,
        tail: Promise.resolve(), closing: false, readTask: null};
      serial = own;
      own.readTask = readSerial(own);
      setState('probe', 'Checking the CB firmware at 2 Mbaud…');
      expectReply('No reply from the CB firmware. Check the port and the firmware version.', 2600);
      await delay(420);
      for (let attempt = 0; attempt < 3 && serial === own && state === 'probe'; attempt++) {
        await send('STATUS\n', own);
        await delay(350);
      }
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
        if (['rx', 'arming', 'sync', 'tuning'].includes(lastState)) {
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

