/* Browser audio I/O. This AudioWorklet only handles microphone capture and
 * buffered mono playback. The radio TDD scheduling remains on the ESP8266. */
(function(scope){
'use strict';
const source=String.raw`
class DuplexVoiceIO extends AudioWorkletProcessor {
  constructor(){
    super();
    this.play=new Float32Array(131072);this.mask=this.play.length-1;
    this.writeAt=0;this.readAt=0;this.rate=25714;this.running=false;this.gain=0;
    this.micOn=false;this.mic=new Float32Array(512);this.micAt=0;
    this.underruns=0;this.dropped=0;this.pulse=0;
    // Conversational audio is live: old speech must never accumulate for seconds.
    this.latencyTrims=0;this.latencySamplesDiscarded=0;
    this.port.onmessage=({data})=>{
      if(data.type==='rate'){
        this.rate=data.rate;this.writeAt=this.readAt=0;this.running=false;this.gain=0;
        this.latencyTrims=0;this.latencySamplesDiscarded=0;
      } else if(data.type==='play'){
        // Hard latency boundary: at 25,714 Hz the physical ring holds ~5 s,
        // but a live call must never replay a backlog that old. Keep the
        // newest 120 ms whenever received data exceeds 250 ms of playout.
        const maxBuffered=Math.max(256,Math.round(this.rate*.25));
        const targetBuffered=Math.max(128,Math.round(this.rate*.12));
        const values=data.values;
        for(let i=0;i<values.length;i++){
          if(this.writeAt-Math.floor(this.readAt)>=this.play.length-2){
            this.readAt=Math.floor(this.readAt)+1;this.dropped++;
          }
          this.play[this.writeAt++&this.mask]=values[i];
        }
        const buffered=this.writeAt-this.readAt;
        if(buffered>maxBuffered){
          const skip=buffered-targetBuffered;
          this.readAt=this.writeAt-targetBuffered;
          this.latencySamplesDiscarded+=Math.max(0,Math.floor(skip));
          this.latencyTrims++;
          // Fade in following a discontinuity; no old voice is replayed.
          this.gain=0;this.running=false;
        }
      } else if(data.type==='mic'){
        this.micOn=data.enabled;
        if(!this.micOn)this.micAt=0;
      } else if(data.type==='clear'){
        this.writeAt=this.readAt=0;this.running=false;this.gain=0;this.micAt=0;
      }
    };
  }
  process(inputs,outputs){
    const input=inputs[0]?.[0];
    if(this.micOn&&input){
      for(let i=0;i<input.length;i++){
        this.mic[this.micAt++]=input[i];
        if(this.micAt===this.mic.length){
          const block=this.mic;
          this.mic=new Float32Array(512);this.micAt=0;
          this.port.postMessage({type:'mic',values:block},[block.buffer]);
        }
      }
    }
    const out=outputs[0][0];
    const queue=this.writeAt-this.readAt;
    const threshold=Math.max(100,Math.round(this.rate*.085));
    if(!this.running && queue>=threshold)this.running=true;
    const target=this.rate*.12;
    // Mild clock tracking prevents gradual browser/ESP sample-clock drift.
    const correction=this.running?Math.max(-.015,Math.min(.03,(queue-target)*.000012)):0;
    const step=this.rate/sampleRate*(1+correction);
    for(let i=0;i<out.length;i++){
      const at=Math.floor(this.readAt);
      if(this.running&&at+1<this.writeAt){
        const f=this.readAt-at;
        const a=this.play[at&this.mask],b=this.play[(at+1)&this.mask];
        this.gain=Math.min(1,this.gain+.004);
        out[i]=(a+(b-a)*f)*this.gain;
        this.readAt+=step;
      } else {
        if(this.running)this.underruns++;
        this.running=false;this.gain=0;out[i]=0;
      }
    }
    if(++this.pulse%50===0){
      this.port.postMessage({type:'stats',bufferMs:1000*(this.writeAt-this.readAt)/this.rate,
        underruns:this.underruns,dropped:this.dropped,
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
    this.audioMode='voice';this.captureSettings=null;
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
  clear(){this.node?.port.postMessage({type:'clear'});}
  setAudioMode(mode){
    if(mode!=='voice'&&mode!=='music')throw new RangeError('Unknown audio profile');
    if(this.stream&&mode!==this.audioMode)throw Error('End the session before changing microphone processing.');
    this.audioMode=mode;
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
})(globalThis);
