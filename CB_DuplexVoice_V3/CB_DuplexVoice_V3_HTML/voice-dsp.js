/* CB Duplex Voice — mono PCM8 resampling and passive two-slot receive mixer.
 * The physical on-air acceleration is performed by the V22 warm TDD firmware:
 * each logical sample occupies one 120 kS/s active sample, while the user-side
 * PCM clock is slower. It is NOT the V.22 telephone data modem.
 */
(function (scope) {
'use strict';
const clamp = (v, low, high) => Math.min(high, Math.max(low, v));
const AUDIO_PROFILES = Object.freeze({
  voice: Object.freeze({txHighPassHz:125, txLowPassHz:3000, rxHighPassHz:95, rxLowPassHz:3200, txGain:1.15}),
  music: Object.freeze({txHighPassHz:30, txLowPassHz:5000, rxHighPassHz:30, rxLowPassHz:5000, txGain:1.0})
});
class VoiceDsp {
  constructor({rate=25714,inputRate=48000,dataSamples=12,passive=false,audioMode='voice',onAudio=()=>{},onDiagnostics=()=>{}}={}) {
    if (!(rate>6000&&rate<=60000) || !(inputRate>=16000) || !Number.isInteger(dataSamples) || dataSamples%2) {
      throw new RangeError('Invalid voice sample clock or TDD slot length');
    }
    this.rate=rate;
    this.inputRate=inputRate;
    this.passive=passive;
    this.dataSamples=dataSamples;
    this.halfSlot=dataSamples/2;
    this.onAudio=onAudio;
    this.onDiagnostics=onDiagnostics;
    this.mic=new Float32Array(131072);
    this.micWrite=0;
    this.micRead=0;
    this.micReady=false;
    this.micPrevious=0;
    this.high=0;
    this.low1=0;
    this.low2=0;
    this.audioMode = null;
    this.radioNext=null;
    this.lanes=[[],[]];
    this.rxfHigh=[0,0];this.rxfPrevious=[0,0];this.rxfLow=[0,0];
    this.inputFrames=0;
    this.erased=0;
    this.overrun=0;
    this.setAudioMode(audioMode);
  }
  setAudioMode(mode) {
    const profile=AUDIO_PROFILES[mode];
    if(!profile) throw new RangeError('Unknown audio profile');
    if(this.audioMode===mode)return;
    this.audioMode=mode;
    this.txGain=profile.txGain;
    this.hpCoeff=Math.exp(-2*Math.PI*profile.txHighPassHz/this.inputRate);
    this.lpCoeff=1-Math.exp(-2*Math.PI*profile.txLowPassHz/this.inputRate);
    const receiveRate=this.passive?this.rate/2:this.rate;
    this.rxHpCoeff=Math.exp(-2*Math.PI*profile.rxHighPassHz/receiveRate);
    this.rxLpCoeff=1-Math.exp(-2*Math.PI*profile.rxLowPassHz/receiveRate);
    this.high=this.low1=this.low2=this.micPrevious=0;
    this.rxfHigh.fill(0);this.rxfLow.fill(0);this.rxfPrevious.fill(0);
  }
  pushMic(samples) {
    for(let i=0;i<samples.length;i++) {
      const x=Number.isFinite(samples[i])?clamp(samples[i],-1,1):0;
      this.high=this.hpCoeff*(this.high+x-this.micPrevious);
      this.micPrevious=x;
      this.low1+=this.lpCoeff*(this.high-this.low1);
      this.low2+=this.lpCoeff*(this.low1-this.low2);
      // Voice: 2-pole speech LP. Music: gentler 1-pole wideband LP.
      this.mic[this.micWrite++%this.mic.length]=this.audioMode==='music'?this.low1:this.low2;
    }
    if(!this.micReady && this.micWrite>=Math.round(this.inputRate*.065)) {
      this.micRead=this.micWrite-this.inputRate*.06;
      this.micReady=true;
    }
    this.inputFrames+=samples.length;
  }
  render(count,enabled=true) {
    if(!Number.isSafeInteger(count)||count<0||count>8192) throw new RangeError('Invalid packet length');
    const out=new Int8Array(count);
    if(!enabled || !this.micReady) return out;
    const step=this.inputRate/this.rate;
    // Absorb slow clock differences without allowing unbounded audio latency.
    if(this.micWrite-this.micRead>this.inputRate*.28) {
      this.overrun++;
      this.micRead=this.micWrite-this.inputRate*.12;
    }
    for(let n=0;n<count;n++) {
      const at=Math.floor(this.micRead);
      let v=0;
      if(at>=this.micWrite-this.mic.length+1 && at+1<this.micWrite) {
        const f=this.micRead-at;
        const x=this.mic[at%this.mic.length], y=this.mic[(at+1)%this.mic.length];
        v=x+f*(y-x);
        this.micRead+=step;
      } else {
        // Do not advance into the future permanently after a brief mic stall.
        this.micRead=Math.max(0,this.micWrite-1);
      }
      // -128 is an out-of-band STOP command on the source firmware UART.
      out[n]=clamp(Math.round(clamp(v*this.txGain,-1,1)*127),-127,127);
    }
    return out;
  }
  _filterRx(v,channel) {
    // No pilot/RDS here. Coefficients precomputed once per profile/role,
    // not once for every sample, to reduce DSP timing jitter.
    this.rxfHigh[channel]=this.rxHpCoeff*(this.rxfHigh[channel]+v-this.rxfPrevious[channel]);
    this.rxfPrevious[channel]=v;
    this.rxfLow[channel]+=this.rxLpCoeff*(this.rxfHigh[channel]-this.rxfLow[channel]);
    return clamp(this.rxfLow[channel],-1,1);
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
    if(gap>0) {
      // Large RF outages do not allocate unbounded audio memory.
      if(gap>8192) {this.erased+=gap;this.radioNext=first;this.lanes=[[],[]];}
      else this._pushSpan(this.radioNext,new Int8Array(gap).fill(-128));
    }
    this._pushSpan(first,bytes);
    this.radioNext=first+bytes.length;
  }
  _pushSpan(index,bytes) {
    if(!this.passive) {
      const out=new Float32Array(bytes.length);
      for(let i=0;i<bytes.length;i++) {
        if(bytes[i]===-128)this.erased++;
        out[i]=this._filterRx(bytes[i]===-128?0:bytes[i]/127,0);
      }
      // Measurements are taken before the playback array is transferred to the worklet.
      this.onDiagnostics(index,bytes,out,this.rate);
      this.onAudio(out,this.rate);
      return;
    }
    const half=this.halfSlot;
    for(let i=0;i<bytes.length;i++) {
      const channel=Math.floor((index+i)/half)%2;
      const b=bytes[i];
      if(b===-128)this.erased++;
      this.lanes[channel].push(this._filterRx(b===-128?0:b/127,channel));
    }
    const ready=Math.min(this.lanes[0].length,this.lanes[1].length);
    if(!ready)return;
    const out=new Float32Array(ready);
    for(let i=0;i<ready;i++) {
      const a=this.lanes[0][i],b=this.lanes[1][i];
      // Both participants can speak at once. A soft saturation keeps headroom.
      const sum=a+b;
      out[i]=clamp(sum/(1+0.15*Math.abs(sum)),-1,1);
    }
    this.lanes[0].splice(0,ready);this.lanes[1].splice(0,ready);
    this.onAudio(out,this.rate/2);
  }
}
scope.CBVoiceDsp=VoiceDsp;
if(typeof module==='object'&&module.exports)module.exports={VoiceDsp,AUDIO_PROFILES};
})(typeof globalThis==='object'?globalThis:this);
