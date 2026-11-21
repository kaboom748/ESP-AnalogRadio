/* CB Duplex Voice — mono PCM8 resampling and passive two-slot receive mixer.
 * The physical on-air acceleration is performed by the V22 warm TDD firmware:
 * each logical sample occupies one 120 kS/s active sample, while the user-side
 * PCM clock is slower. It is NOT the V.22 telephone data modem.
 */
(function (scope) {
'use strict';
const clamp = (v, low, high) => Math.min(high, Math.max(low, v));
const shaping=scope.CBDuplexSignalShaping || (typeof require==='function'?require('./signal-shaping.js'):null);
if(!shaping)throw Error('signal-shaping.js must load before voice-dsp.js');
const AUDIO_PROFILES = Object.freeze({
  voice: Object.freeze({txHighPassHz:125, txLowPassHz:3000, rxHighPassHz:95, rxLowPassHz:3200, txGain:1.15}),
  music: Object.freeze({txHighPassHz:30, txLowPassHz:9600, rxHighPassHz:30, rxLowPassHz:10000, txGain:1}),
  signal: Object.freeze({txHighPassHz:0, txLowPassHz:0, rxHighPassHz:0, rxLowPassHz:0, txGain:1})
});
const AUDIO_TIMING = Object.freeze({
  voice: Object.freeze({captureMs:32,startMs:24,targetMs:32,maxMs:100}),
  music: Object.freeze({captureMs:32,startMs:20,targetMs:28,maxMs:95}),
  signal: Object.freeze({captureMs:32,startMs:24,targetMs:32,maxMs:95})
});
const ERASE_BLOCK=new Int8Array(8192).fill(-128);
const fade=(position,length)=>.5-.5*Math.cos(Math.PI*Math.min(1,position/length));

// This function is also embedded verbatim in the AudioWorklet. Table generation
// is outside the processing loop, and the FIR is symmetric at fractional phase.
function createVoiceResampler(inputRate,outputRate,ceiling=Infinity) {
  const ratio=Math.min(1,outputRate/inputRate,ceiling/(.43*inputRate));
  const half=Math.ceil(8/ratio),taps=2*half+1,phases=128,cutoff=.43*ratio;
  const coeffs=new Float32Array(taps*(phases+1));
  for(let phase=0;phase<=phases;phase++){
    const fraction=phase/phases,offset=phase*taps;
    let total=0;
    for(let tap=0;tap<taps;tap++){
      const time=tap-half-fraction,position=Math.PI*time/half;
      const window=Math.abs(time)<half?.42+.5*Math.cos(position)+.08*Math.cos(2*position):0;
      const angle=2*Math.PI*cutoff*time;
      const sinc=angle===0?2*cutoff:Math.sin(angle)/(Math.PI*time);
      const weight=window*sinc;
      coeffs[offset+tap]=weight;
      total+=weight;
    }
    for(let tap=0;tap<taps;tap++)coeffs[offset+tap]/=total;
  }
  function read(ring,mask,position,validFrom,validTo){
    const center=Math.floor(position),fraction=(position-center)*phases;
    const phase=Math.floor(fraction),mix=fraction-phase,offset=phase*taps;
    const first=center-half;
    const begin=Math.max(0,validFrom-first),end=Math.min(taps,validTo-first);
    let value=0;
    for(let tap=begin;tap<end;tap++){
      const weight=coeffs[offset+tap];
      value+=ring[(first+tap)&mask]*(weight+mix*(coeffs[offset+taps+tap]-weight));
    }
    return value;
  }
  return {half,read};
}
class VoiceDsp {
  constructor({rate=25714,inputRate=48000,dataSamples=12,passive=false,audioMode='voice',companding='strong',
    tddFilter=true,captureAheadSamples=0,onAudio=()=>{},onDiagnostics=()=>{},onGap=()=>{}}={}) {
    if (!(rate>6000&&rate<=60000) || !(inputRate>=16000&&inputRate<=384000) ||
        !Number.isInteger(dataSamples) || dataSamples<2 || dataSamples%2 ||
        !Number.isSafeInteger(captureAheadSamples) || captureAheadSamples<0 || captureAheadSamples>8192) {
      throw new RangeError('Invalid voice sample clock or TDD slot length');
    }
    this.rate=rate;
    this.inputRate=inputRate;
    this.captureAhead=inputRate*captureAheadSamples/rate;
    this.passive=passive;
    this.dataSamples=dataSamples;
    this.companding='off';
    this.requestedCompanding='off';
    this.tddFilter=false;
    this.notches=[];
    this.notchHz=rate/dataSamples;
    this.slotSamples=dataSamples;
    this.onAudio=onAudio;
    this.onDiagnostics=onDiagnostics;
    this.onGap=onGap;
    this.mic=new Float32Array(131072);
    this.micMask=this.mic.length-1;
    this.micWrite=0;
    this.micRead=0;
    this.micReady=false;
    this.captureStarted=false;
    this.micCorrection=0;
    this.micMissed=0;
    this.micSamplesDiscarded=0;
    this.micPrevious=0;
    this.high=0;
    this.low1=0;
    this.low2=0;
    this.audioMode = null;
    this.radioNext=null;
    this.lanes=[[],[]];
    this.rxfHigh=[0,0];this.rxfPrevious=[0,0];this.rxfLow=[0,0];
    this.concealment=[{},{}];
    this.concealedSamples=0;
    this.inputFrames=0;
    this.erased=0;
    this.overrun=0;
    this.setAudioMode(audioMode);
    this.setCompanding(companding);
    this.setTddFilter(tddFilter);
  }
  setAudioMode(mode) {
    if(!Object.hasOwn(AUDIO_PROFILES,mode))throw new RangeError('Unknown audio profile');
    if(this.audioMode===mode)return;
    const profile=AUDIO_PROFILES[mode];
    const music=mode==='music';
    this.audioMode=mode;
    this.resampler=createVoiceResampler(this.inputRate,this.rate,music?profile.txLowPassHz:Infinity);
    // In Music mode a missing TDD slot must not smear the following good slots.
    this.releaseSamples=Math.max(1,Math.round(this.rate*(music ? .00025 : .004)));
    this.recoverSamples=Math.max(1,Math.round(this.rate*(music ? .00025 : .002)));
    this.companding=mode==='signal'?'off':this.requestedCompanding;
    this.txGain=profile.txGain;
    // The microphone reserve must cover a one-shot serial lead fill plus
    // the worklet handoff and fractional interpolation lookahead.
    const minimum=this.inputRate*AUDIO_TIMING[mode].captureMs/1000;
    this.captureLead=Math.max(minimum,this.captureAhead+this.inputRate*.008);
    this.captureSteady=this.captureLead-this.captureAhead;
    this.hpCoeff=profile.txHighPassHz?Math.exp(-2*Math.PI*profile.txHighPassHz/this.inputRate):0;
    this.lpCoeff=1-Math.exp(-2*Math.PI*profile.txLowPassHz/this.inputRate);
    const receiveRate=this.rate;
    this.rxHpCoeff=Math.exp(-2*Math.PI*profile.rxHighPassHz/receiveRate);
    this.rxLpCoeff=1-Math.exp(-2*Math.PI*profile.rxLowPassHz/receiveRate);
    this.high=this.low1=this.low2=this.micPrevious=0;
    this.rxfHigh.fill(0);this.rxfLow.fill(0);this.rxfPrevious.fill(0);
    for(const state of this.concealment)Object.assign(state,{last:0,lost:0,start:0,recover:0});
    this._makeNotches();
  }
  setCompanding(mode){
    if(!shaping.METHODS[mode])throw new RangeError('Unknown companding setting');
    this.requestedCompanding=mode;
    this.companding=this.audioMode==='signal'?'off':mode;
  }
  setTddFilter(enabled){
    this.tddFilter=!!enabled;
    this._makeNotches();
  }
  _makeNotches(){
    // Both the active receiver and each passive source run at the same rate.
    // The filter follows the actual TDD burst clock, not the UART byte rate.
    const sampleRate=this.rate;
    const fundamental=this.rate/this.dataSamples;
    this.notchHz=fundamental;
    this.notches=[];
    if(!this.tddFilter||this.audioMode==='signal')return;
    for(let harmonic=1;harmonic<=8;harmonic++){
      const hz=harmonic*fundamental;
      if(hz>=sampleRate*.45 || hz>12000)continue;
      const q=harmonic===1?30:35;
      const omega=2*Math.PI*hz/sampleRate,alpha=Math.sin(omega)/(2*q),a0=1+alpha;
      const b0=1/a0,b1=-2*Math.cos(omega)/a0,a1=b1,a2=(1-alpha)/a0;
      this.notches.push({b0,b1,a1,a2,z1:0,z2:0,hz});
    }
  }
  _notch(value){
    if(!this.notches.length)return value;
    let sample=value;
    for(const f of this.notches){
      const out=f.b0*sample+f.z1;
      f.z1=f.b1*sample-f.a1*out+f.z2;
      f.z2=f.b0*sample-f.a2*out;
      sample=out;
    }
    return Math.max(-1,Math.min(1,sample));
  }
  pushMic(samples) {
    for(let i=0;i<samples.length;i++) {
      const x=Number.isFinite(samples[i])?clamp(samples[i],-1,1):0;
      let value=x;
      if(this.hpCoeff){
        this.high=this.hpCoeff*(this.high+x-this.micPrevious);
        this.micPrevious=x;
        value=this.high;
      }
      if(this.audioMode==='voice'){
        this.low1+=this.lpCoeff*(this.high-this.low1);
        this.low2+=this.lpCoeff*(this.low1-this.low2);
        value=this.low2;
      }
      this.mic[this.micWrite++&this.micMask]=value;
    }
    if(!this.micReady)this.primeCapture();
    this.inputFrames+=samples.length;
  }
  primeCapture() {
    if(this.captureStarted||this.micWrite<Math.ceil(this.captureLead)+this.resampler.half+1)return false;
    this.micRead=this.micWrite-this.captureLead;
    this.micCorrection=0;
    this.micReady=true;
    return true;
  }
  render(count,enabled=true) {
    if(!Number.isSafeInteger(count)||count<0||count>8192) throw new RangeError('Invalid packet length');
    const out=new Int8Array(count);
    if(!enabled || !this.micReady || !count) return out;
    this.captureStarted=true;
    const linear=this.audioMode==='signal';
    const buffered=this.micWrite-this.micRead;
    const nominalStep=this.inputRate/this.rate;
    if(linear){
      const error=(buffered-count*nominalStep-this.captureSteady)/this.inputRate;
      const target=clamp(error*.02,-.0004,.0004);
      this.micCorrection+=(target-this.micCorrection)*(1-Math.exp(-count/this.rate/2));
    }
    const step=nominalStep*(linear?1+this.micCorrection:1);
    // Absorb slow clock differences without allowing unbounded audio latency.
    if(!linear&&buffered>this.inputRate*.14) {
      this.overrun++;
      this.micSamplesDiscarded+=Math.floor(buffered-this.captureLead);
      this.micRead=this.micWrite-this.captureLead;
    }
    const validFrom=Math.max(0,this.micWrite-this.mic.length);
    for(let n=0;n<count;n++) {
      const at=Math.floor(this.micRead),half=this.resampler.half;
      let v=0;
      if(at>=validFrom && at+half<this.micWrite) {
        v=this.resampler.read(this.mic,this.micMask,this.micRead,validFrom,this.micWrite);
        this.micRead+=step;
      } else {
        this.micMissed++;
        // Signal capture keeps its sample time through a microphone stall.
        if(linear)this.micRead+=step;
        else this.micRead=Math.max(validFrom,this.micWrite-half-1);
      }
      // -128 is an out-of-band STOP command on the source firmware UART.
      out[n]=shaping.encode(v*this.txGain,this.companding);
    }
    return out;
  }
  _filterRx(v,channel) {
    if(this.audioMode==='signal')return v;
    // No pilot/RDS here. Coefficients precomputed once per profile/role,
    // not once for every sample, to reduce DSP timing jitter.
    this.rxfHigh[channel]=this.rxHpCoeff*(this.rxfHigh[channel]+v-this.rxfPrevious[channel]);
    this.rxfPrevious[channel]=v;
    this.rxfLow[channel]+=this.rxLpCoeff*(this.rxfHigh[channel]-this.rxfLow[channel]);
    return clamp(this.rxfLow[channel],-1,1);
  }
  _conceal(v,missing,channel){
    if(this.audioMode==='signal')return v;
    const state=this.concealment[channel];
    if(missing){
      if(!state.lost){state.start=state.last;state.recover=0;}
      if(state.lost<this.releaseSamples)this.concealedSamples++;
      state.lost=Math.min(state.lost+1,this.releaseSamples);
      state.last=state.start*(1-fade(state.lost,this.releaseSamples));
      return state.last;
    }
    if(state.lost){state.lost=0;state.recover=this.recoverSamples;state.start=state.last;}
    if(state.recover){
      const blend=fade(this.recoverSamples-state.recover+1,this.recoverSamples);
      state.recover--;
      v=state.start+(v-state.start)*blend;
    }
    state.last=v;
    return v;
  }
  pushRadio(first,bytes) {
    if(!(bytes instanceof Int8Array)||!Number.isSafeInteger(first)||first<0) throw new TypeError('Invalid received PCM frame');
    if(this.radioNext===null) this.radioNext=first;
    if(first<this.radioNext) {
      const overlap=this.radioNext-first;
      if(overlap>=bytes.length)return;
      bytes=bytes.subarray(overlap);first=this.radioNext;
    }
    const gap=first-this.radioNext;
    if(gap>0)this._pushGap(this.radioNext,gap);
    this._pushSpan(first,bytes);
    this.radioNext=first+bytes.length;
  }
  _pushGap(first,count){
    if(count<=ERASE_BLOCK.length){
      this._pushSpan(first,ERASE_BLOCK.slice(0,count));
      return;
    }
    // Render the leading tail, then pass a long silence as a sample count.
    // In passive mode each full cycle contains the same samples in both lanes.
    const period=this.passive?2*this.slotSamples:1;
    const prefix=(period-first%period)%period+Math.ceil(this.rate*.012/period)*period;
    const remainder=(count-prefix)%period;
    const silent=count-prefix-remainder;
    this._pushSpan(first,ERASE_BLOCK.slice(0,prefix));
    this.erased+=silent;
    this.rxfHigh.fill(0);this.rxfLow.fill(0);this.rxfPrevious.fill(0);
    for(const state of this.concealment)Object.assign(state,{last:0,lost:this.releaseSamples,start:0,recover:0});
    this._makeNotches();
    this.onGap(this.passive?silent/2:silent,{first:first+prefix,radioSamples:silent});
    if(remainder)this._pushSpan(first+prefix+silent,ERASE_BLOCK.slice(0,remainder));
  }
  _pushSpan(index,bytes) {
    if(!this.passive) {
      const out=new Float32Array(bytes.length);
      for(let i=0;i<bytes.length;i++) {
        if(bytes[i]===-128)this.erased++;
        out[i]=this._filterRx(bytes[i]===-128?0:shaping.decode(bytes[i],this.companding),0);
      }
      // Measurements are taken before the playback array is transferred to the worklet.
      // Diagnostics deliberately uses pre-notch filtered audio. In-band test
      // tones at the TDD harmonics must not be mistaken for RF attenuation.
      this.onDiagnostics(index,bytes,out,this.rate);
      const playback=this.audioMode==='signal'?out:Float32Array.from(out,
        (v,i)=>this._notch(this._conceal(v,bytes[i]===-128,0)));
      this.onAudio(playback,this.rate);
      return;
    }
    const slot=this.slotSamples;
    for(let i=0;i<bytes.length;i++) {
      const channel=Math.floor((index+i)/slot)%2;
      const b=bytes[i];
      if(b===-128)this.erased++;
      const v=this._filterRx(b===-128?0:shaping.decode(b,this.companding),channel);
      this.lanes[channel].push(this._conceal(v,b===-128,channel));
    }
    const ready=Math.min(this.lanes[0].length,this.lanes[1].length);
    if(!ready)return;
    const out=new Float32Array(ready);
    for(let i=0;i<ready;i++) {
      const a=this.lanes[0][i],b=this.lanes[1][i];
      // Both participants can speak at once. A soft saturation keeps headroom.
      const sum=a+b;
      out[i]=this.audioMode==='signal'?sum*.5:this._notch(clamp(sum/(1+0.15*Math.abs(sum)),-1,1));
    }
    this.lanes[0].splice(0,ready);this.lanes[1].splice(0,ready);
    this.onAudio(out,this.rate);
  }
}
scope.CBVoiceDsp=VoiceDsp;
scope.CBVoiceResampler=createVoiceResampler;
scope.CBVoiceAudioTiming=AUDIO_TIMING;
if(typeof module==='object'&&module.exports)module.exports={VoiceDsp,AUDIO_PROFILES,AUDIO_TIMING,createVoiceResampler};
})(typeof globalThis==='object'?globalThis:this);
