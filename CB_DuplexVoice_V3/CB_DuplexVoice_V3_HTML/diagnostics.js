/* CB Duplex Voice V3 — in-band audio-link diagnostics.
 * No firmware change: test tones use the existing signed PCM8 TDD stream.
 * The receiving browser measures RAW PCM and audio-profile-filtered PCM.
 * Important: measured dBFS and SNR are audio/baseband metrics, NOT RF dBm.
 */
(function(scope){
'use strict';
const TWO_PI=2*Math.PI;
const clamp=(v,a,b)=>Math.max(a,Math.min(b,v));
const DBFS=amp=>20*Math.log10(Math.max(amp,1e-8));
const HEADER_MS=180, HEADER_GAP_MS=250, STAGE_MS=400, STAGE_GAP_MS=100;
const SYNC=[1730,2570,1730];
const BANKS=Object.freeze({
 full: {label:'Full frequency + level + silence',tag:1100},
 frequency:{label:'Frequency response',tag:1420},
 level:{label:'Amplitude / quiet signals',tag:2110},
 noise:{label:'Silence / TDD noise',tag:2890}
});
const FREQS=[1000,80,125,250,500,750,1500,2000,3000,4000,5000,6000,8000];
const LEVELS=[-6,-12,-18,-24,-30,-36,-42];
function testStages(bank){
 if(!BANKS[bank])throw Error('Unknown diagnostic bank');
 const stages=[{kind:'silence',frequency:0,level:-Infinity,label:'Quiet RF slot'}];
 if(bank==='full'||bank==='frequency')for(const f of FREQS)stages.push({kind:'frequency',frequency:f,level:-12,label:`${f} Hz`});
 if(bank==='full'||bank==='level')for(const level of LEVELS)stages.push({kind:'level',frequency:1000,level,label:`1 kHz · ${level} dBFS`});
 if(bank==='noise')stages.push({kind:'silence',frequency:0,level:-Infinity,label:'Continued silence'});
 return stages;
}
function oscillatorSignal(frequency,rate,index){return Math.sin(TWO_PI*frequency*(index/rate));}
function toneMagnitude(samples,rate,freq){
 let re=0,im=0,sumsq=0,valid=0;
 const w=TWO_PI*freq/rate,cs=Math.cos(w),sn=Math.sin(w);
 let c=1,s=0;
 for(let i=0;i<samples.length;i++){
  const v=samples[i];if(Number.isFinite(v)){re+=v*c;im+=v*s;sumsq+=v*v;valid++;}
  const nc=c*cs-s*sn;s=s*cs+c*sn;c=nc;
  if((i&1023)===1023){const norm=Math.hypot(c,s)||1;c/=norm;s/=norm;}
 }
 return {amp:valid?2*Math.hypot(re,im)/valid:0,rms:valid?Math.sqrt(sumsq/valid):0,valid};
}
function analyzeTone(samples,rate,freq){
 const n=samples.length;
 if(n<Math.max(120,Math.floor(rate*.10)))return {frequency:null,amplitude:0,level:-120,snr:null,thd:null,valid:n};
 const rough=toneMagnitude(samples,rate,freq);
 const range=Math.max(6,freq*.018),steps=20;
 let best={amp:-1,freq};
 for(let j=0;j<=steps;j++){
  const candidate=freq+range*(j/steps*2-1);
  const amp=toneMagnitude(samples,rate,candidate).amp;
  if(amp>best.amp)best={amp,freq:candidate};
 }
 const harmonics=[];
 for(let k=2;k<=3;k++)if(best.freq*k<rate*.48)harmonics.push(toneMagnitude(samples,rate,best.freq*k).amp);
 const distortion=Math.sqrt(harmonics.reduce((s,a)=>s+a*a,0))/Math.max(best.amp,1e-7)*100;
 const desiredPower=best.amp*best.amp/2;
 const residue=Math.max(rough.rms*rough.rms-desiredPower,1e-9);
 return {frequency:best.freq,amplitude:best.amp,level:DBFS(best.amp),snr:10*Math.log10(Math.max(desiredPower,1e-9)/residue),
  thd:distortion,valid:rough.valid};
}
function analyzeStage(stage,raw,filtered,rate,erasures){
 let rawMetric,filteredMetric;
 if(stage.kind==='silence'){
  const rms=a=>Math.sqrt(a.reduce((s,v)=>s+v*v,0)/Math.max(a.length,1));
  rawMetric={frequency:null,amplitude:rms(raw),level:DBFS(rms(raw)),snr:null,thd:null};
  filteredMetric={frequency:null,amplitude:rms(filtered),level:DBFS(rms(filtered)),snr:null,thd:null};
 }else{
  rawMetric=analyzeTone(raw,rate,stage.frequency);
  filteredMetric=analyzeTone(filtered,rate,stage.frequency);
 }
 return {...stage,raw:rawMetric,filtered:filteredMetric,erasures,count:raw.length, verdict:'—',gainError:null,reason:''};
}
function evaluate(rows,mode){
 const ref=rows.find(r=>r.kind==='frequency'&&r.frequency===1000) || rows.find(r=>r.kind==='level'&&r.level===-12);
 const refLevel=ref?.raw.level??null;
 const noise=rows.find(r=>r.kind==='silence')?.raw.level??-70;
 for(const row of rows){
  if(row.kind==='silence'){
   row.verdict=row.raw.level<-28?'PASS':'WARN';
   row.reason='Measured idle-slot audio noise, not RF output power';continue;
  }
  const wanted=row.frequency;
  const inband=mode==='music' ? wanted>=80&&wanted<=4500 : wanted>=250&&wanted<=2600;
  const referenceExpected=row.kind==='level' ? row.level-(-12) : 0;
  row.gainError=refLevel==null?null:row.raw.level-refLevel-referenceExpected;
  const detectable=row.raw.level>noise+8 && row.raw.amplitude>0.0015 && row.count>0;
  const pitch=Number.isFinite(row.raw.frequency)&&Math.abs(row.raw.frequency-wanted)<Math.max(5,wanted*.023);
  if(!detectable||!pitch){row.verdict='FAIL';row.reason='Tone not distinguishable from noise or incorrect pitch';}
  else if(row.kind==='level'&&row.level<=-36){row.verdict='WARN';row.reason='Near the resolution limit of 8-bit PCM; inspect trend';}
  else if(!inband){row.verdict='BAND';row.reason='Outside the nominal voice/music passband; attenuation expected';}
  else if(row.gainError!==null&&Math.abs(row.gainError)>5){row.verdict='WARN';row.reason='More than 5 dB deviation from 1-kHz reference';}
  else if(row.raw.snr!==null&&row.raw.snr<9){row.verdict='WARN';row.reason='Tone detected with high residual noise/distortion';}
  else{row.verdict='PASS';row.reason='Tone detected and within relative level tolerance';}
  if(row.erasures>row.count*.05&&row.verdict==='PASS'){
   row.verdict='WARN';row.reason='Excessive transport erasures';
  }
 }
}
function makeWav(samples,rate){
 const out=new ArrayBuffer(44+samples.length*2),v=new DataView(out);
 const str=(p,s)=>{for(let i=0;i<s.length;i++)v.setUint8(p+i,s.charCodeAt(i));};
 str(0,'RIFF');v.setUint32(4,out.byteLength-8,true);str(8,'WAVE');str(12,'fmt ');
 v.setUint32(16,16,true);v.setUint16(20,1,true);v.setUint16(22,1,true);
 v.setUint32(24,rate,true);v.setUint32(28,rate*2,true);v.setUint16(32,2,true);v.setUint16(34,16,true);
 str(36,'data');v.setUint32(40,samples.length*2,true);
 for(let i=0;i<samples.length;i++)v.setInt16(44+2*i,Math.round(clamp(samples[i],-1,1)*32767),true);
 return new Blob([out],{type:'audio/wav'});
}
class DiagnosticEngine{
 constructor({onEvent=()=>{}}={}){
  this.onEvent=onEvent;this.rate=30000;this.role='off';this.mode='voice';this.autoReturn=false;
  this.allowSelfLoopback=false;
  this.tx=null;this.rx=null;this.results=[];this.scan=[];this.scanFirst=0;this.scanState='wait';
  this.scanCount=0;this.scanBank=null;this.scanStart=0;this.scanLast=0;
  this.samplesSeen=0;this.cooldownUntil=0;this.returnTimer=null;
 }
 configure({rate,role,mode='voice',autoReturn=false}){
  this.cancel();this.rate=rate;this.role=role;this.mode=mode;this.autoReturn=autoReturn;
 }
 emit(type,data={}){this.onEvent({type,...data});}
 startTx(bank='full',via='manual'){
  if(this.tx)throw Error('A test transmission is already running.');
  if(!BANKS[bank])throw Error('Unknown bank');
  const rate=this.rate, stages=testStages(bank),headerSamples=Math.round(rate*HEADER_MS/1000);
  const headerGap=Math.round(rate*HEADER_GAP_MS/1000),stageLen=Math.round(rate*STAGE_MS/1000),
   gapLen=Math.round(rate*STAGE_GAP_MS/1000);
  const length=4*headerSamples+headerGap+stages.length*(stageLen+gapLen);
  const data=new Int8Array(length);
  const header=[...SYNC,BANKS[bank].tag];
  for(let part=0;part<header.length;part++){
   const f=header[part];
   for(let j=0;j<headerSamples;j++){
    const ramp=Math.min(1,j/(rate*.005),(headerSamples-1-j)/(rate*.005));
    data[part*headerSamples+j]=Math.round(.36*Math.max(0,ramp)*oscillatorSignal(f,rate,j)*127);
   }
  }
  const bodyStart=4*headerSamples+headerGap;
  for(let index=0;index<stages.length;index++){
   const stage=stages[index];if(!stage.frequency)continue;
   const a=Math.pow(10,stage.level/20),offset=bodyStart+index*(stageLen+gapLen);
   for(let j=0;j<stageLen;j++){
    const ramp=Math.min(1,j/(rate*.008),(stageLen-1-j)/(rate*.008));
    const value=a*Math.max(0,ramp)*oscillatorSignal(stage.frequency,rate,j);
    data[offset+j]=Math.round(clamp(value,-.98,.98)*127);
   }
  }
  this.tx={bank,via,data,position:0};this.emit('txStart',{bank,via,seconds:length/rate});
  return length/rate;
 }
 render(count,fallback){
  const t=this.tx;
  if(!t)return fallback();
  const result=new Int8Array(count),copy=Math.min(count,t.data.length-t.position);
  result.set(t.data.subarray(t.position,t.position+copy));
  if(copy<count)result.set(fallback().subarray(copy),copy);
  t.position+=copy;
  const pct=Math.floor(100*t.position/t.data.length);
  if(pct!==t.progress&&pct%5===0){t.progress=pct;this.emit('txProgress',{percent:pct,bank:t.bank});}
  if(t.position>=t.data.length){this.tx=null;this.emit('txEnd',{bank:t.bank,via:t.via});}
  return result;
 }
 cancel(){
  if(this.returnTimer!==null)clearTimeout(this.returnTimer);this.returnTimer=null;
  this.tx=null;this.rx=null;this.scan=[];this.scanState='wait';this.scanCount=0;this.scanBank=null;
  this.cooldownUntil=0;
 }
 resetScanner(){this.scanState='wait';this.scanCount=0;this.scanBank=null;this.scanStart=0;this.scanLast=0;}
 feedRx(first,bytes,filtered){
  if(!Number.isFinite(first)||!(bytes instanceof Int8Array))return;
  // While transmitting a test, never interpret residual local RF as a remote
  // benchmark. Local DSP loopback opts into this exception explicitly.
  if(this.tx&&!this.allowSelfLoopback){this.scan=[];this.resetScanner();return;}
  if(this.scanFirst===null)this.scanFirst=first;
  for(let i=0;i<bytes.length;i++){
   const at=first+i,valid=bytes[i]!==-128,raw=valid?bytes[i]/127:NaN;
   const audio=filtered?.[i];
   if(this.rx){this._measureSample(at,raw,Number.isFinite(audio)?audio:0,valid);continue;}
   if(at<this.cooldownUntil)continue;
   // Invalid samples occupy time but contribute silence to the preamble detector.
   this.scan.push(valid?raw:0);
   const window=Math.max(160,Math.round(this.rate*.020));
   if(this.scan.length>=window){
    this._scanWindow(this.scan,at+1-window);
    this.scan=[];
   }
  }
 }
 _scanWindow(samples,start){
  const choices=[1730,2570,1100,1420,2110,2890];
  let max={freq:0,amp:0},second=0;
  let rms=0;for(const x of samples)rms+=x*x;rms=Math.sqrt(rms/samples.length);
  for(const f of choices){
   const a=toneMagnitude(samples,this.rate,f).amp;
   if(a>max.amp){second=max.amp;max={freq:f,amp:a};}
   else if(a>second)second=a;
  }
  const heard=rms>.004&&max.amp>rms*.87&&max.amp>second*1.22?max.freq:0;
  const win=samples.length;
  if(this.scanState==='wait'){
   if(heard===1730){if(++this.scanCount===1)this.scanStart=start;}
   else this.scanCount=0;
   if(this.scanCount>=4){this.scanState='second';this.scanCount=0;this.scanLast=start;}
   return;
  }
  if(start-this.scanStart>this.rate*2){this.resetScanner();return;}
  let expected=this.scanState==='second'?2570:this.scanState==='third'?1730:0;
  if(this.scanState==='bank')expected=0;
  if(this.scanState==='bank'){
   if(Object.values(BANKS).some(b=>b.tag===heard)){
    if(this.scanBank===heard)this.scanCount++;
    else{this.scanBank=heard;this.scanCount=1;}
   }else if(heard) this.scanCount=0;
   if(this.scanCount>=3){
    const bank=Object.entries(BANKS).find(([,v])=>v.tag===this.scanBank)?.[0];
    if(bank)this._beginReceive(bank,this.scanStart);
   }
   return;
  }
  if(heard===expected){this.scanCount++;}
  else if(heard && heard!==expected && heard!==(this.scanState==='second'?1730:2570)){this.resetScanner();return;}
  if(this.scanCount>=3){this.scanCount=0;this.scanState=this.scanState==='second'?'third':'bank';}
 }
 _beginReceive(bank,syncStart){
  const rate=this.rate,stages=testStages(bank);
  const begin=syncStart+Math.round((4*HEADER_MS+HEADER_GAP_MS)*rate/1000);
  this.rx={bank,stages,begin,stageLength:Math.round(rate*STAGE_MS/1000),gapLength:Math.round(rate*STAGE_GAP_MS/1000),
   lastProgress:-1,captures:stages.map(()=>({raw:[],filtered:[],erasures:0})),wav:new Float32Array(Math.min(rate*22,900000)),wavAt:0,
   windowEnd:begin+Math.round(stages.length*(STAGE_MS+STAGE_GAP_MS)*rate/1000),rate};
  this.scan=[];this.resetScanner();this.emit('rxStart',{bank,direction:this.role==='A'?'Slave → Master':this.role==='B'?'Master → Slave':'Passive reception'});
 }
 _measureSample(at,raw,filtered,valid){
  const r=this.rx;if(!r)return;
  if(at>=r.windowEnd){this._finishReceive();return;}
  if(at<r.begin)return;
  const percent=Math.floor(100*(at-r.begin)/Math.max(1,r.windowEnd-r.begin));
  if(percent!==r.lastProgress&&percent%5===0){r.lastProgress=percent;this.emit('rxProgress',{percent,bank:r.bank});}
  const relative=at-r.begin,span=r.stageLength+r.gapLength,index=Math.floor(relative/span);
  if(index>=r.stages.length)return;
  const position=relative-index*span;
  if(r.wavAt<r.wav.length)r.wav[r.wavAt++]=valid?raw:0;
  if(position<Math.round(r.rate*.075)||position>r.stageLength-Math.round(r.rate*.075))return;
  const capture=r.captures[index];
  if(!valid)capture.erasures++;
  capture.raw.push(valid?raw:0);capture.filtered.push(filtered);
 }
 _finishReceive(){
  const r=this.rx;if(!r)return;
  const rows=r.stages.map((stage,i)=>{
   const c=r.captures[i];return analyzeStage(stage,c.raw,c.filtered,r.rate,c.erasures);
  });
  evaluate(rows,this.mode);
  const direction=this.role==='A'?'Slave → Master':this.role==='B'?'Master → Slave':'Passive';
  const result={bank:r.bank,rows,direction,rate:r.rate,mode:this.mode,createdAt:new Date().toISOString(),
   wav:r.wav.slice(0,r.wavAt),sampleCount:r.wavAt,from:'RF audio stream'};
  this.results.unshift(result);if(this.results.length>6)this.results.length=6;
  this.rx=null;this.cooldownUntil=r.windowEnd+this.rate*.6;
  this.emit('rxEnd',{result});
  if(this.role==='B'&&this.autoReturn&&!this.tx){
   this.emit('autoReturn',{bank:r.bank});
   this.returnTimer=setTimeout(()=>{this.returnTimer=null;try{this.startTx(r.bank,'automatic reverse');}
    catch(error){this.emit('error',{message:error.message});}},950);
  }
 }
 latest(){return this.results[0]??null;}
 static toCsv(result){
  if(!result)return '';
  const fields=['direction','bank','kind','expected_hz','tx_dbfs','raw_hz','raw_dbfs','filtered_dbfs','relative_gain_db','raw_snr_db','raw_thd_pct','erasures','verdict','notes'];
  const esc=x=>`"${String(x??'').replace(/"/g,'""')}"`;
  return [fields.join(','),...result.rows.map(r=>[
   result.direction,result.bank,r.kind,r.frequency||'',Number.isFinite(r.level)?r.level:'',
   r.raw.frequency?.toFixed(2)??'',r.raw.level?.toFixed(2)??'',r.filtered.level?.toFixed(2)??'',
   r.gainError?.toFixed(2)??'',r.raw.snr?.toFixed(2)??'',r.raw.thd?.toFixed(2)??'',r.erasures,r.verdict,r.reason
  ].map(esc).join(','))].join('\r\n');
 }
 static toWav(result){return makeWav(result.wav,result.rate);}
 static get BANKS(){return BANKS;}
 static get STAGE_MS(){return STAGE_MS;}
}
scope.CBRadioDiagnosticEngine=DiagnosticEngine;
if(typeof module==='object'&&module.exports)module.exports={DiagnosticEngine,testStages,toneMagnitude,analyzeTone,evaluate,makeWav};
})(typeof globalThis==='object'?globalThis:this);
