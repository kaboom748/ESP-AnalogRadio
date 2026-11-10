'use strict';

// Original CB V5 AudioWorklet retained; V6 adds RDS to the TX multiplex without modifying the RX audio path.
// Load with a data URL to support file:// too.
const AUDIO_WORKLET_SOURCE = String.raw`// AudioWorklet shared by FMStereoHQ transmitter and FMStereoRX decoder.
const AUDIO_RATE = 48000;
const MPX_RATE = 120000;
const MPX_CHUNK = 600;
const PILOT_LEVEL = .09;
const RDS_LEVEL = .06; // ±3 kHz on ±50 kHz TX modulation, matching the reference implementation.
const RDS_STEP = 1187.5 * 64 / MPX_RATE;
const PROFILES = {
  HQ: { rate: 120000, cutoff: 15150, taps: 121 },
  ROBUST: { rate: 96000, cutoff: 8300, taps: 101 },
  MONO: { rate: 48000, cutoff: 15000, taps: 63 },
};

function clamp(value, minimum, maximum) {
  return Math.max(minimum, Math.min(maximum, value));
}

function lowpass(length, cutoff, rate, fraction) {
  const taps = new Float64Array(length);
  const middle = (length - 1) / 2;
  let gain = 0;
  for (let i = 0; i < length; i++) {
    const x = i - middle - fraction;
    const sinc = x === 0 ? 2 * cutoff / rate : Math.sin(2 * Math.PI * cutoff * x / rate) / (Math.PI * x);
    const angle = 2 * Math.PI * i / (length - 1);
    taps[i] = sinc * (.42 - .5 * Math.cos(angle) + .08 * Math.cos(2 * angle));
    gain += taps[i];
  }
  for (let i = 0; i < length; i++) taps[i] /= gain;
  return taps;
}

class StereoEncoder {
  constructor(inputRate, send, rdsEvent) {
    this.rate = inputRate;
    this.send = send;
    this.rdsEvent = rdsEvent;
    this.rdsCarrier = Float32Array.from({length:40}, (_,i) => Math.cos(2 * Math.PI * 57000 * i / MPX_RATE));
    this.radius = 32;
    this.ringSize = 128;
    this.left = new Float32Array(this.ringSize);
    this.right = new Float32Array(this.ringSize);
    this.dcAlpha = 1 - Math.exp(-2 * Math.PI * 30 / inputRate);
    this.release = 1 - Math.exp(-1 / (.03 * MPX_RATE));
    this.pilot = new Float32Array(120);
    this.subcarrier = new Float32Array(120);
    for (let i = 0; i < this.pilot.length; i++) {
      const phase = 2 * Math.PI * 19 * i / this.pilot.length;
      this.pilot[i] = PILOT_LEVEL * Math.sin(phase);
      this.subcarrier[i] = Math.sin(2 * phase);
    }
    // A short lookbehind makes the band-limited interpolator causal. Each
    // phase uses the same fractional Blackman window as the file encoder.
    const length = 2 * this.radius + 1;
    const cutoff = Math.min(14200, inputRate * .44) / inputRate;
    this.phaseCount = 64;
    this.bank = Array.from({ length: this.phaseCount }, (_, phase) => {
      const weights = new Float64Array(length);
      const fraction = phase / this.phaseCount;
      let gain = 0;
      for (let i = 0; i < length; i++) {
        const d = this.radius + fraction - i;
        if (Math.abs(d) >= this.radius) continue;
        const angle = Math.PI * d / this.radius;
        const window = .42 + .5 * Math.cos(angle) + .08 * Math.cos(2 * angle);
        const sinc = d === 0 ? 2 * cutoff : Math.sin(2 * Math.PI * cutoff * d) / (Math.PI * d);
        weights[i] = sinc * window;
        gain += weights[i];
      }
      for (let i = 0; i < length; i++) weights[i] /= gain;
      return weights;
    });
    this.reset();
  }

  reset() {
    this.left.fill(0);
    this.right.fill(0);
    this.dcLeft = this.dcRight = 0;
    this.inputIndex = 0;
    this.sourceIndex = 0;
    this.sourcePhase = 0;
    this.pilotIndex = 0;
    this.gain = .8 * (1 - PILOT_LEVEL - RDS_LEVEL);
    this.mpxSamples = 0;
    this.rdsPhase = 0;
    this.rdsEnvelope = null;
    this.rdsNext = null;
    this.rdsHalfSent = false;
    this.chunk = new Uint8Array(MPX_CHUNK);
    this.chunkPosition = 0;
  }

  setRdsEnvelope(envelope) {
    if (!(envelope instanceof Float32Array) || envelope.length < 64) return;
    if (this.rdsEnvelope) this.rdsNext = envelope;
    else {
      this.rdsEnvelope = envelope;
      this.rdsPhase = 0;
      this.rdsEvent('rds-applied', this.mpxSamples);
    }
  }

  rdsSample() {
    const wave = this.rdsEnvelope;
    if (!wave) return 0;
    const at = Math.floor(this.rdsPhase);
    const following = at + 1 === wave.length ? 0 : at + 1;
    const amplitude = wave[at] + (wave[following] - wave[at]) * (this.rdsPhase - at);
    const value = amplitude * this.rdsCarrier[this.pilotIndex % this.rdsCarrier.length];
    this.rdsPhase += RDS_STEP;
    // Prepare the next text midway through the RDS cycle: on air
    // the transmitted timer will be close to actual TX elapsed time.
    if (!this.rdsHalfSent && this.rdsPhase >= wave.length / 2) {
      this.rdsHalfSent = true;
      const targetSamples = this.mpxSamples + Math.round((wave.length - this.rdsPhase) / RDS_STEP);
      this.rdsEvent('rds-prepare', this.mpxSamples, targetSamples);
    }
    if (this.rdsPhase >= wave.length) {
      this.rdsPhase -= wave.length;
      this.rdsHalfSent = false;
      if (this.rdsNext) {
        this.rdsEnvelope = this.rdsNext;
        this.rdsNext = null;
        this.rdsEvent('rds-applied', this.mpxSamples);
      } else this.rdsEvent('rds-cycle', this.mpxSamples);
    }
    return value;
  }

  process(input, frames) {
    const left = input?.[0];
    const right = input?.[1] ?? left;
    const mask = this.ringSize - 1;
    const nominalGain = .8 * (1 - PILOT_LEVEL - RDS_LEVEL);
    for (let i = 0; i < frames; i++) {
      const position = this.inputIndex++;
      const l = left?.[i] ?? 0;
      const r = right?.[i] ?? 0;
      this.dcLeft += this.dcAlpha * (l - this.dcLeft);
      this.dcRight += this.dcAlpha * (r - this.dcRight);
      this.left[position & mask] = l - this.dcLeft;
      this.right[position & mask] = r - this.dcRight;
      while (this.sourceIndex <= position) {
        const phase = Math.floor(this.sourcePhase * this.phaseCount / MPX_RATE);
        const weights = this.bank[phase];
        const start = this.sourceIndex - 2 * this.radius;
        let a = 0;
        let b = 0;
        for (let j = 0; j < weights.length; j++) {
          const index = (start + j) & mask;
          a += this.left[index] * weights[j];
          b += this.right[index] * weights[j];
        }
        // Retain the existing ±50 kHz chain's difference precompensation.
        const programme = .5 * (a + b + 1.145 * (a - b) * this.subcarrier[this.pilotIndex]);
        const limit = (1 - PILOT_LEVEL - RDS_LEVEL) / Math.max(1, Math.abs(programme) * 1.25);
        this.gain = Math.min(limit, this.gain + this.release * (nominalGain - this.gain));
        // RDS is centered on the third harmonic of the 19 kHz stereo pilot.
        const mpx = programme * this.gain + this.pilot[this.pilotIndex] + RDS_LEVEL * this.rdsSample();
        this.mpxSamples++;
        this.chunk[this.chunkPosition++] = clamp(Math.round(mpx * 127), -127, 127);
        if (++this.pilotIndex === this.pilot.length) this.pilotIndex = 0;
        if (this.chunkPosition === this.chunk.length) {
          this.send(this.chunk);
          this.chunk = new Uint8Array(MPX_CHUNK);
          this.chunkPosition = 0;
        }
        this.sourcePhase += this.rate;
        this.sourceIndex += Math.floor(this.sourcePhase / MPX_RATE);
        this.sourcePhase %= MPX_RATE;
      }
    }
  }
}

class StereoDecoder {
  constructor(mode) {
    const profile = PROFILES[mode];
    this.stereo = mode !== 'MONO';
    this.rate = profile.rate;
    this.length = profile.taps;
    this.taps = [lowpass(this.length, profile.cutoff, this.rate, 0),
      lowpass(this.length, profile.cutoff, this.rate, .5)];
    this.sum = new Float64Array(this.length * 2);
    this.diff = new Float64Array(this.length * 2);
    this.position = this.resamplePhase = 0;
    this.sin = 0;
    this.cos = 1;
    this.offset = 0;
    this.setOscillator();
    this.pilotAlpha = 1 - Math.exp(-2 * Math.PI * 250 / this.rate);
    this.dcAlpha = 1 - Math.exp(-2 * Math.PI * 20 / this.rate);
    this.pi = this.pq = this.pi2 = this.pq2 = this.dc = 0;
    this.pilotAmp = this.good = this.bad = this.tick = 0;
    this.locked = false;
    this.blend = this.outLeft = this.outRight = 0;
    this.lastLeft = this.lastRight = this.pending = 0;
    this.gapLength = this.fadeIn = 0;
    this.longGap = false;
  }

  setOscillator() {
    const w = 2 * Math.PI * (19000 + this.offset) / this.rate;
    this.stepSin = Math.sin(w);
    this.stepCos = Math.cos(w);
  }

  rotate(s, c) {
    const nextSin = this.sin * c + this.cos * s;
    this.cos = this.cos * c - this.sin * s;
    this.sin = nextSin;
  }

  track(x) {
    const k = this.pilotAlpha;
    this.pi += k * (x * this.sin - this.pi);
    this.pq += k * (x * this.cos - this.pq);
    this.pi2 += k * (this.pi - this.pi2);
    this.pq2 += k * (this.pq - this.pq2);
    if (++this.tick < this.rate / 1000) return;
    this.tick = 0;
    this.pilotAmp = 2 * Math.hypot(this.pi2, this.pq2);
    const error = Math.atan2(this.pq2, this.pi2);
    if (this.pilotAmp > .025) {
      this.offset = clamp(this.offset + .55 * error, -150, 150);
      this.setOscillator();
      const c = Math.cos(.12 * error);
      const s = Math.sin(.12 * error);
      this.rotate(s, c);
      const a = this.pi * c + this.pq * s;
      const b = this.pi2 * c + this.pq2 * s;
      this.pq = this.pq * c - this.pi * s;
      this.pq2 = this.pq2 * c - this.pi2 * s;
      this.pi = a;
      this.pi2 = b;
    }
    if (this.pilotAmp > .025 && Math.abs(error) < .48) {
      this.bad = 0;
      if (++this.good >= 50) this.locked = true;
    } else {
      this.good = 0;
      if (++this.bad >= 30) this.locked = false;
    }
    const norm = Math.hypot(this.sin, this.cos);
    this.sin /= norm;
    this.cos /= norm;
  }

  input(value, valid) {
    let x = value;
    if (valid) {
      this.dc += this.dcAlpha * (x - this.dc);
      x -= this.dc;
      if (this.stereo) this.track(x);
    }
    const difference = this.stereo ? 4 * x * this.sin * this.cos : 0;
    if (this.stereo) this.rotate(this.stepSin, this.stepCos);
    const end = this.position + this.length;
    this.sum[this.position] = this.sum[end] = x;
    this.diff[this.position] = this.diff[end] = difference;
    if (++this.position === this.length) this.position = 0;
    this.resamplePhase += AUDIO_RATE;
    if (this.resamplePhase < this.rate) return false;
    this.resamplePhase -= this.rate;
    if (!valid) return true;
    const taps = this.taps[this.resamplePhase === 0 ? 0 : 1];
    let m = 0;
    let d = 0;
    for (let i = 0; i < this.length; i++) {
      m += taps[i] * this.sum[end - i];
      d += taps[i] * this.diff[end - i];
    }
    this.blend += ((this.locked ? 1 : 0) - this.blend) * .0011;
    d *= this.blend;
    this.outLeft = m + d;
    this.outRight = m - d;
    return true;
  }

  gap(samples) {
    if (!this.pending) {
      this.lastLeft = this.outLeft;
      this.lastRight = this.outRight;
    }
    this.gapLength += samples;
    if (this.gapLength <= this.rate * .006 && !this.longGap) {
      for (let i = 0; i < samples; i++) {
        if (this.input(this.locked ? this.pilotAmp * this.sin : 0, false)) this.pending++;
      }
      return;
    }
    const position = this.resamplePhase + samples * AUDIO_RATE;
    this.pending += Math.floor(position / this.rate);
    this.resamplePhase = position % this.rate;
    const w = 2 * Math.PI * (19000 + this.offset) * samples / this.rate;
    if (this.stereo) this.rotate(Math.sin(w), Math.cos(w));
    this.sum.fill(0);
    this.diff.fill(0);
    this.outLeft = this.outRight = 0;
    this.longGap = true;
    if (this.gapLength >= this.rate * .05) {
      this.locked = false;
      this.good = this.bad = this.tick = 0;
      this.pi = this.pq = this.pi2 = this.pq2 = 0;
    }
  }

  conceal(push, targetL, targetR) {
    for (let i = 0; i < this.pending; i++) {
      if (this.longGap) {
        const down = Math.max(0, 1 - (i + 1) / 144);
        const up = Math.max(0, 1 - (this.pending - i) / 144);
        push(this.lastLeft * down + targetL * up, this.lastRight * down + targetR * up);
      } else {
        const fraction = (i + 1) / (this.pending + 1);
        push(this.lastLeft + (targetL - this.lastLeft) * fraction,
          this.lastRight + (targetR - this.lastRight) * fraction);
      }
    }
    this.pending = this.gapLength = 0;
    this.longGap = false;
  }

  process(samples, push) {
    for (let i = 0; i < samples.length; i++) {
      const byte = samples[i];
      if (byte === 0x80) {
        let end = i + 1;
        while (end < samples.length && samples[end] === 0x80) end++;
        this.gap(end - i);
        i = end - 1;
        if (this.pending >= 960) {
          this.conceal(push, 0, 0);
          this.fadeIn = 144;
        }
        continue;
      }
      if (!this.input((byte < 128 ? byte : byte - 256) / 128, true)) continue;
      if (this.pending) {
        this.conceal(push, this.outLeft, this.outRight);
        this.fadeIn = 0;
      }
      const gain = this.fadeIn ? 1 - (--this.fadeIn) / 144 : 1;
      push(this.outLeft * gain, this.outRight * gain);
    }
  }
}

class CBRadio extends AudioWorkletProcessor {
  constructor() {
    super();
    this.capacity = 16384;
    this.mask = this.capacity - 1;
    this.ring = new Float32Array(this.capacity * 2);
    this.mode = 'idle';
    this.epoch = 0;
    this.encoder = new StereoEncoder(sampleRate, samples => {
      this.port.postMessage({ type: 'tx', epoch: this.epoch, samples }, [samples.buffer]);
    }, (type, elapsedSamples, targetSamples) => this.port.postMessage({type,epoch:this.epoch,elapsedSamples,targetSamples}));
    this.pushPCM = (left, right) => {
      if (this.writeAt - Math.floor(this.readAt) >= this.capacity) {
        this.readAt = this.writeAt - 1728;
        this.gain = 0;
      }
      const i = (this.writeAt++ & this.mask) * 2;
      this.ring[i] = left;
      this.ring[i + 1] = right;
    };
    this.port.onmessage = ({ data }) => {
      if (data.type === 'mode') {
        this.mode = data.mode;
        this.epoch = data.epoch;
        this.decoder = this.mode === 'rx' ? new StereoDecoder(data.profile) : null;
        if (this.mode === 'tx') this.encoder.reset();
        this.readAt = this.writeAt = 0;
        this.running = this.open = false;
        this.gain = this.correction = this.underruns = this.statusFrames = 0;
      } else if (data.type === 'rds-wave' && this.mode === 'tx' && data.epoch === this.epoch) {
        this.encoder.setRdsEnvelope(data.envelope);
      } else if (data.type === 'squelch') {
        this.open = data.open;
      } else if (data.type === 'rx' && this.decoder && data.epoch === this.epoch) {
        this.decoder.process(data.samples, this.pushPCM);
      }
    };
  }

  process(inputs, outputs) {
    const out = outputs[0];
    const frames = out[0].length;
    if (this.mode === 'tx') this.encoder.process(inputs[0], frames);
    if (this.mode !== 'rx') return true;

    const queued = this.writeAt - this.readAt;
    if (queued > 7200) {
      this.readAt = this.writeAt - 1728;
      this.gain = 0;
    }
    if (!this.running && queued >= 1152) this.running = true;
    const correction = clamp((this.writeAt - this.readAt - 1728) * .000003, -.009, .009);
    this.correction += (correction - this.correction) * .04;
    const step = AUDIO_RATE / sampleRate * (1 + this.correction);
    const ramp = 1 / (.005 * sampleRate);
    for (let i = 0; i < frames; i++) {
      const j = Math.floor(this.readAt);
      if (!this.running || j + 1 >= this.writeAt) {
        if (this.running) this.underruns++;
        this.running = false;
        this.gain = 0;
        break;
      }
      const fraction = this.readAt - j;
      const a = (j & this.mask) * 2;
      const b = ((j + 1) & this.mask) * 2;
      this.gain = clamp(this.gain + (this.open ? ramp : -ramp), 0, 1);
      for (let c = 0; c < 2; c++) {
        const value = this.ring[a + c] + (this.ring[b + c] - this.ring[a + c]) * fraction;
        out[c][i] = clamp(value * this.gain, -1, 1);
      }
      this.readAt += step;
    }
    this.statusFrames += frames;
    if (this.statusFrames >= sampleRate / 5) {
      this.statusFrames = 0;
      this.port.postMessage({ type: 'rx-status', epoch: this.epoch,
        pilotLocked: this.decoder.locked, underruns: this.underruns,
        buffered: (this.writeAt - this.readAt) * 1000 / AUDIO_RATE });
    }
    return true;
  }
}

registerProcessor('cb-radio', CBRadio);
`;
