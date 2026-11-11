/* CB Duplex Voice V3 — diagnostic bench presentation and controls. */
(function(scope){
'use strict';
const $=id=>document.getElementById(id);
const num=(x,d=1)=>Number.isFinite(x)?Number(x).toFixed(d):'—';
function filename(prefix,ext){return `${prefix}_${new Date().toISOString().replace(/[:.]/g,'-')}.${ext}`;}
function save(blob,name){
 const url=URL.createObjectURL(blob),a=document.createElement('a');
 a.href=url;a.download=name;a.hidden=true;document.body.append(a);a.click();a.remove();
 setTimeout(()=>URL.revokeObjectURL(url),2000);
}
function renderChart(result,display='both'){
 const c=$('diagChart'),ctx=c.getContext('2d');if(!ctx)return;
 const W=c.width,H=c.height,left=69,right=W-25,top=30,bottom=H-44;
 ctx.fillStyle='#10202a';ctx.fillRect(0,0,W,H);
 const rows=result?.rows||[];
 const levels=rows.filter(r=>r.kind==='level');
 const freq=rows.filter(r=>r.kind==='frequency');
 const plotLevel=freq.length===0&&levels.length>0;
 const legend=plotLevel?'Amplitude transfer (1 kHz)':freq.length?'Frequency response relative to 1 kHz':'No response curve in silence-only test';
 ctx.fillStyle='#d7e5e8';ctx.font='bold 15px system-ui';ctx.fillText(legend,18,21);
 const axisMin=plotLevel?-48:-30,axisMax=plotLevel?0:10;
 const yFor=y=>top+(axisMax-y)/(axisMax-axisMin)*(bottom-top);
 const xFor=x=>plotLevel?left+(x+42)/36*(right-left):left+(Math.log2(x)-Math.log2(80))/(Math.log2(8000)-Math.log2(80))*(right-left);
 ctx.font='12px system-ui';ctx.strokeStyle='#344b57';ctx.fillStyle='#9fb6bf';ctx.lineWidth=1;
 for(let y=axisMin;y<=axisMax;y+=plotLevel?12:10){
  const py=yFor(y);ctx.beginPath();ctx.moveTo(left,py);ctx.lineTo(right,py);ctx.stroke();
  ctx.textAlign='right';ctx.fillText(`${y} dB`,left-10,py+4);
 }
 const labels=plotLevel?[-42,-36,-30,-24,-18,-12,-6]:[80,125,250,500,1000,2000,4000,8000];
 for(const x of labels){const px=xFor(x);ctx.beginPath();ctx.moveTo(px,top);ctx.lineTo(px,bottom);ctx.stroke();
  ctx.textAlign='center';ctx.fillText(plotLevel?`${x}`:`${x>=1000?x/1000+'k':x}`,px,bottom+18);
 }
 ctx.textAlign='center';ctx.fillText(plotLevel?'Transmitted dBFS':'Audio frequency (Hz)',(left+right)/2,H-7);
 if(!result){ctx.textAlign='center';ctx.font='14px system-ui';ctx.fillStyle='#90a8b3';ctx.fillText('No measurements — run a diagnostic bank',W/2,H/2);return;}
 if(!freq.length&&!levels.length){ctx.textAlign='center';ctx.font='14px system-ui';ctx.fillStyle='#90a8b3';ctx.fillText('Silence-only test — see noise level and WAV export',W/2,H/2);return;}
 const reference=plotLevel?levels.find(r=>r.level===-12):freq.find(r=>r.frequency===1000);
 const plotted=plotLevel?levels:freq;
 const drawSeries=(field,color,offset=0)=>{
  if(display!=='both'&&display!==field)return;
  const ref=reference?.[field]?.level;
  const dots=plotted.filter(r=>Number.isFinite(r[field]?.level)).map(r=>({
    x:xFor(plotLevel?r.level:r.frequency),
    y:yFor(clamp((r[field].level-ref)+(plotLevel?-12:0),axisMin,axisMax))
  })).sort((a,b)=>a.x-b.x);
  ctx.strokeStyle=color;ctx.fillStyle=color;ctx.lineWidth=2.5;ctx.beginPath();
  dots.forEach((p,i)=>i?ctx.lineTo(p.x,p.y):ctx.moveTo(p.x,p.y));ctx.stroke();
  dots.forEach(p=>{ctx.beginPath();ctx.arc(p.x,p.y,3.6,0,Math.PI*2);ctx.fill();});
 };
 if(plotLevel){ctx.save();ctx.strokeStyle='#aebac4';ctx.setLineDash([5,5]);ctx.beginPath();
  for(let x=-42;x<=-6;x+=3){const px=xFor(x),py=yFor(x);x===-42?ctx.moveTo(px,py):ctx.lineTo(px,py);}ctx.stroke();ctx.restore();}
 drawSeries('raw','#6be3c1');drawSeries('filtered','#e7b96c');
 ctx.textAlign='left';ctx.font='12px system-ui';ctx.fillStyle='#6be3c1';ctx.fillText('● Raw received PCM',left+10,top+16);
 ctx.fillStyle='#e7b96c';ctx.fillText('● After audio filters',left+200,top+16);
}
function clamp(v,a,b){return Math.max(a,Math.min(b,v));}
class DiagnosticUI{
 constructor(controller,lab){this.controller=controller;this.lab=lab;this.loopActive=false;this.status='Idle';this.latest=null;this.busy=false;}
 bind(){
  $('diagStart').addEventListener('click',()=>this.begin());
  $('diagStop').addEventListener('click',()=>this.stop());
  $('diagLoopback').addEventListener('click',()=>this.loopback());
  $('diagAutoReverse').addEventListener('change',()=>{this.lab.autoReturn=$('diagAutoReverse').checked;});
  $('diagMeasure').addEventListener('change',()=>renderChart(this.latest,$('diagMeasure').value));
  $('diagCsv').addEventListener('click',()=>{if(this.latest)save(new Blob([CBRadioDiagnosticEngine.toCsv(this.latest)],{type:'text/csv;charset=utf-8'}),filename('duplex_audio_diagnostics','csv'));});
  $('diagWav').addEventListener('click',()=>{if(this.latest)save(CBRadioDiagnosticEngine.toWav(this.latest),filename('duplex_received_pcm','wav'));});
  renderChart();this.refresh();
 }
 message(text){this.status=text;$('diagStatus').textContent=text;this.controller.log('TEST: '+text);}
 event(event){
  switch(event.type){
   case 'txStart':this.message(`TX ${CBRadioDiagnosticEngine.BANKS[event.bank].label} (${event.seconds.toFixed(1)} s). Mic bypassed.`);$('diagProgress').value=0;break;
   case 'txProgress':$('diagProgress').value=event.percent;break;
   case 'txEnd':this.message(event.via==='automatic reverse'?'Slave return bank transmitted.':'TX complete. Waiting for measurements or the reverse bank.');$('diagProgress').value=100;break;
   case 'rxStart':this.message(`Detected ${event.direction} · capturing ${CBRadioDiagnosticEngine.BANKS[event.bank].label}.`);$('diagProgress').value=0;break;
   case 'rxProgress':$('diagProgress').value=event.percent;break;
   case 'rxEnd':this.present(event.result);this.message(`Measured ${event.result.direction} (${event.result.rows.length} stages). Export CSV/WAV on this receiving computer.`);$('diagProgress').value=100;break;
   case 'autoReturn':this.message('Slave: will transmit the reverse bank in about 1 second.');break;
   case 'error':this.message('Error: '+event.message);break;
  }
  this.refresh();
 }
 present(result){
  this.latest=result;
  $('diagDirection').textContent=result.direction;
  const pass=result.rows.filter(r=>r.verdict==='PASS').length;
  const fail=result.rows.filter(r=>r.verdict==='FAIL').length;
  const warn=result.rows.filter(r=>r.verdict==='WARN').length;
  const noise=result.rows.find(r=>r.kind==='silence');
  $('diagPass').textContent=`${pass} / ${result.rows.length}`;
  $('diagFailures').textContent=`${warn} warn · ${fail} fail`;
  $('diagNoise').textContent=noise?`${num(noise.raw.level,1)} dBFS`:'—';
  $('diagSamples').textContent=Number(result.sampleCount).toLocaleString('en-CA');
  const body=$('diagRows');body.replaceChildren();
  for(const row of result.rows){
   const tr=document.createElement('tr');
   const cells=[row.kind==='silence'?row.label:row.label,
      row.kind==='silence'?'—':`${num(row.raw.frequency,1)} Hz`,
      `${num(row.raw.level,1)} dBFS`,`${num(row.filtered.level,1)} dBFS`,
      row.gainError===null?'—':`${num(row.gainError,1)} dB`,
      row.raw.snr===null?'—':`${num(row.raw.snr,1)} dB`,
      row.raw.thd===null?'—':`${num(row.raw.thd,1)} %`,String(row.erasures)];
   for(const value of cells){const td=document.createElement('td');td.textContent=value;td.title=row.reason;tr.append(td);}
   const cell=document.createElement('td'),pill=document.createElement('span');pill.className='diag-result '+row.verdict;pill.textContent=row.verdict;pill.title=row.reason;
   cell.append(pill);tr.append(cell);body.append(tr);
  }
  renderChart(result,$('diagMeasure').value);this.refresh();
 }
 refresh(){
  const role=this.controller.role;const onAir=role==='A'||role==='B';
  const active=!!this.lab.tx||!!this.lab.rx||!!this.lab.returnTimer;
  $('diagStart').disabled=this.controller.busy||!onAir||active||this.loopActive;
  $('diagStart').textContent=role==='A'?'Start Master → Slave benchmark':role==='B'?'Send Slave → Master benchmark':'Start RF benchmark (connect as Master or Slave)';
  $('diagStop').disabled=!(active||this.loopActive);
  $('diagLoopback').disabled=this.controller.busy||!!this.controller.port||this.loopActive;
  $('diagCsv').disabled=!this.latest;
  $('diagWav').disabled=!this.latest;
 }
 begin(){
  if(!(this.controller.role==='A'||this.controller.role==='B')){this.message('Connect, then start or join a session before transmitting tests.');return;}
  try{this.lab.startTx($('diagBank').value);this.refresh();}catch(error){this.message(error.message);}
 }
 stop(){this.lab.cancel();this.loopActive=false;$('diagProgress').value=0;this.message('Test canceled. Normal microphone audio resumed.');this.refresh();}
 onRoleChange(){this.lab.cancel();this.refresh();}
 muteReceived(){return $('diagMute').checked&&(!!this.lab.rx||!!this.lab.tx||this.lab.scanState!=='wait');}
 async loopback(){
  if(this.controller.port){this.message('Disconnect the radio before running the local digital loopback.');return;}
  this.loopActive=true;this.refresh();
  const sim=new CBRadioDiagnosticEngine({onEvent:event=>{
   if(event.type==='rxEnd'){
    const r=event.result;r.direction='Local digital loopback · no RF';r.from='local PCM simulation';this.present(r);
   }
  }});
  const bank=$('diagBank').value;
  sim.allowSelfLoopback=true;
  const localProfile=$('audioProfile').value;
  const localCompanding=$('companding').value;
  sim.configure({rate:30000,role:'A',mode:localProfile,companding:localCompanding});
  const simulatedReceiver=new CBVoiceDsp({rate:30000,inputRate:48000,passive:false,dataSamples:20,
    audioMode:localProfile,companding:localCompanding,tddFilter:$('tddNotch').checked,onAudio:()=>{},
    onDiagnostics:(first,raw,filtered)=>sim.feedRx(first,raw,filtered)});
  sim.startTx(bank,'local loopback');
  this.message('Local PCM8 loopback running. These are NOT RF measurements.');
  try{
   let position=0,blocks=0;
   while(sim.tx||sim.rx||position<30000*.95){
    if(!this.loopActive)break;
    const bytes=sim.render(256,()=>new Int8Array(256));
    simulatedReceiver.pushRadio(position,bytes);position+=bytes.length;
    if(++blocks%32===0)await new Promise(resolve=>setTimeout(resolve,0));
    if(position>30000*24)throw Error('Loopback exceeded the safety limit');
    if(!sim.tx&&!sim.rx&&position>30000*2)break;
   }
   if(this.loopActive){
    if(!this.latest||this.latest.from!=='local PCM simulation')throw Error('Loopback receiver did not detect the synchronization pattern');
    this.message('Digital PCM8 loopback complete (no RF). Connect two stations for a real test.');
   }
  }catch(error){this.message('Loopback error: '+error.message);}
  finally{this.loopActive=false;this.refresh();}
 }
}
scope.CBRadioDiagnosticUI=DiagnosticUI;
})(globalThis);
