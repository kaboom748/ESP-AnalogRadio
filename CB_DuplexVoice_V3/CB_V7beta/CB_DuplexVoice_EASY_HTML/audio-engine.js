/* Browser audio I/O. This AudioWorklet only handles microphone capture and
 * buffered mono playback. The firmware schedules the radio TDD slots. */
(function(scope){
'use strict';
const timings=scope.CBVoiceAudioTiming;
if(!scope.CBVoiceResampler||!timings)throw Error('voice-dsp.js must load before audio-engine.js');
const source=String.raw`
const createVoiceResampler=${scope.CBVoiceResampler.toString()};
const timings=${JSON.stringify(timings)};
class DuplexVoiceIO extends AudioWorkletProcessor {
  constructor(){
    super();
    this.play=new Float32Array(131072);this.mask=this.play.length-1;
    this.real=new Uint8Array(this.play.length);
    this.rate=25714;this.mode='voice';this.timing=timings.voice;
    this.resampler=createVoiceResampler(this.rate,sampleRate);
    this.micOn=false;this.mic=new Float32Array(256);this.micAt=0;this.pulse=0;
    this.reset();
    this.underruns=0;this.underrunFrames=0;this.dropped=0;this.lateSamples=0;
    this.latencyTrims=0;this.latencySamplesDiscarded=0;
    this.port.onmessage=({data})=>{
      if(data.type==='rate'){
        if(!(data.rate>6000&&data.rate<=60000))return;
        this.rate=data.rate;
        this.resampler=createVoiceResampler(this.rate,sampleRate);
        this.reset();
        this.underruns=0;this.underrunFrames=0;this.dropped=0;this.lateSamples=0;
        this.latencyTrims=0;this.latencySamplesDiscarded=0;
      } else if(data.type==='profile'){
        this.mode=data.mode;this.timing=data.timing||timings[this.mode];
      } else if(data.type==='play'){
        this.append(data.values.length,data.values);
      } else if(data.type==='gap'){
        if(Number.isSafeInteger(data.count)&&data.count>0)this.append(data.count,null);
      } else if(data.type==='mic'){
        this.micOn=data.enabled;
        if(!this.micOn)this.micAt=0;
      } else if(data.type==='clear'){
        this.reset();this.micAt=0;
      }
    };
  }
  reset(){
    this.writeAt=0;this.readAt=0;this.historyFrom=0;
    this.running=false;this.starving=false;this.correction=0;this.gain=0;
    this.tail=0;
  }
  append(count,values){
    const first=this.writeAt,end=first+count;
    const oldest=Math.max(0,end-this.play.length);
    const previousOldest=Math.max(0,first-this.play.length);
    const consumed=Math.floor(this.readAt);
    for(let at=Math.max(previousOldest,consumed);at<Math.min(first,oldest);at++){
      this.dropped+=this.real[at&this.mask];
    }
    if(values)this.dropped+=Math.max(0,oldest-Math.max(first,consumed));
    if(values&&this.mode==='signal')this.lateSamples+=Math.min(count,Math.max(0,consumed-first));
    for(let at=Math.max(first,oldest);at<end;at++){
      this.play[at&this.mask]=values?values[at-first]:0;
      this.real[at&this.mask]=values?1:0;
    }
    this.writeAt=end;
    const queue=this.writeAt-this.readAt;
    if(this.mode!=='signal'&&queue>this.rate*this.timing.maxMs/1000){
      const next=Math.max(this.historyFrom,Math.floor(this.writeAt-this.rate*this.timing.targetMs/1000));
      this.latencySamplesDiscarded+=next-Math.floor(this.readAt);
      this.readAt=this.historyFrom=next;
      this.latencyTrims++;this.gain=0;this.tail=0;this.running=false;
    }
  }
  process(inputs,outputs){
    const input=inputs[0]?.[0];
    if(this.micOn&&input){
      for(let i=0;i<input.length;i++){
        this.mic[this.micAt++]=input[i];
        if(this.micAt===this.mic.length){
          const block=this.mic;
          this.mic=new Float32Array(256);this.micAt=0;
          this.port.postMessage({type:'mic',values:block},[block.buffer]);
        }
      }
    }
    const out=outputs[0][0];
    const queue=this.writeAt-this.readAt;
    const linear=this.mode==='signal';
    const threshold=Math.max(this.resampler.half+2,Math.round(this.rate*this.timing.startMs/1000));
    if(!this.running&&queue>=threshold){this.running=true;this.gain=linear?1:0;}
    const limit=linear?.0004:.001;
    const error=queue/this.rate-this.timing.targetMs/1000;
    const target=this.running?Math.max(-limit,Math.min(limit,error*.025)):0;
    this.correction+=(target-this.correction)*(1-Math.exp(-out.length/sampleRate/1.5));
    const step=this.rate/sampleRate*(1+this.correction);
    const oldest=Math.max(this.historyFrom,this.writeAt-this.play.length);
    const ramp=1/(sampleRate*.004);
    for(let i=0;i<out.length;i++){
      const at=Math.floor(this.readAt);
      if(this.running&&at>=oldest&&at+(linear?0:this.resampler.half)<this.writeAt){
        this.starving=false;
        if(!linear)this.gain=Math.min(1,this.gain+ramp);
        out[i]=this.resampler.read(this.play,this.mask,this.readAt,oldest,this.writeAt)*this.gain;
        this.tail=out[i];
        this.readAt+=step;
      } else {
        if(this.running){
          if(!this.starving)this.underruns++;
          this.starving=true;
        }
        if(this.starving)this.underrunFrames++;
        if(linear){
          // Missing playout time is silence, and late samples cannot shift the modem clock.
          out[i]=0;
          if(this.running)this.readAt+=step;
        }else{
          this.running=false;this.gain=0;
          out[i]=this.tail;
          this.tail*=1-ramp*8;
        }
      }
    }
    if(++this.pulse%50===0){
      this.port.postMessage({type:'stats',bufferMs:1000*(this.writeAt-this.readAt)/this.rate,
        underruns:this.underruns,underrunFrames:this.underrunFrames,dropped:this.dropped,
        lateSamples:this.lateSamples,clockCorrectionPpm:Math.round(this.correction*1e6),
        latencyTrims:this.latencyTrims,latencySamplesDiscarded:this.latencySamplesDiscarded});
    }
    return true;
  }
}
registerProcessor('cb-duplex-voice-io',DuplexVoiceIO);
`;
class DuplexAudio {
  constructor(onMic=()=>{},onStats=()=>{}){
    this.onMic=onMic;this.onStats=onStats;this.ctx=null;this.node=null;this.stream=null;this.micSource=null;
    this.audioMode='voice';this.captureSettings=null;this.buffering=null;
  }
  async open(){
    if(this.ctx){await this.ctx.resume();return;}
    const AudioContextClass=globalThis.AudioContext||globalThis.webkitAudioContext;
    if(!AudioContextClass)throw Error('Web Audio API is unavailable.');
    let ctx;
    try{ctx=new AudioContextClass({sampleRate:48000,latencyHint:'interactive'});}catch(_){ctx=new AudioContextClass({latencyHint:'interactive'});}
    if(!ctx.audioWorklet){await ctx.close();throw Error('AudioWorklet requires a modern browser.');}
    // A blob created from file:// can become blob:null/... and Chromium may
    // reject it as a worklet module. A data: module also works for an
    // unpacked local HTML page, following the CB FM Stereo audio loader.
    const moduleUrl='data:text/javascript;charset=utf-8,'+encodeURIComponent(source);
    try{
      await ctx.audioWorklet.addModule(moduleUrl);
    }catch(error){
      await ctx.close().catch(()=>{});
      throw Error('AudioWorklet module could not load. Use current Chrome/Edge; if opened as a local file, try http://localhost. '+(error?.message||error));
    }
    this.ctx=ctx;
    this.node=new AudioWorkletNode(ctx,'cb-duplex-voice-io',{numberOfInputs:1,numberOfOutputs:1,outputChannelCount:[1]});
    this.node.port.onmessage=({data})=>{
      if(data.type==='mic')this.onMic(data.values);
      if(data.type==='stats')this.onStats(data);
    };
    this.applyProfile();
    // Keep the worklet processing even when the microphone is not yet opened.
    this.silence=ctx.createConstantSource();this.silence.offset.value=0;
    this.silence.connect(this.node);this.silence.start();
    this.volume=ctx.createGain();this.volume.gain.value=0.65;
    this.node.connect(this.volume).connect(ctx.destination);
    await ctx.resume();
  }
  get inputRate(){return this.ctx?.sampleRate??48000;}
  setVolume(percent){if(this.volume)this.volume.gain.value=Math.max(0,Math.min(1.5,percent/100));}
  setRate(rate){this.node?.port.postMessage({type:'rate',rate});}
  play(samples){
    if(!this.node||!samples.length)return;
    // No heavy DSP runs on the UI thread; send normalized mono samples directly.
    this.node.port.postMessage({type:'play',values:samples},[samples.buffer]);
  }
  playGap(count){
    if(!Number.isSafeInteger(count)||count<0)throw new RangeError('Invalid playout gap');
    if(count)this.node?.port.postMessage({type:'gap',count});
  }
  clear(){this.node?.port.postMessage({type:'clear'});}
  setAudioMode(mode){
    if(!Object.hasOwn(timings,mode))throw new RangeError('Unknown audio profile');
    if(this.stream&&mode!==this.audioMode)throw Error('End the session before changing microphone processing.');
    this.audioMode=mode;
    this.applyProfile();
  }
  setBuffering(timing){
    if(timing===null){this.buffering=null;this.applyProfile();return;}
    const {startMs,targetMs,maxMs}=timing;
    if(![startMs,targetMs,maxMs].every(Number.isFinite)||
      startMs<6||startMs>targetMs||targetMs>=maxMs||maxMs>250)throw new RangeError('Invalid playout timing');
    this.buffering={startMs,targetMs,maxMs};
    this.applyProfile();
  }
  applyProfile(){
    this.node?.port.postMessage({type:'profile',mode:this.audioMode,timing:this.buffering});
  }
  async startMic(){
    await this.open();
    if(this.stream){this.node.port.postMessage({type:'mic',enabled:true});return;}
    if(!navigator.mediaDevices?.getUserMedia)throw Error('Microphone capture requires a secure context.');
    // Browser voice enhancement can aggressively gate quiet music and regard
    // loudspeaker audio as echo. Music requests unprocessed capture instead.
    const enhance=this.audioMode==='voice';
    const stream=await navigator.mediaDevices.getUserMedia({audio:{channelCount:1,
      echoCancellation:enhance,noiseSuppression:enhance,autoGainControl:enhance}});
    this.captureSettings=stream.getAudioTracks()[0]?.getSettings?.()??{};
    this.stream=stream;
    this.micSource=this.ctx.createMediaStreamSource(stream);
    this.micSource.connect(this.node);
    this.node.port.postMessage({type:'mic',enabled:true});
  }
  stopMic(){
    this.node?.port.postMessage({type:'mic',enabled:false});
    this.micSource?.disconnect();this.micSource=null;
    for(const track of this.stream?.getTracks()||[])track.stop();
    this.stream=null;this.captureSettings=null;
  }
  async close(){
    this.stopMic();
    this.silence?.stop();this.silence?.disconnect();this.silence=null;
    this.node?.disconnect();this.volume?.disconnect();this.node=null;
    if(this.ctx)await this.ctx.close().catch(()=>{});
    this.ctx=null;
  }
}
scope.CBDuplexAudio=DuplexAudio;
if(typeof module==='object'&&module.exports)module.exports={DuplexAudio,source};
})(globalThis);
