/* CB Duplex Voice EASY front panel — presentation and user controls only.
 * Reuses the user's CB FM Stereo faceplate and DTMF keypad layout.
 * No modifications to audio-engine.js, voice-dsp.js, signal-shaping.js or INO.
 */
(function(scope){
'use strict';
const $=id=>document.getElementById(id);
const clamp=(value,lo,hi)=>Math.max(lo,Math.min(hi,value));
const CHANNELS=Object.freeze([
  {name:'01',khz:2412312},
  {name:'07',khz:2442312},
  {name:'11',khz:2462312}
]);
const DTMF=Object.freeze({
  '1':[697,1209], '2':[697,1336], '3':[697,1477],
  '4':[770,1209], '5':[770,1336], '6':[770,1477],
  '7':[852,1209], '8':[852,1336], '9':[852,1477],
  '*':[941,1209], '0':[941,1336], '#':[941,1477]
});
const dbGain=decibels=>Math.pow(10,decibels/20);
const channelFor=khz=>CHANNELS.find(channel=>channel.khz===Number(khz));
function dBFS(amplitude){return 20*Math.log10(Math.max(0.0001,amplitude));}
class CBFacade {
  constructor(controller){
    this.c=controller;
    this.activeTab='easy';
    this.gainDb=0;
    this.micDevice='default';
    this.speakerDevice='default';
    this.rxLevel=0;this.txLevel=0;
    this.signalQuality=0;
    this.signalFrames=0;
    this.detailUntil=0;
    this.activeKey=null;
    this.activeTone=null;
    this.keyQueue=[];
    this.gapSamples=0;
    this.localTone=null;
    this.toneAt=0;
    this.lastToneLevel=0;
    for(const id of ['cb-vu-rx','cb-vu-tx']){
      $(id).replaceChildren(...Array.from({length:12},()=>document.createElement('i')));
    }
  }
  bind(){
    $('cb-tab-easy').addEventListener('click',()=>this.setTab('easy'));
    $('cb-tab-advanced').addEventListener('click',()=>this.setTab('advanced'));
    $('cb-easy-call').addEventListener('click',()=>this.c.startAs('B'));
    $('cb-easy-answer').addEventListener('click',()=>this.c.startAs('A'));
    $('cb-easy-hangup').addEventListener('click',()=>this.c.endSession());
    $('cb-easy-port').addEventListener('click',()=>{
      if(this.c.port)void this.c.disconnect();
      else void this.c.connect();
    });
    $('cb-easy-channel').addEventListener('click',()=>this.cycleChannel(1));
    $('cb-easy-channel').addEventListener('contextmenu',event=>{event.preventDefault();this.cycleChannel(-1);});
    $('cb-easy-channel').addEventListener('wheel',event=>{event.preventDefault();this.cycleChannel(event.deltaY>0?-1:1);},{passive:false});
    $('cb-easy-vol').addEventListener('click',()=>this.adjustVolume(5));
    $('cb-easy-vol').addEventListener('wheel',event=>{event.preventDefault();this.adjustVolume(event.deltaY>0?-5:5);},{passive:false});
    $('cb-easy-mic-dial').addEventListener('click',()=>this.adjustMicGain(1));
    $('cb-easy-mic-dial').addEventListener('wheel',event=>{event.preventDefault();this.adjustMicGain(event.deltaY>0?-1:1);},{passive:false});
    $('cb-easy-fine').addEventListener('click',()=>this.openSettings('frequency'));
    $('cb-easy-mode').addEventListener('click',()=>this.openSettings('mode'));
    $('cb-easy-gain').addEventListener('click',()=>this.openSettings('gain'));
    $('cb-easy-power').addEventListener('click',()=>this.openSettings('power'));
    $('cb-easy-audio').addEventListener('click',()=>{void this.openSettings('audio');});
    $('cb-easy-settings-close').addEventListener('click',()=>this.closeSettings());
    $('frequency').addEventListener('input',()=>this.update());
    $('txPower').addEventListener('input',()=>this.update());
    $('companding').addEventListener('change',()=>this.update());
    $('volume').addEventListener('input',()=>this.update());
    window.addEventListener('blur',()=>{if(this.activeKey)this.releaseDtmf(this.activeKey);});
    for(const button of document.querySelectorAll('[data-dtmf-key]')){
      const key=button.dataset.dtmfKey;
      button.addEventListener('pointerdown',event=>{
        event.preventDefault();
        try{button.setPointerCapture(event.pointerId);}catch(_){}
        this.pressDtmf(key);
      });
      button.addEventListener('pointerup',()=>this.releaseDtmf(key));
      button.addEventListener('pointercancel',()=>this.releaseDtmf(key));
      button.addEventListener('lostpointercapture',()=>this.releaseDtmf(key));
      button.addEventListener('keydown',event=>{
        if(event.repeat)return;
        if(event.code==='Space'||event.code==='Enter'){event.preventDefault();this.pressDtmf(key);}
      });
      button.addEventListener('keyup',event=>{
        if(event.code==='Space'||event.code==='Enter'){event.preventDefault();this.releaseDtmf(key);}
      });
    }
    this.update();
  }
  setTab(name){
    this.activeTab=name;
    const easy=name==='easy';
    $('cb-easy-panel').hidden=!easy;
    $('cb-advanced-panel').hidden=easy;
    for(const [tab,selected] of [['cb-tab-easy',easy],['cb-tab-advanced',!easy]]){
      $(tab).setAttribute('aria-selected',String(selected));
      $(tab).tabIndex=selected?0:-1;
    }
  }
  writeStatus(message,error=false){
    $('cb-easy-status').textContent=message;
    $('cb-easy-status').dataset.error=String(error);
  }
  showDetail(message,ms=1800){
    $('cb-lcd-detail').textContent=message;
    this.detailUntil=performance.now()+ms;
  }
  cycleChannel(dir){
    if(this.c.port||this.c.busy){this.showDetail('DISCONNECT TO TUNE');return;}
    const old=channelFor($('frequency').value);
    const idx=CHANNELS.findIndex(channel=>channel===old);
    const next=CHANNELS[(idx<0?(dir>0?0:CHANNELS.length-1):(idx+dir+CHANNELS.length)%CHANNELS.length)];
    $('frequency').value=String(next.khz);
    $('frequency').dispatchEvent(new Event('input',{bubbles:true}));
    this.showDetail(`CHANNEL ${next.name}`);
  }
  adjustVolume(step){
    const control=$('volume');
    control.value=String(clamp(+control.value+step,0,100));
    control.dispatchEvent(new Event('input',{bubbles:true}));
    this.c.audio.setVolume(+control.value);
    this.showDetail(`SPEAKER ${control.value}%`);
  }
  adjustMicGain(step){
    this.setMicGain(this.gainDb+step);
    this.showDetail(`MIC ${this.gainDb>=0?'+':''}${this.gainDb} dB`);
  }
  setMicGain(value){
    this.gainDb=clamp(Math.round(Number(value)||0),-18,18);
    this.applyMicGain();
    $('cb-dtmf-mic-gain').textContent=`${this.gainDb} dB`;
    const range=$('cb-mic-range'),field=$('cb-mic-db');
    if(range)range.value=String(this.gainDb);
    if(field)field.value=String(this.gainDb);
  }
  applyMicGain(){
    // The existing VoiceDsp exposes txGain and uses it on every render call.
    // Only this live multiplier changes; no DSP or AudioWorklet source is edited.
    const dsp=this.c.dsp;
    if(dsp){
      const base=dsp.audioMode==='voice'?1.15:1;
      dsp.txGain=base*dbGain(this.gainDb);
    }
  }
  update(){
    const role=this.c.role;
    const inCall=role==='A'||role==='B';
    const connected=!!this.c.port;
    const busy=this.c.busy;
    const scan=role==='L';
    const mode=role==='A'?'MASTER':role==='B'?'SLAVE':scan?'READY':'OFF';
    $('cb-face').dataset.mode=role==='A'?'master':role==='B'?'slave':scan?'rx':'off';
    $('cb-lcd-mode').textContent=mode;
    $('cb-lcd-profile').textContent={off:'LIN',medium:'MU63',strong:'MU255'}[$('companding').value]||'MU63';
    const frequency=Number($('frequency').value),channel=channelFor(frequency);
    $('cb-lcd-channel').textContent=channel?.name||'--';
    $('cb-lcd-frequency').textContent=Number.isFinite(frequency)?
      (frequency/1000).toLocaleString('en-CA',{minimumFractionDigits:3,maximumFractionDigits:3})+' MHz':'— MHz';
    if(performance.now()>this.detailUntil)$('cb-lcd-detail').textContent='';
    $('cb-easy-call').disabled=busy||!connected||inCall;
    $('cb-easy-answer').disabled=busy||!connected||inCall;
    $('cb-easy-hangup').disabled=busy||!inCall;
    $('cb-easy-port').title=connected?'Disconnect ESP8266':'Connect ESP8266 serial port';
    $('cb-easy-port').setAttribute('aria-label',connected?'Disconnect serial port':'Connect serial port');
    $('cb-dtmf-speaker').textContent=$('volume').value+'%';
    $('cb-dtmf-mic-gain').textContent=this.gainDb+' dB';
    $('cb-easy-power').title=`TX POWER — APWR ${$('txPower').value} (0 maximum, 191 minimum)`;
    if(!connected)this.writeStatus('Press PORT to connect the ESP8266.');
    else if(busy)this.writeStatus('Switching radio...');
    else if(role==='A')this.writeStatus('MASTER — Full duplex · waiting for a Slave speaker.');
    else if(role==='B')this.writeStatus('SLAVE — Full duplex · both microphones are active.');
    else this.writeStatus('READY — Listen for a Master, then CALL to join; or ANSWER to start one.');
    this.renderVu('cb-vu-tx',inCall?this.txLevel:0);
    this.renderVu('cb-vu-rx',connected?this.rxLevel:0);
    const quality=connected?clamp(this.signalQuality,0,1):0;
    $('cb-lcd-meter').style.setProperty('--cb-bars',`${Math.round(quality*100)}%`);
    $('cb-lcd-meter').title='Indicative TDD marker/synchronization quality — not a calibrated RF S-meter';
    this.renderDtmfPressed();
  }
  renderVu(id,amplitude){
    const db=dBFS(amplitude),count=amplitude>.002?clamp(Math.floor((db+36)/3),0,12):0;
    const bar=$(id);let k=0;
    for(const child of bar.children)child.classList.toggle('is-lit',k++<count);
    bar.title=`${db.toFixed(1)} dBFS (estimated)`;
  }
  onMic(samples){
    if(!samples?.length)return;
    let sum=0;for(let i=0;i<samples.length;i++)sum+=samples[i]*samples[i];
    const gain=this.c.dsp?.txGain??dbGain(this.gainDb);
    this.txLevel=this.txLevel*.78+clamp(Math.sqrt(sum/samples.length)*gain,0,1)*.22;
  }
  onRadio(samples,flags){
    if(!samples?.length)return;
    let sum=0,used=0;
    const shape=scope.CBDuplexSignalShaping;
    const mode=$('companding').value;
    for(let i=0;i<samples.length;i++){
      if(samples[i]===-128)continue;
      const v=shape.decode(samples[i],mode);sum+=v*v;used++;
    }
    const level=used?Math.sqrt(sum/used):0;
    this.rxLevel=.82*this.rxLevel+.18*level;
    const good=!!(flags&1);
    this.signalQuality=.9*this.signalQuality+(good?0.1:0);
    this.signalFrames++;
  }
  closeSettings(){ $('cb-easy-settings').hidden=true; }
  async openSettings(kind){
    const content=$('cb-easy-settings-body');
    const message=$('cb-easy-settings-msg');
    content.replaceChildren();message.textContent='';
    $('cb-easy-settings').hidden=false;
    const titles={mode:'RX MODE — On-air amplitude encoding',gain:'MIC GAIN — Live adjustment',power:'TX POWER — APWR',audio:'AUDIO — Speaker & Microphone',frequency:'RF Frequency'};
    $('cb-easy-settings-title').textContent=titles[kind]||'Settings';
    const label=(text,id)=>{const lbl=document.createElement('label');const title=document.createElement('span');title.textContent=text;lbl.appendChild(title);return {lbl,id};};
    const note=text=>{message.textContent=text;};
    const addHelp=text=>{const el=document.createElement('p');el.className='cb-field-help';el.textContent=text;content.appendChild(el);};
    if(kind==='mode'){
      const wrap=label('On-air amplitude encoding','cb-air-select');
      const select=document.createElement('select');select.id=wrap.id;
      for(const [value,title] of [['off','Off — Linear PCM8'],['medium','Medium — reversible µ-law 63'],['strong','Strong — reversible µ-law 255']]){
        select.add(new Option(title,value));
      }
      select.value=$('companding').value;
      select.disabled=!!this.c.port;
      select.addEventListener('change',()=>{
        $('companding').value=select.value;
        $('companding').dispatchEvent(new Event('change',{bubbles:true}));
        this.showDetail(select.value.toUpperCase());
      });
      wrap.lbl.append(select);content.appendChild(wrap.lbl);
      addHelp('All stations must use the same encoding. Select it before connecting the serial port.');
      if(select.disabled)note('Disconnect before changing on-air encoding.');
    }
    if(kind==='gain'){
      const wrap=label('Microphone gain (−18 to +18 dB)','cb-mic-range');
      const row=document.createElement('div');row.className='cb-field-row';
      const slider=document.createElement('input');slider.id='cb-mic-range';slider.type='range';slider.min='-18';slider.max='18';slider.step='1';slider.value=this.gainDb;
      const field=document.createElement('input');field.id='cb-mic-db';field.type='number';field.min='-18';field.max='18';field.value=this.gainDb;
      slider.addEventListener('input',()=>this.setMicGain(slider.value));
      field.addEventListener('input',()=>this.setMicGain(field.value));
      row.append(slider,field);wrap.lbl.append(row);content.appendChild(wrap.lbl);
      addHelp('Applied immediately while transmitting. High gain can clip audio.');
    }
    if(kind==='power'){
      const wrap=label('APWR — 0 (maximum) to 191 (minimum)','cb-power-range');
      const row=document.createElement('div');row.className='cb-field-row';
      const slider=document.createElement('input');slider.id='cb-power-range';slider.type='range';slider.min='0';slider.max='191';slider.step='1';slider.value=$('txPower').value;
      const field=document.createElement('input');field.id='cb-power-number';field.type='number';field.min='0';field.max='191';field.step='1';field.value=$('txPower').value;
      slider.disabled=field.disabled=!!this.c.port;
      const set=v=>{
        const n=clamp(Math.round(Number(v)||0),0,191);
        slider.value=field.value=$('txPower').value=String(n);
        $('txPower').dispatchEvent(new Event('input',{bubbles:true}));
        this.showDetail(`APWR ${n}`);
      };
      slider.addEventListener('input',()=>set(slider.value));
      field.addEventListener('input',()=>set(field.value));
      row.append(slider,field);wrap.lbl.append(row);content.appendChild(wrap.lbl);
      addHelp('0 = highest RF output. 191 = lowest output. Never exceed 191.');
      if(slider.disabled)note('Disconnect to change APWR safely. The value is applied at radio START.');
    }
    if(kind==='frequency'){
      const wrap=label('RF frequency (kHz)','cb-frequency-number');
      const field=document.createElement('input');field.id=wrap.id;field.type='number';field.step='1';field.min='2400000';field.max='2483500';field.value=$('frequency').value;field.disabled=!!this.c.port;
      field.addEventListener('change',()=>{
        const n=Number(field.value);
        if(!Number.isInteger(n)||n<2400000||n>2483500){note('Frequency must be 2400000–2483500 kHz.');return;}
        $('frequency').value=String(n);$('frequency').dispatchEvent(new Event('input',{bubbles:true}));this.showDetail('FREQUENCY SET');
      });
      wrap.lbl.append(field);content.appendChild(wrap.lbl);
      addHelp('Use the large CHANNEL knob for presets 01 / 07 / 11, or enter an exact kHz frequency here. Both radios must match.');
      if(field.disabled)note('Disconnect before changing RF frequency.');
    }
    if(kind==='audio'){
      const wrap=document.createElement('div');wrap.className='cb-select-devices';
      wrap.innerHTML='<label>Speaker output<select id="cb-speaker-select"><option value="default">System default</option></select></label><label>Microphone input<select id="cb-microphone-select"><option value="default">System default</option></select></label><button type="button" id="cb-allow-audio">Allow / refresh audio devices</button>';
      content.append(wrap);
      const mic=$('cb-microphone-select'),speaker=$('cb-speaker-select');
      const populate=async()=>{
        if(!navigator.mediaDevices?.enumerateDevices){note('Audio device enumeration unavailable in this browser.');return;}
        const devices=await navigator.mediaDevices.enumerateDevices();
        for(const type of ['audioinput','audiooutput']){
          const target=type==='audioinput'?mic:speaker;
          target.replaceChildren(new Option('System default','default'));
          for(const d of devices.filter(d=>d.kind===type)){
            if(!d.deviceId||d.deviceId==='default')continue;
            target.add(new Option(d.label||`${type==='audioinput'?'Microphone':'Speaker'} ${target.options.length}`,d.deviceId));
          }
          const desired=type==='audioinput'?this.micDevice:this.speakerDevice;
          target.value=[...target.options].some(o=>o.value===desired)?desired:'default';
        }
      };
      $('cb-allow-audio').addEventListener('click',async()=>{
        try{
          const stream=await navigator.mediaDevices.getUserMedia({audio:true});
          for(const track of stream.getTracks())track.stop();
          await populate();note('Audio devices ready. Choose an output and an input.');
        }catch(e){note('Microphone permission: '+(e?.message||e));}
      });
      mic.addEventListener('change',()=>{
        if(this.c.role==='A'||this.c.role==='B'){
          note('Hang up before changing the active microphone.');mic.value=this.micDevice;return;
        }
        this.micDevice=mic.value;note('Microphone preset saved for the next call.');
      });
      speaker.addEventListener('change',async()=>{
        this.speakerDevice=speaker.value;
        try{await this.applySpeaker();note('Speaker output selected.');}
        catch(e){note(e.message+' Use the system-default speaker on this browser.');}
      });
      await populate().catch(e=>note(e.message));
      addHelp('Microphone is used only during an active call. Speaker output switching requires AudioContext.setSinkId support; otherwise the system default is used.');
    }
  }
  async applySpeaker(){
    const ctx=this.c.audio.ctx;
    if(!ctx || this.speakerDevice==='default')return;
    if(typeof ctx.setSinkId!=='function')throw Error('Browser does not support selecting the speaker output directly.');
    await ctx.setSinkId(this.speakerDevice);
  }
  async startSelectedMic(){
    const audio=this.c.audio;
    if(this.micDevice==='default')return audio.startMic();
    await audio.open();
    if(audio.stream){audio.node.port.postMessage({type:'mic',enabled:true});return;}
    if(!navigator.mediaDevices?.getUserMedia)throw Error('Microphone capture unavailable.');
    const enhance=audio.audioMode==='voice';
    const stream=await navigator.mediaDevices.getUserMedia({audio:{deviceId:{exact:this.micDevice},channelCount:1,
      echoCancellation:enhance,noiseSuppression:enhance,autoGainControl:enhance}});
    audio.captureSettings=stream.getAudioTracks()[0]?.getSettings?.()??{};
    audio.stream=stream;
    audio.micSource=audio.ctx.createMediaStreamSource(stream);
    audio.micSource.connect(audio.node);
    audio.node.port.postMessage({type:'mic',enabled:true});
  }
  /* CB-style DTMF, 12 keys: on-air while calling/answering and local monitor.
   * The UI wraps the existing PCM8 TX render buffer, leaving VoiceDsp untouched. */
  pressDtmf(key){
    if(!DTMF[key]||this.activeKey)return;
    this.activeKey=key;this.toneAt=performance.now();
    if(this.c.role==='A'||this.c.role==='B'){
      const tone={key,played:0,phase1:0,phase2:0,released:false};
      if(!this.activeTone)this.activeTone=tone;
      else if(this.keyQueue.length<16)this.keyQueue.push(tone);
    }
    this.startLocalTone(key);
    this.renderDtmfPressed();
  }
  releaseDtmf(key){
    if(this.activeKey!==key)return;
    this.activeKey=null;
    if(this.activeTone?.key===key)this.activeTone.released=true;
    else {const queued=this.keyQueue.find(t=>t.key===key&&!t.released);if(queued)queued.released=true;}
    const rest=Math.max(0,90-(performance.now()-this.toneAt));
    if(rest>0)setTimeout(()=>this.stopLocalTone(),rest);else this.stopLocalTone();
    this.renderDtmfPressed();
  }
  renderDtmfPressed(){
    for(const button of document.querySelectorAll('[data-dtmf-key]')){
      button.setAttribute('aria-pressed',String(button.dataset.dtmfKey===this.activeKey));
    }
  }
  startLocalTone(key){
    const ctx=this.c.audio.ctx;
    if(!ctx||!this.c.audio.volume)return;
    this.stopLocalTone();
    const gain=ctx.createGain();gain.gain.value=0.13;
    gain.connect(this.c.audio.volume);
    const oscillators=DTMF[key].map(freq=>{
      const osc=ctx.createOscillator();osc.frequency.value=freq;osc.connect(gain);osc.start();return osc;
    });
    this.localTone={gain,oscillators};
  }
  stopLocalTone(){
    const tone=this.localTone;
    if(!tone)return;
    try{
      tone.gain.gain.setTargetAtTime(0,tone.gain.context.currentTime,.003);
      tone.oscillators.forEach(osc=>osc.stop(tone.gain.context.currentTime+.025));
      setTimeout(()=>{tone.oscillators.forEach(osc=>{try{osc.disconnect();}catch(_){}});try{tone.gain.disconnect();}catch(_){}},80);
    }catch(_){}
    this.localTone=null;
  }
  cancelDtmf(){
    this.activeKey=null;this.activeTone=null;this.keyQueue.length=0;this.gapSamples=0;
    this.stopLocalTone();this.renderDtmfPressed();
  }
  mixDtmf(samples,mode,rate){
    if(!(samples instanceof Int8Array)||!(rate>0))return samples;
    const shaping=scope.CBDuplexSignalShaping;
    for(let i=0;i<samples.length;i++){
      if(this.gapSamples>0){this.gapSamples--;continue;}
      let tone=this.activeTone;
      if(!tone){if(!this.keyQueue.length)break;tone=this.activeTone=this.keyQueue.shift();}
      const f=DTMF[tone.key];
      const v=.16*(Math.sin(tone.phase1)+Math.sin(tone.phase2))*.5;
      tone.phase1+=2*Math.PI*f[0]/rate;
      tone.phase2+=2*Math.PI*f[1]/rate;
      if(tone.phase1>2*Math.PI)tone.phase1-=2*Math.PI;
      if(tone.phase2>2*Math.PI)tone.phase2-=2*Math.PI;
      samples[i]=shaping.encode(v,mode);
      tone.played++;
      if(tone.released && tone.played>=Math.ceil(rate*.09)){
        this.activeTone=null;
        this.gapSamples=Math.ceil(rate*.05);
      }
    }
    return samples;
  }
}
scope.CBDuplexFacade=CBFacade;
})(globalThis);
