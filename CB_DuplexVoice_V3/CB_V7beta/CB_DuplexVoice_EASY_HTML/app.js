/* CB Duplex Voice — serial protocol and prototype session UI.
 * START L = passive listening, START A = session clock Master,
 * START B = complementary Slave TX/RX. Original DPX1 packet/CRC framing.
 */
(() => {
'use strict';
const el=id=>document.getElementById(id);
const enc=new TextEncoder();
const dec=new TextDecoder();
const FRAME=146, TELEMETRY_FRAME=54, BAUD=921600, STOP_BITS=2, UART_PACED_BITS=12, PAYLOAD=128;
const TX_AHEAD_MS=24;
const AUTO_RF_GAIN=7;
const AUDIO_MODE_NAMES={voice:'Voice',music:'Music / Raw',signal:'Signal linear'};
function addressCalibration(){
  const search=globalThis.location?.search;
  if(!search)return null;
  const params=new URLSearchParams(search);
  const value=params.get('esp8266Correction');
  if(value===null||!/^-?\d{1,3}$/.test(value)||Math.abs(Number(value))>100)return null;
  const bridge=params.get('esp8266Usb')?.match(/^([\da-f]{4}):([\da-f]{4})$/i);
  return {offset:Number(value),filters:bridge?[{usbVendorId:parseInt(bridge[1],16),usbProductId:parseInt(bridge[2],16)}]:null};
}
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
const telemetryFields=['rxIndex','slotsTx','slotsRx','misses','uartErrors','rxDrops','txOverflow',
  'iqNearRail','iqObservations','scanNearRail','scanObservations'];
const telemetryExpiryMs=1800;
const peerEvidenceMs=750;
const legacyPeerWindowMs=120;
const pcmWindowMs=250;
const pcmNearFullScale=124;
const pcmWarningShare=.20;
const afcSteps=[0,48,-48,24,-24,72,-72,96,-96];
const afcRetryMs=20000;
const afcIdleRetryMs=60000;
const afcProbeMs=1350;
const afcLostMs=1750;
const afcRecoveredMs=4000;
const rfMinimum=2400000,rfMaximum=2483500;
const masterPeerLabels=Object.freeze({
  waiting:{state:'waiting',text:'Waiting for a Slave speaker',slot:'Awaiting confirmation',
    instructions:'Waiting for a Slave speaker to join.'},
  present:{state:'present',text:'Slave speaker confirmed',slot:'Reply slot occupied',
    instructions:'Slave speaker confirmed. Both microphones can speak at once.'},
  lost:{state:'lost',text:'Slave link lost',slot:'Reply slot unconfirmed',
    instructions:'Slave reception was lost. Waiting for confirmed reply slots.'}
});
const share=(part,total)=>total>0&&part<=total?part/total:null;
const percent=n=>n===null?'—':(100*n).toFixed(n&&n<.1?1:0)+'%';
function level(code,text,detail=''){return {code,text,detail};}
function describeMic(mic,active){
  if(!active)return level('idle','Microphone idle');
  if(!mic)return level('idle','Waiting for microphone');
  if(mic.rawNearRail)return level('warn','Mic input near full scale','Lower the microphone device level.');
  if(mic.peak>=1)return level('warn','MIC GAIN can clip','Lower MIC GAIN.');
  if(mic.silent)return level('idle','Mic silence / near silence','Speak or play a reference at the microphone.');
  if(mic.lowStable)return level('warn','Low mic level · '+mic.rmsDb+' dBFS RMS',
    'Raise MIC GAIN gradually while playing a reference.');
  return level('good','Peak '+mic.peakDb+' dBFS');
}
function describeRf(telemetry,role,automatic=false){
  if(role==='off')return level('idle','Receiver idle');
  if(!telemetry)return level('idle','RF telemetry unavailable');
  const {latest,recent,scanRailReports}=telemetry;
  const confirmed=recent?.iqObservations>=8;
  const near=confirmed?share(recent.iqNearRail,recent.iqObservations):null;
  const peak=latest.iqPeakPermille<=1000?latest.iqPeakPermille:0;
  if(confirmed&&(near>=.01||peak>=980)){
    const action=automatic?'Ask':'Lower local RX gain or ask';
    return level('warn','RX IQ near rail',action+' the remote station to raise APWR (less TX power).');
  }
  if(!confirmed&&scanRailReports>=2){
    return level('warn','RX near rail while searching',automatic?'Check nearby RF sources.':
      'Lower local RX gain and check nearby RF sources.');
  }
  if(!latest.locked)return level('idle','No confirmed RF lock','A peer level cannot be estimated while searching.');
  if(!confirmed||!peak)return level('idle','Waiting for confirmed IQ');
  if(peak<180)return level('warn','Low confirmed IQ level','Check antennas and tuning before changing gain.');
  return level('good','IQ peak '+percent(peak/1000)+' of full scale');
}
function describeRecoveredPcm(pcm,role){
  if(role==='off')return level('idle','RX PCM idle');
  if(!pcm||pcm.valid<PAYLOAD)return level('idle','RX PCM · waiting for valid samples');
  const text='RX PCM · '+percent(pcm.nearShare)+' near full scale';
  if(pcm.sustained)return level('warn',text,'Recovered PCM after demodulation, before playback processing.');
  return level(pcm.nearShare>=pcmWarningShare?'idle':'good',text);
}
function confirmedSlotWindow(telemetry){
  const recent=telemetry?.recent;
  return !!(telemetry?.latest.locked&&recent?.slotsRx>=3&&
    share(recent.slotsRx,recent.slotsRx+recent.misses)>=.02);
}
function assessCarrier(before,after,passive){
  const span=after.at-before.at;
  if(span<350)return null;
  const delta=key=>(after.latest[key]-before.latest[key])>>>0;
  const slots=delta('slotsRx'),misses=delta('misses');
  const expected=(slots+misses)/(passive&&!after.latest.otherActive?2:1);
  const coverage=expected>=24?Math.min(1,slots/expected):0;
  const valid=after.valid-before.valid,near=after.near-before.near;
  const rail=share(near,valid);
  const imbalance=Math.abs(2*(after.positive-before.positive)-near);
  const biasedRail=share(imbalance,valid);
  const transport=!!(after.errors!==before.errors||delta('uartErrors')||delta('rxDrops')||delta('txOverflow'));
  // A detuned discriminator pins the recovered signal on one side. A signal
  // with symmetric peaks can legitimately spend time near both rails.
  const usable=!transport&&after.latest.locked&&slots>=12&&coverage>=.10&&valid>=4*PAYLOAD&&biasedRail<.20;
  return {slots,coverage,rail,transport,usable,healthy:usable&&coverage>=.48&&biasedRail<.12,
    lost:!transport&&(!after.latest.locked||slots<6||coverage<.06||(valid>=4*PAYLOAD&&biasedRail>=.35)),
    score:usable?coverage-biasedRail:-Infinity,reply:after.latest.otherActive};
}
function describeSlots(telemetry,role,passive){
  if(role==='off')return level('idle','Slots idle');
  if(role==='L'||telemetry?.latest.listener){
    const master=passive?.master??confirmedSlotWindow(telemetry);
    const reply=passive?.reply??telemetry?.latest.otherActive;
    if(!master)return telemetry?.latest.locked?
      level('idle','Listening · checking slot reference'):level('idle','Listening · no slot reference');
    return reply?level('good','Both slot markers heard'):
      level('good','TDD traffic confirmed · reply slot open');
  }
  if(!telemetry)return level('idle','Slot telemetry unavailable');
  const {latest,recent,peerEstablished,measured}=telemetry;
  if(!latest.locked)return level('idle',peerEstablished?'Peer lock lost':'Waiting for slot reference');
  if(!peerEstablished)return level('idle',role==='A'?'Waiting for Slave':'Waiting for Master');
  if(!measured||!recent||!(recent.slotsRx+recent.misses))return level('idle','Measuring confirmed slots');
  const missed=recent.misses,total=recent.slotsRx+missed;
  return level(missed?'warn':'good',formatNumber(missed)+' / '+formatNumber(total)+' slots missing');
}
function describeTransport(session,telemetry){
  if(telemetry?.latest.uartErrors)return level('warn','Board UART errors','Check the USB–UART connection.');
  if(telemetry?.latest.rxDrops)return level('warn','Board RX queue drops','Check the USB–UART connection and serial load.');
  if(telemetry?.latest.txOverflow)return level('warn','Board TX queue overflow');
  if(session?.loss||session?.crcErrors)return level('warn','DPX1 loss or CRC rejects');
  if(session?.telemetryCrcErrors)return level('warn','DPT1 CRC rejects');
  if(!session?.frames)return level('idle','Waiting for audio frames');
  return level('good',telemetry?'Transport clear':'DPX1 clear · board data unavailable');
}
class SerialSession {
  constructor(port,role,config,{onReady,onPacket,onTelemetry,onFatal,onLine}={}){
    this.port=port;this.role=role;this.config=config;
    this.onReady=onReady??(()=>{});this.onPacket=onPacket??(()=>{});
    this.onTelemetry=onTelemetry??(()=>{});
    this.onFatal=onFatal??(()=>{});this.onLineText=onLine??(()=>{});
    this.ready=false;this.stopping=false;this.closing=false;this.ended=false;
    this.line=[];this.buffer=new Uint8Array(0);this.rejectedPrefix=0;
    this.readyResolve=null;this.readyReject=null;this.endResolve=null;
    this.statusResolve=null;this.statusReject=null;
    this.model=null;this.modelResolve=null;this.startRequested=false;
    this.expected=null;this.nextIndex=null;this.loss=0;this.rfErased=0;this.frames=0;this.crcErrors=0;
    this.recoveredPcm=null;this.recoveredPcmWindow=null;this.recoveredPcmHighWindows=0;
    this.carrierReports=[];this.carrierValid=0;this.carrierNear=0;this.carrierPositive=0;
    this.telemetry=null;this.telemetryFrames=0;this.telemetryCrcErrors=0;
    this.legacyPeerWindow=null;this.legacyPeerAt=-Infinity;this.passiveReplyAt=-Infinity;
    this.masterPeerAt=-Infinity;
    this.txRequested=0;this.consumed=0;this.consumedLow=0;this.lastCredit=null;
    this.pendingRender=false;this.writeChain=Promise.resolve();this.lastPacket=null;
  }
  async start(){
    this.statusPromise=new Promise((resolve,reject)=>{this.statusResolve=resolve;this.statusReject=reject;});
    this.readyPromise=new Promise((resolve,reject)=>{this.readyResolve=resolve;this.readyReject=reject;});
    this.endPromise=new Promise(resolve=>this.endResolve=resolve);
    const modelPromise=new Promise(resolve=>{this.modelResolve=resolve;});
    this.statusPromise.catch(()=>{});this.readyPromise.catch(()=>{});
    this.writer=this.port.writable.getWriter();
    this.reader=this.port.readable.getReader();
    this.readTask=this.readLoop();
    // Flush a stale RF stream left by a previous browser close.
    await this.send(Uint8Array.of(0x80,10));
    await wait(120);
    for(let attempt=0;attempt<10;attempt++){
      await this.send(enc.encode('STATUS\n'));
      const ok=await Promise.race([this.statusPromise,wait(350).then(()=>false)]);
      if(ok)break;
      if(attempt===9)throw Error('No CB V6 radio detected. Check the board firmware and the external 921600-baud UART.');
    }
    await this.send(enc.encode('HWINFO\n'));
    this.model=await Promise.race([modelPromise,wait(350).then(()=>null)]);
    const c=this.config;
    if(c.expectedModel&&this.model!==c.expectedModel){
      throw Error(`This calibrated page requires an ESP8266. Detected ${this.model??'an unidentified radio'}.`);
    }
    const offset=this.model==='ESP8266'?(c.esp8266Correction??0):0;
    const frequency=c.radioFrequency??c.frequency+offset;
    if(frequency<rfMinimum||frequency>rfMaximum)throw Error('The tuned frequency is outside the 2.4 GHz range.');
    this.radioFrequency=frequency;
    this.seedFrequency=c.frequency+offset;
    const fields=[this.role,c.data,c.guard,frequency,c.power,c.rfGain,c.vga,0,c.ifK,c.bbGain];
    this.startRequested=true;
    await this.send(enc.encode('START '+fields.join(' ')+'\n'));
    await timeout(this.readyPromise,5000,'The radio did not reply READY.');
    if(this.role!=='L')this.pump();
    this.watchdog=setInterval(()=>{
      if(this.stopping||this.closing||!this.ready)return;
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
    }catch(error){if(!this.stopping&&!this.closing)this.onFatal(error);return;}
    finally{
      try{this.reader.releaseLock();}catch(_){}
      this.reader=null;
    }
    if(!this.stopping&&!this.closing)this.onFatal(Error('Serial connection was interrupted.'));
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
    let i=0,rejectedUntil=this.rejectedPrefix;
    while(i+3<b.length){
      if(i>=rejectedUntil&&b[i]===69&&b[i+1]===78&&b[i+2]===68&&b[i+3]===32){
        let end=i+4;
        while(end<b.length&&end-i<200&&
          (b[end]===32||b[end]===13||(b[end]>=48&&b[end]<=57)))end++;
        if(end===b.length&&end-i<200)break;
        if(b[end]===10){
          this.handleLine(dec.decode(b.subarray(i,end)).trim());i=end+1;continue;
        }
      }
      if(b[i]!==68||b[i+1]!==80||b[i+3]!==49){i++;continue;}
      if(b[i+2]===84){
        if(i+TELEMETRY_FRAME>b.length)break;
        const view=new DataView(b.buffer,b.byteOffset+i,TELEMETRY_FRAME);
        if(view.getUint16(52,true)!==crc16(b,i+4,i+52)){
          // Search for a CRC-valid boundary inside a truncated packet, but
          // do not count its payload magic as another transport error.
          if(i>=rejectedUntil){this.telemetryCrcErrors++;rejectedUntil=i+TELEMETRY_FRAME;}
          i++;continue;
        }
        rejectedUntil=0;
        this.handleTelemetry(view);i+=TELEMETRY_FRAME;continue;
      }
      if(b[i+2]!==88){i++;continue;}
      if(i+FRAME>b.length)break;
      const view=new DataView(b.buffer,b.byteOffset+i,FRAME);
      if(view.getUint16(12,true)!==128 || view.getUint16(144,true)!==crc16(b,i+4,i+144)){
        if(i>=rejectedUntil){this.crcErrors++;rejectedUntil=i+FRAME;}
        i++;continue;
      }
      rejectedUntil=0;
      const first=view.getUint32(4,true),used=view.getUint32(8,true),flags=view.getUint16(14,true);
      const copied=new Int8Array(b.subarray(i+16,i+144));
      this.handlePacket(first,used,flags,copied);i+=FRAME;
    }
    const keep=b.length-i>4096?b.length-4:i;
    this.buffer=b.slice(keep);
    this.rejectedPrefix=Math.max(0,rejectedUntil-keep);
  }
  handleTelemetry(view){
    const latest={};
    telemetryFields.forEach((field,i)=>{latest[field]=view.getUint32(4+4*i,true);});
    latest.iqPeakPermille=view.getUint16(48,true);
    const flags=view.getUint16(50,true);
    latest.locked=!!(flags&1);latest.peerSeen=!!(flags&2);
    latest.otherActive=!!(flags&4);latest.listener=!!(flags&8);
    const now=performance.now();
    const before=this.telemetry;
    let recent=null;
    if(before&&now-before.at<=telemetryExpiryMs){
      recent=Object.fromEntries(telemetryFields.map(key=>[key,(latest[key]-before.latest[key])>>>0]));
      if(Object.values(recent).some(delta=>delta>0x7fffffff))recent=null;
    }
    const confirmed=latest.locked&&latest.peerSeen&&!latest.listener;
    const scanningNearRail=recent?.iqObservations<8&&recent.scanObservations>=16&&recent.scanNearRail>=4&&
      share(recent.scanNearRail,recent.scanObservations)>=.02;
    this.telemetry={latest,recent,at:now,
      peerEstablished:!!(confirmed||before?.peerEstablished),
      measured:!!(recent&&before?.peerEstablished&&before.latest.locked&&latest.locked),
      scanRailReports:scanningNearRail?1+(before?.scanRailReports||0):0};
    if(this.role==='A'&&confirmedSlotWindow(this.telemetry))this.masterPeerAt=now;
    this.telemetryFrames++;
    this.carrierReports.push({at:now,latest,valid:this.carrierValid,near:this.carrierNear,positive:this.carrierPositive,
      errors:this.loss+this.crcErrors+this.telemetryCrcErrors});
    if(this.carrierReports.length>4)this.carrierReports.shift();
    this.onTelemetry(this.telemetry);
  }
  carrierSignal(now=performance.now()){
    const reports=this.carrierReports,last=reports.at(-1);
    if(reports.length<3||now-last.at>peerEvidenceMs)return null;
    return assessCarrier(reports[0],last,this.role==='L');
  }
  observeLegacyPeer(index,flags,data){
    if(!(flags&1)){
      this.legacyPeerWindow=null;this.legacyPeerAt=this.passiveReplyAt=-Infinity;
      return;
    }
    // Count on the radio sample clock so a USB burst cannot combine separate
    // sightings. Old firmware may send zero for a missing slot.
    const rate=this.role==='L'?2*this.rate:this.rate;
    const span=Math.max(4*PAYLOAD,Math.round(rate*legacyPeerWindowMs/1000));
    if(!this.legacyPeerWindow||index-this.legacyPeerWindow.first>=span){
      this.legacyPeerWindow={first:index,pcm:0,markers:0};
    }
    const window=this.legacyPeerWindow;
    let count=0,available=0;
    for(const sample of data){
      if(sample===-128)continue;
      available++;
      if(sample!==0)count++;
    }
    const marker=!!(flags&2)&&available>0;
    window.pcm+=count;window.markers+=Number(marker);
    if(!(count||marker))return;
    const now=performance.now();
    // A lone marker offers at most one slot of audio and cannot set the
    // confirmed-marker bit in two successive 128-sample frame headers.
    if(window.pcm>2*this.config.data||window.markers>=2){
      this.legacyPeerAt=now;
      if(this.role==='A')this.masterPeerAt=now;
    }
    if(this.role==='L'&&(flags&4))this.passiveReplyAt=now;
  }
  passiveStatus(now){
    if(this.role!=='L')return {master:false,reply:false};
    if(this.telemetry){
      const master=now-this.telemetry.at<=peerEvidenceMs&&confirmedSlotWindow(this.telemetry);
      return {master,reply:master&&this.telemetry.latest.otherActive};
    }
    const master=now-this.legacyPeerAt<=peerEvidenceMs;
    return {master,reply:master&&now-this.passiveReplyAt<=peerEvidenceMs};
  }
  masterPeerState(now){
    if(this.role!=='A'||!Number.isFinite(this.masterPeerAt))return 'waiting';
    const locked=this.ready&&!this.ended&&(this.telemetry?.latest.locked??Number.isFinite(this.legacyPeerAt));
    return locked&&now-this.masterPeerAt<=peerEvidenceMs?'present':'lost';
  }
  handleLine(line){
    this.onLineText(line);
    if(line===`DUPLEX 1 ${BAUD} 1`)this.statusResolve(true);
    if(line===`DUPLEX 1 ${BAUD} 0`)this.statusReject(Error('Radio initialization failed. Check the board and Arduino CPU settings.'));
    const model=line.match(/^OK HWINFO CB_V6 (ESP8266|ESP32|ESP32-C5|ESP32-C6)$/);
    if(model)this.modelResolve(model[1]);
    const m=line.match(/^READY (\d+) (\d+) (\d+) (\d+)$/);
    if(m&&this.startRequested&&!this.ready&&!this.closing){
      if((this.config.logical&&Number(m[1])!==this.config.logical)||
        (this.config.data&&Number(m[2])!==this.config.data)||
        (this.config.guard&&Number(m[3])!==this.config.guard)||Number(m[4])!==12){
        this.readyReject(Error('READY does not match the requested TDD timing.'));return;
      }
      this.ready=true;this.rate=Number(m[1]);this.lastPacket=performance.now();
      this.onReady(this.rate);
      this.readyResolve(this.rate);
    }
    if(this.startRequested&&line.startsWith('ERR ')){
      this.startFailed=true;this.readyReject(Error(line));
    }
    if(/^END (?:\d+ ){14}\d+$/.test(line)){
      this.ended=true;this.endResolve();
      if(!this.stopping&&!this.closing)this.onFatal(Error('The radio ended the session.'));
    }
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
    const index=this.nextIndex===null?first:this.nextIndex+missing;
    if(missing){
      this.loss+=missing;
      // Recover sample-index continuity without allocating huge bursts.
      const count=Math.min(8192,missing);
      this.onPacket(index-count,new Int8Array(count).fill(-128),flags);
    }
    this.expected=(first+PAYLOAD)>>>0;
    this.nextIndex=index+PAYLOAD;
    let valid=0,near=0;
    for(const sample of data){
      if(sample===-128){this.rfErased++;continue;}
      valid++;
      if(Math.abs(sample)>=pcmNearFullScale){
        near++;
        if(sample>0)this.carrierPositive++;
      }
    }
    this.carrierValid+=valid;this.carrierNear+=near;
    this.observeRecoveredPcm(index,valid,near);
    if((this.role==='A'||this.role==='L')&&!this.telemetry)this.observeLegacyPeer(index,flags,data);
    this.onPacket(index,data,flags);
    if(this.role!=='L')this.pump();
  }
  observeRecoveredPcm(index,valid,near){
    if(!this.rate)return;
    const rate=this.role==='L'?2*this.rate:this.rate;
    const bucket=Math.floor(index/Math.round(rate*pcmWindowMs/1000));
    const before=this.recoveredPcmWindow;
    if(!before||bucket!==before.bucket){
      if(before&&bucket===before.bucket+1){
        const nearShare=share(before.near,before.valid);
        const high=before.valid>=PAYLOAD&&nearShare>=pcmWarningShare;
        this.recoveredPcmHighWindows=high?Math.min(2,this.recoveredPcmHighWindows+1):0;
        this.recoveredPcm={valid:before.valid,near:before.near,nearShare,
          sustained:this.recoveredPcmHighWindows>=2,at:this.lastPacket};
      }else{
        this.recoveredPcmHighWindows=0;
        this.recoveredPcm=null;
      }
      this.recoveredPcmWindow={bucket,valid:0,near:0};
    }
    this.recoveredPcmWindow.valid+=valid;
    this.recoveredPcmWindow.near+=near;
  }
  pump(){
    if(!this.ready||this.stopping||this.closing||this.ended||this.pendingRender||this.role==='L')return;
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
    if(this.stopping||this.closing)return;
    this.stopping=true;clearInterval(this.watchdog);
    let stopError;
    try{
      if(this.writer){
        await timeout(this.send(Uint8Array.of(0x80)),1200,'STOP timed out');
        if(this.startRequested&&!this.startFailed)await timeout(this.endPromise,1500,'The radio did not confirm STOP.');
      }
    }catch(error){stopError=error;}
    this.closing=true;
    try{await this.reader?.cancel();}catch(_){}
    try{await timeout(this.readTask??Promise.resolve(),1500,'Read stop timed out');}catch(_){}
    try{this.writer?.releaseLock();}catch(_){}
    this.reader=this.writer=null;this.ready=false;
    if(stopError)throw stopError;
  }
}
class Controller {
  constructor(){
    this.port=null;this.portOpen=false;this.model=null;this.session=null;this.dsp=null;this.busy=false;
    const calibration=addressCalibration();
    this.esp8266Correction=calibration?.offset??null;
    this.esp8266Filters=calibration?.filters;
    if(calibration){
      const offset=(this.esp8266Correction>0?'+':'')+this.esp8266Correction;
      document.title=`CB Duplex Voice · ESP8266 ${offset} kHz`;
      const hint=el('board-calibration');
      hint.textContent=`ESP8266 only. The automatic tuner starts at ${offset} kHz. A nonzero manual correction overrides it.`;
      hint.hidden=false;
    }
    this.role='off';
    this.afcJob=null;this.afcFrequency=null;this.afcNext=Infinity;this.afcLostAt=null;
    this.afcResumeView=null;
    this.calibrationAt=-Infinity;
    this.playbackStats=null;
    this.audio=new CBDuplexAudio(mic=>{this.dsp?.pushMic(mic);this.facade?.onMic(mic);},stats=>{
      this.playbackStats=stats;
      el('audioBuffer').textContent=Math.max(0,stats.bufferMs).toFixed(0)+' ms';
      el('underruns').textContent=formatNumber(stats.underruns);
      el('latencyTrims').textContent=formatNumber(stats.latencyTrims||0);
    });
    this.lab=new CBRadioDiagnosticEngine({onEvent:event=>this.diagUI?.event(event)});
    this.diagUI=new CBRadioDiagnosticUI(this,this.lab);
    this.lines=[];
    this.facade=new CBDuplexFacade(this);
    this.bind();this.diagUI.bind();this.facade.bind();this.draw();
  }
  log(message){
    this.lines.push(`[${new Date().toLocaleTimeString('en-CA')}] ${message}`);
    if(this.lines.length>100)this.lines.shift();
    el('log').textContent=this.lines.join('\n');el('log').scrollTop=el('log').scrollHeight;
  }
  error(e){this.log('ERROR: '+e.message);el('error').textContent=e.message;}
  readOptions(view=this.facade?.activeTab){
    const nominal=Math.round(Number(el('frequency').value));
    const correction=Number(el('frequencyCorrection').value),ifK=Number(el('ifK').value);
    const frequency=nominal+correction;
    const data=Number(el('dataSamples').value),guard=Number(el('guardSamples').value);
    const audioMode=el('audioProfile').value;
    const companding=audioMode==='signal'?'off':el('companding').value;
    const tddFilter=audioMode!=='signal'&&el('tddNotch').checked;
    const power=Number(el('txPower').value);
    const rfGain=view==='easy'?AUTO_RF_GAIN:Number(el('rfGain').value);
    const automatic=rfGain===AUTO_RF_GAIN;
    const vga=automatic?0:Number(el('vga').value),bbGain=automatic?16:Number(el('bbGain').value);
    if(!Number.isInteger(frequency)||frequency<2400000||frequency>2483500||
       !Number.isInteger(correction)||Math.abs(correction)>100||![0,1].includes(ifK)||
       !Number.isInteger(data)||data<12||data>44||data%2!==0||
       !Number.isInteger(guard)||guard<4||guard>32||
       !Number.isInteger(power)||power<0||power>191||
       !Object.hasOwn(AUDIO_MODE_NAMES,audioMode)||!['off','medium','strong'].includes(companding)||
       ![0,1,2,3,4,5,6,7].includes(rfGain)||vga<0||vga>7||bbGain<0||bbGain>32)
      throw Error('Check radio frequency, audio profile, even slot length, guard and gain settings.');
    const logical=Math.floor(120000*data/(2*(data+12+guard)));
    // The passive monitor receives BOTH directions at the full logical rate.
    // Check worst-case UART traffic including DPX1 headers and 4% headroom.
    if(2*logical*FRAME*UART_PACED_BITS>=BAUD*PAYLOAD*.96)
      throw Error('Slot configuration exceeds passive 921600-baud UART capacity (4% reserve). Use a longer guard or fewer data samples.');
    const lead=Math.max(4*PAYLOAD,Math.ceil(logical*TX_AHEAD_MS/1000));
    return {frequency,nominal,esp8266Correction:correction===0?this.esp8266Correction:null,
      expectedModel:Number.isInteger(this.esp8266Correction)?'ESP8266':null,
      automaticTuning:el('automaticTuning')?.checked===true,
      ifK,data,guard,power,rfGain,vga,bbGain,audioMode,companding,tddFilter,lead,logical};
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
      const select=el('audioProfile'),mode=select.value;
      if(this.dsp&&((mode==='signal')!==(this.dsp.audioMode==='signal'))){
        select.value=this.dsp.audioMode;
        return;
      }
      this.audio.setAudioMode(mode);
      this.dsp?.setAudioMode(mode);
      this.lab.mode=mode;
      this.log(`Audio profile selected: ${AUDIO_MODE_NAMES[mode]}.`);
      this.updateNotchStatus();
      this.draw();
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
    el('rfGain').addEventListener('change',()=>this.draw());
    window.addEventListener('pagehide',()=>{void this.disconnect();});
    document.addEventListener('keydown',event=>{
      if(event.code!=='Space'||event.repeat||this.busy||this.facade?.activeTab==='easy')return;
      if(['INPUT','SELECT','TEXTAREA','BUTTON'].includes(document.activeElement?.tagName))return;
      if(this.port&&this.role==='L'){event.preventDefault();void this.ptt();}
    });
  }
  updateTimingPreview(){
    const data=Number(el('dataSamples').value),guard=Number(el('guardSamples').value);
    const rate=Math.floor(120000*data/(2*(data+12+guard)));
    const cycles=120000/(2*(data+12+guard));
    const valid=Number.isInteger(data)&&data>=12&&data<=44&&data%2===0&&
      Number.isInteger(guard)&&guard>=4&&guard<=32&&
      2*rate*FRAME*UART_PACED_BITS<BAUD*PAYLOAD*.96;
    el('timingPreview').textContent=valid?
      `${data}/${guard} · ${formatNumber(cycles)} TX bursts/s · ${formatNumber(rate)} logical samples/s per direction.`:
      'Invalid timing or insufficient UART bandwidth. Choose a preset.';
    el('timingPreview').classList.toggle('warning',!valid);
    this.updateNotchStatus();
  }
  updateNotchStatus(){
    if(el('audioProfile').value==='signal'){
      el('notchStatus').textContent='Bypassed in Signal linear.';
      return;
    }
    const data=Number(el('dataSamples').value),guard=Number(el('guardSamples').value);
    const hz=120000/(2*(data+12+guard));
    const on=el('tddNotch').checked;
    el('notchStatus').textContent=on&&Number.isFinite(hz)?
      `Active: narrow notches at ${hz.toFixed(1)} Hz and harmonics (playback only).`:
      'Off: original un-notched playback.';
  }
  calibrationSnapshot(now=performance.now()){
    const session=this.session;
    const active=this.role==='A'||this.role==='B';
    const mic=active?this.facade?.microphoneMeasurement(now):null;
    const config=session?.ready?session.config:null;
    const receiveGain=Number.isInteger(config?.rfGain)?{mode:config.rfGain===AUTO_RF_GAIN?'auto':'manual',
      rfGain:config.rfGain,vga:config.vga,bbGain:config.bbGain}:null;
    const last=session?.telemetry;
    const ageMs=last?now-last.at:null;
    const telemetry=last&&ageMs<=telemetryExpiryMs?last:null;
    const pcm=session?.recoveredPcm;
    const recoveredPcm=pcm&&now-pcm.at<=3*pcmWindowMs?pcm:null;
    return {mic,telemetry,recoveredPcm,receiveGain,boardTotals:last?.latest??null,ageMs,telemetryFrames:session?.telemetryFrames??0,
      telemetryCrcErrors:session?.telemetryCrcErrors??0,states:{
        mic:describeMic(mic,active),
        rf:last&&!telemetry?level('idle','RF telemetry paused'):describeRf(telemetry,this.role,receiveGain?.mode==='auto'),
        pcm:describeRecoveredPcm(recoveredPcm,this.role),
        slots:describeSlots(telemetry,this.role,this.passiveStatus(now)),
        usb:session?describeTransport(session,last):level('idle','USB idle')
      }};
  }
  passiveStatus(now=performance.now()){
    return this.session?.passiveStatus?.(now)??{master:false,reply:false};
  }
  tuningStatus(){
    const session=this.session;
    const offset=session?.radioFrequency-(session?.config.nominal??session?.config.frequency);
    const adjusted=Number.isFinite(offset)?`${offset>=0?'+':''}${offset} kHz`:null;
    let state='off';
    if(this.afcResumeView||this.afcJob?.resume)state='reacquiring';
    else if(this.afcJob)state='searching';
    else if(session?.config.automaticTuning&&session.ready){
      state=this.role==='A'?'reference':session.carrierSignal()?.usable?'locked':'listening';
    }else if(session?.ready)state='manual';
    return {state,frequency:session?.radioFrequency??null,offsetKHz:adjusted?offset:null,
      text:{off:'',manual:`Manual tuning · ${adjusted}`,reference:`Master frequency reference · ${adjusted}`,
        searching:'Finding TDD carrier…',reacquiring:'Recovering the TDD link…',
        locked:`Automatic tuning locked · ${adjusted}`,listening:`Automatic tuning listening · ${adjusted}`}[state]};
  }
  async probeCarrier(job){
    const until=performance.now()+afcProbeMs;
    while(!job.cancelled){
      const signal=this.session.carrierSignal();
      if(signal||performance.now()>=until)return signal;
      await wait(75);
    }
    return null;
  }
  async findCarrier(view,job){
    const current=this.session;
    const original=current.radioFrequency,seed=current.seedFrequency;
    const candidates=[...new Set([original,...afcSteps.map(step=>seed+step)])]
      .filter(frequency=>frequency>=rfMinimum&&frequency<=rfMaximum);
    const listen=async frequency=>{
      if(this.session?.role!=='L'||this.session.radioFrequency!==frequency)
        await this.switchRole('L',view,frequency);
    };
    let best=null;
    for(const frequency of candidates){
      if(job.cancelled)break;
      await listen(frequency);
      const signal=await this.probeCarrier(job);
      if(!signal?.usable)continue;
      if(!best||signal.score>best.score+.04)best={frequency,...signal};
      if(signal.healthy)break;
    }
    const hadToReturn=best&&this.session.radioFrequency!==best.frequency;
    await listen(!job.cancelled&&best?best.frequency:original);
    if(!job.cancelled&&hadToReturn){
      const fresh=await this.probeCarrier(job);
      best=fresh?.usable?{frequency:best.frequency,...fresh}:null;
      if(!best)await listen(original);
    }
    if(job.cancelled)return null;
    const result=best;
    if(result){
      this.afcFrequency=result.frequency;
      this.log(`Automatic tuning · ${result.frequency} kHz · ${Math.round(result.coverage*100)}% slots · ${percent(result.rail)} PCM near rail`);
    }
    if(job.resume&&result&&!result.reply){
      await this.switchRole('B',view,result.frequency);
      this.afcResumeView=null;
    }
    return result;
  }
  seekCarrier(view,resume=false){
    const job={resume,cancelled:false,promise:null};
    if(resume)this.afcResumeView=view;
    this.afcJob=job;
    job.promise=this.findCarrier(view,job).then(result=>{
      const retry=resume&&this.role==='B'?afcRecoveredMs:result||resume?afcRetryMs:afcIdleRetryMs;
      this.afcNext=performance.now()+retry;
      return result;
    }).finally(()=>{
      this.afcJob=null;this.afcLostAt=null;
      this.draw();
    });
    this.draw();
    return job.promise;
  }
  async pauseCarrierSearch(){
    if(!this.afcJob)return;
    const job=this.afcJob;
    job.cancelled=true;
    await job.promise;
  }
  trackCarrier(now=performance.now()){
    const session=this.session;
    if(this.busy||this.afcJob||!session?.ready||!session.config.automaticTuning||this.role==='A')return;
    const signal=session.carrierSignal(now);
    if(!signal||signal.transport){this.afcLostAt=null;return;}
    if(this.role==='L'){
      if(!this.afcResumeView&&(signal.healthy||
        (signal.usable&&this.afcFrequency===session.radioFrequency))){
        this.afcFrequency=session.radioFrequency;return;
      }
    }else{
      if(!signal.lost){this.afcLostAt=null;return;}
      this.afcLostAt??=now;
      if(now-this.afcLostAt<afcLostMs)return;
    }
    const canResume=this.afcResumeView&&this.role==='L'&&signal.usable&&!signal.reply;
    if(now<this.afcNext&&!canResume)return;
    void this.seekCarrier(this.afcResumeView??this.facade.activeTab,this.role==='B'||!!this.afcResumeView).catch(async error=>{
      this.error(error);await this.disconnect();
    });
  }
  masterPeerStatus(now=performance.now()){
    return masterPeerLabels[this.session?.masterPeerState?.(now)??'waiting'];
  }
  updateCalibration(force=false){
    const now=performance.now();
    if(!force&&now-this.calibrationAt<150)return;
    this.calibrationAt=now;
    const {states,mic,telemetry,recoveredPcm,receiveGain,boardTotals,ageMs,telemetryFrames,telemetryCrcErrors}=this.calibrationSnapshot(now);
    const show=(id,text)=>{
      const node=el(id);
      if(node.textContent!==text)node.textContent=text;
    };
    for(const [kind,state] of Object.entries(states)){
      for(const prefix of ['cb-easy-cal-','cal-']){
        const id=prefix+kind;
        el(id).dataset.level=state.code;
        show(id,state.text);
        show(id+'-hint',state.detail);
      }
    }
    const latest=boardTotals,recent=telemetry?.recent;
    const count=value=>value===undefined?'—':formatNumber(value);
    const observations=(near,total)=>total?formatNumber(near)+' / '+formatNumber(total)+' · '+percent(share(near,total)):'—';
    const firmware=telemetry?formatNumber(telemetryFrames)+' frames · live':
      ageMs!==null?'Paused':this.session?'Waiting · older firmware may omit DPT1':'—';
    show('cb-easy-cal-rf-mode','RF receiver'+(this.model?' · '+this.model:'')+
      (receiveGain?' · '+(receiveGain.mode==='auto'?'Auto gain':'Manual gain'):''));
    for(const [id,text] of Object.entries({
      'cal-telemetry':firmware,
      'cal-rf-gain':receiveGain?(receiveGain.mode==='auto'?'Auto (7) · hardware AGC requested':
        `Manual · RF ${receiveGain.rfGain} / VGA ${receiveGain.vga} / BB ${receiveGain.bbGain}`):'—',
      'cal-mic-level':mic?mic.peakDb+' peak / '+mic.rmsDb+' RMS dBFS':'—',
      'cal-mic-raw':mic?mic.rawPeakDb+' dBFS peak · '+formatNumber(mic.rawNearRail)+' / '+formatNumber(mic.count)+' near full scale':'—',
      'cal-iq-peak':recent?.iqObservations&&latest.iqPeakPermille<=1000?
        percent(latest.iqPeakPermille/1000)+' of full scale':'—',
      'cal-iq-confirmed':observations(recent?.iqNearRail,recent?.iqObservations),
      'cal-iq-scan':observations(recent?.scanNearRail,recent?.scanObservations),
      'cal-pcm-count':observations(recoveredPcm?.near,recoveredPcm?.valid),
      'cal-slots-tx':count(latest?.slotsTx),
      'cal-slots-rx':count(latest?.slotsRx),
      'cal-slots-miss':count(latest?.misses),
      'cal-rx-index':count(latest?.rxIndex),
      'cal-uart-errors':count(latest?.uartErrors),
      'cal-rx-drops':count(latest?.rxDrops),
      'cal-tx-overflow':count(latest?.txOverflow),
      'cal-dpt-crc':count(this.session?telemetryCrcErrors:undefined)
    }))show(id,text);
  }
  draw(){
    const listening=this.role==='L',onAir=this.role==='A'||this.role==='B';
    const master=this.role==='A'?this.masterPeerStatus():null;
    const {master:remote,reply:taken}=this.passiveStatus();
    el('connection').textContent=this.port?this.busy?'Switching radio…':`${this.model??'CB V6 radio'} connected`:'Port closed';
    const tuning=el('carrier-tuning');
    if(tuning)tuning.textContent=this.tuningStatus().text;
    el('role').textContent=this.role==='off'?'OFF':this.role==='A'?'MASTER · hands-free':
      this.role==='B'?'SLAVE SPEAKER · hands-free':remote?'PASSIVE · session detected':'PASSIVE · scanning';
    el('role').dataset.role=this.role;
    el('peer-label').textContent=master?'Remote Slave':'Remote Master';
    el('peer').textContent=master?.text??(remote?'TDD carrier detected':'No remote Master detected');
    el('speaker').textContent=listening?(remote?(taken?'Reply slot occupied':'Reply slot available'):'No slot reference'):
      this.role==='B'?'This station occupies the reply slot':master?.slot??'—';
    el('connect').disabled=this.busy||!!this.port;
    el('ptt').disabled=this.busy||!this.port||onAir;
    el('ptt').textContent=remote?'PTT — Join as speaker':'PTT — Start session';
    el('endSession').disabled=this.busy||(!onAir&&!this.afcResumeView);
    el('disconnect').disabled=this.busy||!this.port;
    el('session').textContent=master?.instructions??(onAir?
      'Both microphones can speak at once. Release PTT; talk hands-free.':
      'Press PTT once to start a session, or join an existing Master.');
    for(const id of ['frequency','frequencyCorrection','automaticTuning','ifK','timingPreset','dataSamples','guardSamples','txPower','rfGain','vga','bbGain']){
      if(el(id))el(id).disabled=this.busy||!!this.port;
    }
    if(+el('rfGain').value===AUTO_RF_GAIN){el('vga').disabled=true;el('bbGain').disabled=true;}
    const profiles=el('audioProfile'),signal=profiles.value==='signal';
    profiles.disabled=this.busy||onAir;
    for(const option of profiles.options)option.disabled=!!this.port&&((option.value==='signal')!==signal);
    el('companding').disabled=this.busy||!!this.port||signal;
    el('tddNotch').disabled=signal;
    el('volume').disabled=!this.audio.ctx;
    this.diagUI?.refresh();
    this.facade?.update();
    this.updateCalibration();
  }
  async connect(view=this.facade.activeTab){
    if(this.busy||this.port)return;
    this.busy=true;this.draw();el('error').textContent='';
    try{
      if(!navigator.serial)throw Error('Web Serial requires desktop Chrome/Edge and a secure context.');
      this.port=this.esp8266Filters?await navigator.serial.requestPort({filters:this.esp8266Filters}):
        await navigator.serial.requestPort();
      await this.port.open({baudRate:BAUD,dataBits:8,stopBits:STOP_BITS,
        parity:'none',flowControl:'none',bufferSize:8192});
      this.portOpen=true;
      await this.port.setSignals({dataTerminalReady:false,requestToSend:false})
        .catch(e=>this.log('USB–UART signal controls: '+e.message));
      await this.audio.open();this.audio.setVolume(+el('volume').value);
      await this.facade.applySpeaker().catch(e=>this.log('Speaker selection: '+e.message));
      await this.switchRole('L',view);
      this.afcNext=performance.now()+2000;
      this.log('Listening. Press PTT to become Master, or to join an existing session.');
    }catch(e){this.error(e);await this.cleanup();}
    finally{this.busy=false;this.draw();}
  }
  async switchRole(role,view=this.facade.activeTab,frequency=null){
    if(this.session){const previous=this.session;this.session=null;await previous.close();}
    this.lab.cancel();
    this.audio.clear();
    this.dsp=null;
    this.facade.resetMicMeter();
    el('sync').textContent='—';
    for(const id of ['samplesLost','rfErased','crcErrors','frames'])el(id).textContent='0';
    if(role==='L')this.audio.stopMic();
    const config=this.readOptions(view);
    if(role!=='A'&&config.automaticTuning)config.radioFrequency=frequency??this.afcFrequency;
    const createDsp=rate=>new CBVoiceDsp({rate,inputRate:this.audio.inputRate,passive:role==='L',dataSamples:config.data,
      audioMode:config.audioMode,companding:config.companding,tddFilter:config.tddFilter,
      captureAheadSamples:role==='L'?0:config.lead,
      onAudio:samples=>{if(this.diagUI.muteReceived())this.audio.playGap(samples.length);else this.audio.play(samples);},
      onGap:count=>this.audio.playGap(count),
      onDiagnostics:(first,raw,filtered)=>this.lab.feedRx(first,raw,filtered)});
    this.audio.setAudioMode(config.audioMode);
    if(role!=='L'){
      this.dsp=createDsp(config.logical);
      await this.facade.startSelectedMic();
    }
    const session=new SerialSession(this.port,role,{...config,render:count=>{
      const live=this.dsp.render(count,true);
      const out=this.lab.render(count,()=>live);
      return config.audioMode==='signal'?out:this.facade.mixDtmf(out,config.companding,session.rate||30000);
    }},{
      onReady:rate=>{
        if(this.session!==session)return;
        this.model=session.model??this.model;
        this.lab.configure({rate,role,mode:config.audioMode,companding:config.companding,autoReturn:el('diagAutoReverse').checked});
        if(this.dsp?.rate!==rate)this.dsp=createDsp(rate);
        if(role!=='L')this.dsp.primeCapture();
        this.audio.setRate(rate);
        this.role=role;
        this.facade.applyMicGain();
        el('sampleRate').textContent=rate.toFixed(0)+' samples/s'+(role==='L'?' / station':'');
        this.log(`READY ${role} · ${formatNumber(rate)} audio samples/s${role==='L'?' per station (two full-rate channels)':''} · ${config.data}+${config.guard} active/guard · ${config.audioMode} audio · companding ${config.companding}`);
        this.draw();
      },
      onPacket:(index,samples,flags)=>{
        if(this.session!==session)return;
        if(!this.afcJob||role!=='L')this.dsp?.pushRadio(index,samples);
        this.facade?.onRadio(samples,flags);
        el('samplesLost').textContent=formatNumber(session.loss);
        el('rfErased').textContent=formatNumber(session.rfErased);
        el('crcErrors').textContent=formatNumber(session.crcErrors);
        el('frames').textContent=formatNumber(session.frames);
        el('sync').textContent=flags&1?'LOCKED':'SEARCHING';
        if(session.frames%6===0)this.draw();
      },
      onTelemetry:()=>{if(this.session===session)this.updateCalibration(true);},
      onFatal:e=>{if(this.session===session&&!this.busy&&!this.afcJob){this.error(e);void this.disconnect();}},
      onLine:line=>{if(line.startsWith('END '))this.log('ESP stopped: '+line.slice(0,120));}
    });
    this.session=session;
    try{await session.start();}catch(e){
      if(this.session===session)this.session=null;
      try{await session.close();}catch(stopError){this.log(stopError.message);}
      throw e;
    }
  }
  async startAs(requestedRole,view=this.facade.activeTab){
    if(this.busy||!this.port||this.role!=='L'||this.afcResumeView)return;
    if(!['A','B'].includes(requestedRole))throw Error('Invalid duplex role.');
    this.busy=true;this.draw();el('error').textContent='';
    try{
      let matched=null;
      if(requestedRole==='A')await this.pauseCarrierSearch();
      else if(this.afcJob)matched=await this.afcJob.promise;
      await wait(80+Math.floor(Math.random()*260));
      const session=this.session;
      if(requestedRole==='B'&&!matched&&session?.config.automaticTuning&&
        !(session.carrierSignal()?.usable&&this.afcFrequency===session.radioFrequency)){
        matched=await this.seekCarrier(view);
      }
      const {master:masterPresent,reply:busySpeaker}=matched?{master:true,reply:matched.reply}:this.passiveStatus();
      if(requestedRole==='A'&&masterPresent)
        throw Error('A Master is already on this channel. Use CALL to join as Slave.');
      if(requestedRole==='B'&&(!masterPresent||(session?.config.automaticTuning&&!matched&&
        !this.session?.carrierSignal()?.usable&&this.session?.telemetry)))
        throw Error('No Master detected. The remote station must press ANSWER first.');
      if(requestedRole==='B'&&busySpeaker)
        throw Error('Slave reply channel is already occupied.');
      this.log(requestedRole==='A'?'ANSWER: starting Master TDD session...':'CALL: joining existing Master as Slave...');
      await this.switchRole(requestedRole,view,matched?.frequency);
      if(requestedRole==='B')this.afcNext=performance.now();
      this.log(requestedRole==='A'?'Master ready, speaking hands-free.':'Slave active, speaking hands-free.');
    }catch(e){this.error(e);await this.returnToListen(view);}
    finally{this.busy=false;this.draw();}
  }
  async ptt(view=this.facade.activeTab){
    if(this.busy||!this.port||this.role!=='L'||this.afcResumeView)return;
    this.busy=true;this.draw();el('error').textContent='';
    try{
      // Listen-before-talk with random contention delay. This lowers the
      // probability of collision but is NOT a distributed grant protocol.
      await wait(80+Math.floor(Math.random()*260));
      const hadSignal=this.passiveStatus().master;
      const matched=this.afcJob?await this.afcJob.promise:
        hadSignal&&this.session?.config.automaticTuning&&!this.session.carrierSignal()?.usable?
          await this.seekCarrier(view):null;
      const {master:masterPresent,reply:busySpeaker}=matched?{master:true,reply:matched.reply}:this.passiveStatus();
      if(hadSignal&&!matched&&this.session?.config.automaticTuning&&this.session?.telemetry&&
        !this.session.carrierSignal()?.usable)throw Error('Could not confirm the Master frequency. Retry CALL.');
      if(masterPresent&&busySpeaker){
        throw Error('The reply slot appears occupied. Remain in passive listening mode.');
      }
      const role=masterPresent?'B':'A';
      this.log(role==='A'?'PTT: requesting Master session…':'PTT: joining the Master as the only Slave speaker…');
      await this.switchRole(role,view,matched?.frequency);
      if(role==='B')this.afcNext=performance.now();
      this.log(role==='A'?'Master active; microphone is open.':'Slave speaker active; microphone is open.');
    }catch(e){this.error(e);await this.returnToListen(view);}
    finally{this.busy=false;this.draw();}
  }
  async returnToListen(view=this.facade.activeTab){
    this.facade.cancelDtmf();
    this.audio.stopMic();
    if(!this.port)return;
    try{if(!this.session?.ready||this.session.role!=='L')await this.switchRole('L',view);}
    catch(e){this.error(e);await this.cleanup();}
  }
  async endSession(view=this.facade.activeTab){
    if(this.busy||!this.port||(this.role==='L'&&!this.afcResumeView))return;
    this.busy=true;this.draw();
    try{
      await this.pauseCarrierSearch();
      this.afcResumeView=null;
      await this.returnToListen(view);
      if(this.role==='L')this.log('Session ended; resumed passive listening.');
    }catch(e){this.error(e);await this.cleanup();}
    finally{this.busy=false;this.draw();}
  }
  async cleanup(){
    this.facade.cancelDtmf();
    const session=this.session;this.session=null;
    try{await session?.close();}catch(e){this.error(e);}
    try{if(this.portOpen)await this.port.close();}catch(e){this.error(e);}
    this.lab.cancel();this.diagUI.refresh();
    this.audio.stopMic();
    try{await this.audio.close();}catch(e){this.error(e);}
    this.port=null;this.portOpen=false;this.model=null;this.role='off';this.dsp=null;
    this.afcFrequency=null;this.afcNext=Infinity;this.afcLostAt=null;this.afcResumeView=null;
    this.facade.resetMicMeter();
    this.playbackStats=null;
    for(const id of ['sync','sampleRate'])el(id).textContent='—';
    for(const id of ['samplesLost','rfErased','crcErrors','frames','underruns','latencyTrims'])el(id).textContent='0';
    el('audioBuffer').textContent='0 ms';
  }
  async disconnect(){
    if(this.busy||!this.port)return;
    this.busy=true;this.draw();
    try{await this.pauseCarrierSearch();}
    catch(e){this.error(e);}
    try{await this.cleanup();this.log('Radio disconnected.');}
    finally{this.busy=false;this.draw();}
  }
}
const controller=new Controller();
setInterval(()=>{
  controller.trackCarrier();
  if(!controller.busy&&controller.port)controller.draw();
},600);
// Expose only diagnostic state to the developer console.
globalThis.CBDuplexVoice={status:()=>({role:controller.role,model:controller.model,frames:controller.session?.frames??0,
    lost:controller.session?.loss??0,rfErased:controller.session?.rfErased??0,crc:controller.session?.crcErrors??0,
    txAheadMs:controller.session?.rate?1000*(controller.session.txRequested-controller.session.consumed)/controller.session.rate:0,
    playback:controller.playbackStats,
    capture:controller.dsp?{missed:controller.dsp.micMissed,discarded:controller.dsp.micSamplesDiscarded}:null,
    audioMode:controller.dsp?.audioMode??el('audioProfile').value,
    companding:controller.dsp?.companding??(el('audioProfile').value==='signal'?'off':el('companding').value),
    notchEnabled:el('audioProfile').value!=='signal'&&el('tddNotch').checked,
    calibration:controller.calibrationSnapshot(),
    tuning:controller.tuningStatus(),
    timing:`${el('dataSamples').value}/${el('guardSamples').value}`,
    masterPeer:controller.role==='A'?controller.masterPeerStatus().state:null,
    facade:{tab:controller.facade.activeTab,volume:+el('volume').value,micGainDb:controller.facade.gainDb,
      channel:document.getElementById('cb-lcd-channel').textContent,dtmfKey:controller.facade.activeKey},
    diagnostic:{tx:!!controller.lab.tx,rx:!!controller.lab.rx,results:controller.lab.results.length}})};
})();
