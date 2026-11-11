/* CB Duplex Voice — serial protocol and prototype session UI.
 * START L = passive listening, START A = session clock Master,
 * START B = complementary Slave TX/RX. Original DPX1 packet/CRC framing.
 */
(() => {
'use strict';
const el=id=>document.getElementById(id);
const enc=new TextEncoder();
const dec=new TextDecoder();
const FRAME=146, BAUD=460800, STOP_BITS=2;
const crcTable=Uint16Array.from({length:256},(_,value)=>{
  let c=value<<8;
  for(let j=0;j<8;j++)c=((c<<1)^((c&0x8000)?0x1021:0))&0xffff;
  return c;
});
function crc16(data,start,end){
  let c=0xffff;
  for(let i=start;i<end;i++)c=((c<<8)^crcTable[((c>>>8)^data[i])&255])&0xffff;
  return c;
}
function timeout(p,ms,label){
  let handle;
  return Promise.race([p,new Promise((_,rej)=>handle=setTimeout(()=>rej(Error(label)),ms))])
    .finally(()=>clearTimeout(handle));
}
const wait=ms=>new Promise(r=>setTimeout(r,ms));
function formatNumber(n){return Math.round(n).toLocaleString('en-CA');}
class SerialSession {
  constructor(port,role,config,{onReady,onPacket,onFatal,onLine}={}){
    this.port=port;this.role=role;this.config=config;
    this.onReady=onReady??(()=>{});this.onPacket=onPacket??(()=>{});
    this.onFatal=onFatal??(()=>{});this.onLineText=onLine??(()=>{});
    this.ready=false;this.closing=false;this.line=[];this.buffer=new Uint8Array(0);
    this.readyResolve=null;this.readyReject=null;this.endResolve=null;
    this.statusResolve=null;this.statusReject=null;
    this.expected=null;this.loss=0;this.frames=0;this.crcErrors=0;
    this.txRequested=0;this.consumed=0;this.consumedLow=0;this.lastCredit=null;
    this.pendingRender=false;this.writeChain=Promise.resolve();this.lastPacket=null;
  }
  async start(){
    this.statusPromise=new Promise((resolve,reject)=>{this.statusResolve=resolve;this.statusReject=reject;});
    this.readyPromise=new Promise((resolve,reject)=>{this.readyResolve=resolve;this.readyReject=reject;});
    this.endPromise=new Promise(resolve=>this.endResolve=resolve);
    this.statusPromise.catch(()=>{});this.readyPromise.catch(()=>{});
    await this.port.open({baudRate:BAUD,dataBits:8,stopBits:STOP_BITS,
      parity:'none',flowControl:'none',bufferSize:8192});
    this.reader=this.port.readable.getReader();
    this.writer=this.port.writable.getWriter();
    this.readTask=this.readLoop();
    // Flush a stale RF stream left by a previous browser close.
    await this.send(Uint8Array.of(0x80,10));
    await wait(120);
    for(let attempt=0;attempt<10;attempt++){
      await this.send(enc.encode('STATUS\n'));
      const ok=await Promise.race([this.statusPromise,wait(350).then(()=>false)]);
      if(ok)break;
      if(attempt===9)throw Error('No compatible ESP8266 duplex firmware found.');
    }
    const c=this.config;
    const fields=[this.role,c.data,c.guard,c.frequency,c.power,c.rfGain,c.vga,0,0,c.bbGain];
    await this.send(enc.encode('START '+fields.join(' ')+'\n'));
    await timeout(this.readyPromise,5000,'The radio did not reply READY.');
    if(this.role!=='L')this.pump();
    this.watchdog=setInterval(()=>{
      if(this.closing||!this.ready)return;
      if(this.role!=='L'&&this.lastPacket!==null&&performance.now()-this.lastPacket>3500){
        this.onFatal(Error('The radio stopped delivering DPX1 frames.'));
      }
    },750);
  }
  send(data){
    const next=this.writeChain.then(()=>this.writer.write(data));
    this.writeChain=next.catch(()=>{});
    return next;
  }
  async readLoop(){
    try{
      while(!this.closing){
        const {value,done}=await this.reader.read();
        if(done)break;
        if(value?.length)this.read(value);
      }
    }catch(error){if(!this.closing)this.onFatal(error);}
    finally{
      try{this.reader.releaseLock();}catch(_){}
      this.reader=null;
    }
    if(!this.closing)this.onFatal(Error('Serial connection was interrupted.'));
  }
  read(data){
    let pos=0;
    while(!this.ready&&pos<data.length){
      const value=data[pos++];
      if(value===10){
        this.handleLine(dec.decode(Uint8Array.from(this.line)).trim());
        this.line=[];
      }else if(this.line.length<200)this.line.push(value);
    }
    if(pos===data.length)return;
    const rest=data.subarray(pos),b=new Uint8Array(this.buffer.length+rest.length);
    b.set(this.buffer);b.set(rest,this.buffer.length);
    let i=0;
    while(i+3<b.length){
      if(b[i]===69&&b[i+1]===78&&b[i+2]===68&&b[i+3]===32){
        const end=b.indexOf(10,i);
        if(end<0)break;
        this.handleLine(dec.decode(b.subarray(i,end)).trim());i=end+1;continue;
      }
      if(b[i]!==68||b[i+1]!==80||b[i+2]!==88||b[i+3]!==49){i++;continue;}
      if(i+FRAME>b.length)break;
      const view=new DataView(b.buffer,b.byteOffset+i,FRAME);
      if(view.getUint16(12,true)!==128 || view.getUint16(144,true)!==crc16(b,i+4,i+144)){
        this.crcErrors++;i++;continue;
      }
      const first=view.getUint32(4,true),used=view.getUint32(8,true),flags=view.getUint16(14,true);
      const copied=new Int8Array(b.subarray(i+16,i+144));
      this.handlePacket(first,used,flags,copied);i+=FRAME;
    }
    this.buffer=b.slice(i);
    if(this.buffer.length>4096)this.buffer=this.buffer.slice(-4);
  }
  handleLine(line){
    this.onLineText(line);
    if(line===`DUPLEX 1 ${BAUD} 1`)this.statusResolve(true);
    if(line===`DUPLEX 1 ${BAUD} 0`)this.statusReject(Error('The ESP8266 must run at 160 MHz.'));
    const m=line.match(/^READY (\d+) (\d+) (\d+) (\d+)$/);
    if(m&&!this.ready&&!this.closing){
      this.ready=true;this.rate=Number(m[1]);
      this.onReady(this.rate);
      this.readyResolve(this.rate);
    }
    if(line.startsWith('ERR '))this.readyReject(Error(line));
    if(line.startsWith('END '))this.endResolve();
  }
  handlePacket(first,used,flags,data){
    this.lastPacket=performance.now();this.frames++;
    const delta=(used-this.consumedLow)>>>0;
    if(delta&&delta<=0x7fffffff){
      this.consumed+=delta;this.consumedLow=used;this.lastCredit=this.lastPacket;
    }
    let missing=0;
    if(this.expected!==null){
      missing=(first-this.expected)>>>0;
      if(missing>0x7fffffff)return; // late frame
    }
    if(missing){
      this.loss+=missing;
      // Recover sample-index continuity without allocating huge bursts.
      const count=Math.min(8192,missing);
      this.onPacket(first-count,new Int8Array(count).fill(-128),flags);
    }
    this.expected=(first+128)>>>0;
    this.onPacket(first,data,flags);
    if(this.role!=='L')this.pump();
  }
  pump(){
    if(!this.ready||this.closing||this.pendingRender||this.role==='L')return;
    const count=Math.max(0,Math.min(1536,this.config.lead-(this.txRequested-this.consumed)));
    if(count<128)return;
    this.pendingRender=true;this.txRequested+=count;
    let samples;
    try{samples=this.config.render(count);}catch(error){this.pendingRender=false;this.onFatal(error);return;}
    // 0x80 is a STOP control byte in firmware; audio output must avoid it.
    for(let i=0;i<samples.length;i++)if(samples[i]===-128)samples[i]=-127;
    this.send(new Uint8Array(samples.buffer,samples.byteOffset,samples.byteLength)).then(()=>{
      this.pendingRender=false;this.pump();
    },error=>{this.pendingRender=false;this.onFatal(error);});
  }
  async close(){
    if(this.closing)return;
    this.closing=true;clearInterval(this.watchdog);
    try{
      if(this.writer){
        await timeout(this.send(Uint8Array.of(0x80)),1200,'STOP timed out').catch(()=>{});
        if(this.ready)await timeout(this.endPromise,1500,'END timed out').catch(()=>{});
      }
    }catch(_){}
    try{await this.reader?.cancel();}catch(_){}
    try{await timeout(this.readTask??Promise.resolve(),1500,'Read stop timed out');}catch(_){}
    try{this.writer?.releaseLock();}catch(_){}
    try{await this.port.close();}catch(_){}
    this.reader=this.writer=null;this.ready=false;
  }
}
class Controller {
  constructor(){
    this.port=null;this.session=null;this.dsp=null;this.busy=false;
    this.role='off';this.lastMaster=-Infinity;this.lastSpeaker=-Infinity;
    this.audio=new CBDuplexAudio(mic=>this.dsp?.pushMic(mic),stats=>{
      el('audioBuffer').textContent=Math.max(0,stats.bufferMs).toFixed(0)+' ms';
      el('underruns').textContent=formatNumber(stats.underruns);
      el('latencyTrims').textContent=formatNumber(stats.latencyTrims||0);
    });
    this.lab=new CBRadioDiagnosticEngine({onEvent:event=>this.diagUI?.event(event)});
    this.diagUI=new CBRadioDiagnosticUI(this,this.lab);
    this.lines=[];
    this.bind();this.diagUI.bind();this.draw();
  }
  log(message){
    this.lines.push(`[${new Date().toLocaleTimeString('en-CA')}] ${message}`);
    if(this.lines.length>100)this.lines.shift();
    el('log').textContent=this.lines.join('\n');el('log').scrollTop=el('log').scrollHeight;
  }
  error(e){this.log('ERROR: '+e.message);el('error').textContent=e.message;}
  readOptions(){
    const frequency=Math.round(Number(el('frequency').value));
    const data=Number(el('dataSamples').value),guard=Number(el('guardSamples').value);
    const audioMode=el('audioProfile').value;
    const companding=el('companding').value;
    const tddFilter=el('tddNotch').checked;
    const power=Number(el('txPower').value);
    const rfGain=Number(el('rfGain').value),vga=Number(el('vga').value),bbGain=Number(el('bbGain').value);
    if(!Number.isInteger(frequency)||frequency<2400000||frequency>2483500||
       !Number.isInteger(data)||data<12||data>20||data%2!==0||
       !Number.isInteger(guard)||guard<4||guard>30||
       !Number.isInteger(power)||power<0||power>191||
       !['voice','music'].includes(audioMode)||!['off','medium','strong'].includes(companding)||
       ![0,1,2,3,4,5,6,7].includes(rfGain)||vga<0||vga>7||bbGain<0||bbGain>32)
      throw Error('Check radio frequency, audio profile, even slot length, guard and gain settings.');
    const logical=Math.floor(120000*data/(2*(data+12+guard)));
    if(logical*146*12>=460800*128*.96)
      throw Error('Slot configuration leaves less than 4% UART headroom. Use a longer guard or fewer data samples.');
    return {frequency,data,guard,power,rfGain,vga,bbGain,audioMode,companding,tddFilter,lead:6144};
  }
  bind(){
    el('connect').addEventListener('click',()=>this.connect());
    el('ptt').addEventListener('click',()=>this.ptt());
    el('endSession').addEventListener('click',()=>this.endSession());
    el('disconnect').addEventListener('click',()=>this.disconnect());
    el('volume').addEventListener('input',()=>this.audio.setVolume(+el('volume').value));
    el('timingPreset').addEventListener('change',()=>{
      const choice=el('timingPreset').value;
      if(choice!=='custom'){
        const [d,g]=choice.split(':').map(Number);
        el('dataSamples').value=d;el('guardSamples').value=g;
      }
      this.updateTimingPreview();
    });
    for(const id of ['dataSamples','guardSamples'])el(id).addEventListener('input',()=>{
      const value=`${el('dataSamples').value}:${el('guardSamples').value}`;
      el('timingPreset').value=[...el('timingPreset').options].some(o=>o.value===value)?value:'custom';
      this.updateTimingPreview();
    });
    el('audioProfile').addEventListener('change',()=>{
      const mode=el('audioProfile').value;
      this.audio.setAudioMode(mode);
      this.dsp?.setAudioMode(mode);
      this.lab.mode=mode;
      this.log(`Audio profile selected: ${mode==='voice'?'Voice':'Music / Raw'}.`);
    });
    el('companding').addEventListener('change',()=>{
      // Link-level amplitudes must be encoded/decoded with the same setting.
      // Locked while a radio port is open; both peers choose this before RX.
      this.log('Air-link fidelity: '+el('companding').selectedOptions[0].textContent+'. Set the same mode on every station.');
    });
    el('tddNotch').addEventListener('change',()=>{
      this.dsp?.setTddFilter(el('tddNotch').checked);
      this.updateNotchStatus();
    });
    this.updateTimingPreview();
    this.updateNotchStatus();
    el('rfGain').addEventListener('change',()=>{
      const automatic=+el('rfGain').value===7;
      el('vga').disabled=automatic;el('bbGain').disabled=automatic;
    });
    window.addEventListener('pagehide',()=>{void this.disconnect();});
    document.addEventListener('keydown',event=>{
      if(event.code!=='Space'||event.repeat||this.busy)return;
      if(['INPUT','SELECT','TEXTAREA','BUTTON'].includes(document.activeElement?.tagName))return;
      if(this.port&&this.role==='L'){event.preventDefault();void this.ptt();}
    });
  }
  updateTimingPreview(){
    const data=Number(el('dataSamples').value),guard=Number(el('guardSamples').value);
    const rate=Math.floor(120000*data/(2*(data+12+guard)));
    const cycles=120000/(2*(data+12+guard));
    const valid=Number.isInteger(data)&&data>=12&&data<=20&&data%2===0&&
      Number.isInteger(guard)&&guard>=4&&guard<=30&&
      rate*146*12<460800*128*.96;
    el('timingPreview').textContent=valid?
      `${data}/${guard} · ${formatNumber(cycles)} TX bursts/s · ${formatNumber(rate)} logical samples/s per direction.`:
      'Invalid timing or insufficient UART bandwidth. Choose a preset.';
    el('timingPreview').classList.toggle('warning',!valid);
    this.updateNotchStatus();
  }
  updateNotchStatus(){
    const data=Number(el('dataSamples').value),guard=Number(el('guardSamples').value);
    const hz=120000/(2*(data+12+guard));
    const on=el('tddNotch').checked;
    el('notchStatus').textContent=on&&Number.isFinite(hz)?
      `Active: narrow notches at ${hz.toFixed(1)} Hz and harmonics (playback only).`:
      'Off: original un-notched playback.';
  }
  draw(){
    const listening=this.role==='L',onAir=this.role==='A'||this.role==='B';
    const remote=performance.now()-this.lastMaster<2600;
    const taken=performance.now()-this.lastSpeaker<1700;
    el('connection').textContent=this.port?this.busy?'Switching radio…':'ESP8266 connected':'Port closed';
    el('role').textContent=this.role==='off'?'OFF':this.role==='A'?'MASTER · hands-free':
      this.role==='B'?'SLAVE SPEAKER · hands-free':remote?'PASSIVE · session detected':'PASSIVE · scanning';
    el('role').dataset.role=this.role;
    el('peer').textContent=remote?'TDD carrier detected':'No remote Master detected';
    el('speaker').textContent=listening?(taken?'Reply slot occupied':'Reply slot available'):
      this.role==='B'?'This station occupies the reply slot':this.role==='A'?'Awaiting one Slave speaker':'—';
    el('connect').disabled=this.busy||!!this.port;
    el('ptt').disabled=this.busy||!this.port||onAir;
    el('ptt').textContent=remote?'PTT — Join as speaker':'PTT — Start session';
    el('endSession').disabled=this.busy||!onAir;
    el('disconnect').disabled=this.busy||!this.port;
    el('session').textContent=onAir?'Both microphones can speak at once. Release PTT; talk hands-free.':
      'Press PTT once to start a session, or join an existing Master.';
    for(const id of ['frequency','timingPreset','dataSamples','guardSamples','txPower','rfGain','vga','bbGain']){
      el(id).disabled=this.busy||!!this.port;
    }
    el('audioProfile').disabled=this.busy||onAir;
    el('companding').disabled=this.busy||!!this.port;
    el('volume').disabled=!this.audio.ctx;
    this.diagUI?.refresh();
  }
  async connect(){
    if(this.busy||this.port)return;
    this.busy=true;this.draw();el('error').textContent='';
    try{
      if(!navigator.serial)throw Error('Web Serial requires desktop Chrome/Edge and a secure context.');
      this.port=await navigator.serial.requestPort();
      await this.audio.open();this.audio.setVolume(+el('volume').value);
      await this.switchRole('L');
      this.log('Listening. Press PTT to become Master, or to join an existing session.');
    }catch(e){this.error(e);await this.cleanup();}
    finally{this.busy=false;this.draw();}
  }
  async switchRole(role){
    if(this.session){await this.session.close();this.session=null;}
    this.lab.cancel();
    this.audio.clear();
    this.dsp=null;
    if(role==='L')this.audio.stopMic();
    const config=this.readOptions();
    this.audio.setAudioMode(config.audioMode);
    if(role!=='L')await this.audio.startMic();
    const session=new SerialSession(this.port,role,{...config,render:count=>this.lab.render(count,()=>this.dsp.render(count,true))},{
      onReady:rate=>{
        if(this.session!==session)return;
        this.lab.configure({rate,role,mode:config.audioMode,companding:config.companding,autoReturn:el('diagAutoReverse').checked});
        this.dsp=new CBVoiceDsp({rate,inputRate:this.audio.inputRate,passive:role==='L',dataSamples:config.data,
          audioMode:config.audioMode,companding:config.companding,tddFilter:config.tddFilter,
          onAudio:(samples,playbackRate)=>{if(!this.diagUI.muteReceived())this.audio.play(samples);},
          onDiagnostics:(first,raw,filtered)=>this.lab.feedRx(first,raw,filtered)});
        this.audio.setRate(role==='L'?rate/2:rate);
        this.role=role;
        el('sampleRate').textContent=(rate/(role==='L'?2:1)).toFixed(0)+' samples/s';
        this.log(`READY ${role} · ${formatNumber(rate)} transport samples/s · ${config.data}+${config.guard} active/guard · ${config.audioMode} audio · companding ${config.companding}`);
        this.draw();
      },
      onPacket:(index,samples,flags)=>{
        if(this.session!==session)return;
        this.dsp?.pushRadio(index,samples);
        if(role==='L'){
          const now=performance.now();
          if(flags&1)this.lastMaster=now;
          if(flags&4)this.lastSpeaker=now;
        }
        el('samplesLost').textContent=formatNumber(session.loss);
        el('crcErrors').textContent=formatNumber(session.crcErrors);
        el('frames').textContent=formatNumber(session.frames);
        el('sync').textContent=flags&1?'LOCKED':'SEARCHING';
        if(session.frames%6===0)this.draw();
      },
      onFatal:e=>{if(this.session===session&&!this.busy){this.error(e);void this.endSession();}},
      onLine:line=>{if(line.startsWith('END '))this.log('ESP stopped: '+line.slice(0,120));}
    });
    this.session=session;
    try{await session.start();}catch(e){await session.close();if(this.session===session)this.session=null;throw e;}
  }
  async ptt(){
    if(this.busy||!this.port||this.role!=='L')return;
    this.busy=true;this.draw();el('error').textContent='';
    try{
      // Listen-before-talk with random contention delay. This lowers the
      // probability of collision but is NOT a distributed grant protocol.
      await wait(80+Math.floor(Math.random()*260));
      const masterPresent=performance.now()-this.lastMaster<2600;
      const busySpeaker=performance.now()-this.lastSpeaker<1700;
      if(masterPresent&&busySpeaker){
        throw Error('The reply slot appears occupied. Remain in passive listening mode.');
      }
      const role=masterPresent?'B':'A';
      this.log(role==='A'?'PTT: requesting Master session…':'PTT: joining the Master as the only Slave speaker…');
      await this.switchRole(role);
      this.log(role==='A'?'Master active; microphone is open.':'Slave speaker active; microphone is open.');
    }catch(e){this.error(e);await this.returnToListen();}
    finally{this.busy=false;this.draw();}
  }
  async returnToListen(){
    this.audio.stopMic();
    if(!this.port)return;
    try{await this.switchRole('L');}catch(e){this.error(e);await this.cleanup();}
  }
  async endSession(){
    if(this.busy||!this.port||this.role==='L')return;
    this.busy=true;this.draw();
    try{await this.returnToListen();this.log('Session ended; resumed passive listening.');}
    finally{this.busy=false;this.draw();}
  }
  async cleanup(){
    if(this.session){await this.session.close();this.session=null;}
    this.lab.cancel();this.diagUI.refresh();
    this.audio.stopMic();await this.audio.close();
    this.port=null;this.role='off';this.dsp=null;
    this.lastMaster=this.lastSpeaker=-Infinity;
    for(const id of ['sync','sampleRate'])el(id).textContent='—';
    for(const id of ['samplesLost','crcErrors','frames','underruns','latencyTrims'])el(id).textContent='0';
    el('audioBuffer').textContent='0 ms';
  }
  async disconnect(){
    if(this.busy||!this.port)return;
    this.busy=true;this.draw();
    try{await this.cleanup();this.log('Radio disconnected.');}
    catch(e){this.error(e);}
    finally{this.busy=false;this.draw();}
  }
}
const controller=new Controller();
setInterval(()=>{if(!controller.busy&&controller.port)controller.draw();},600);
// Expose only diagnostic state to the developer console.
globalThis.CBDuplexVoice={status:()=>({role:controller.role,frames:controller.session?.frames??0,
    lost:controller.session?.loss??0,crc:controller.session?.crcErrors??0,
    audioMode:controller.dsp?.audioMode??el('audioProfile').value,
    companding:controller.dsp?.companding??el('companding').value,notchEnabled:el('tddNotch').checked,
    timing:`${el('dataSamples').value}/${el('guardSamples').value}`,
    diagnostic:{tx:!!controller.lab.tx,rx:!!controller.lab.rx,results:controller.lab.results.length}})};
})();
