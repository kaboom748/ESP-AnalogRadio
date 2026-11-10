'use strict';

// RDS V6: workers are loaded from blob URLs to support file://.
// TX and RX require no additional Arduino commands.
(() => {
  const $r = id => document.getElementById(id);
  const MPX_RATE = 120000;
  const RDS_BATCH = 4096;
  const RDS_MAX_INFLIGHT = 3;
  const DEFAULTS = Object.freeze({region:'rbds', pi:'C3D7', ps:'KABOOMFM', rt:'CB FM Stereo',
    pty:'5', rtVersion:'2A'});
  const PTY_LABELS = {
    rbds:['None','News','Information','Sports','Talk','Rock','Classic Rock','Adult Hits',
      'Soft Rock','Top 40','Country','Oldies','Soft','Nostalgia','Jazz','Classical','R&B',
      'Soft R&B','Foreign Language','Religious Music','Religious Talk','Personality',
      'Public','College','Unassigned','Unassigned','Unassigned','Unassigned',
      'Weather','Emergency Test','Emergency'],
    rds:['None','News','Current Affairs','Information','Sports','Education','Drama',
      'Culture','Science','Varied','Pop','Rock','Easy Listening','Light Classical','Classical',
      'Other Music','Weather','Finance','Children','Social Affairs','Religion','Phone-in','Travel',
      'Leisure','Jazz','Country','National Music','Oldies','Folk','Documentary',
      'Emergency Test','Emergency']
  };
  let options = {...DEFAULTS};
  let configVersion = 1;
  let txWorker = null;
  let txWorkerUrl = null;
  let txPrepared = null;
  let preparePromise = null;
  let activeTx = null;
  let txBuilding = false;
  let nextJob = 0;
  const jobs = new Map();
  let txText = '';
  let txError = '';
  let rxTap = null;
  let rxData = null;
  let rxError = '';
  let rxSkipped = 0;
  let rxProfile = null;
  // Preserve only decoded fields from CRC-valid, locked RDS groups.
  // RDS lock is an instantaneous transport indicator; short fades should not
  // erase the last complete station name or RadioText on the display.
  let lastConfirmed = null;
  let lastConfirmedPi = null;
  let lastLockedAt = 0;
  let lastStatusAt = 0;
  const LOCK_DISPLAY_GRACE_MS = 2200;
  let lastRenderSignature = '';

  function createRdsWorker(source, onMessage, onError) {
    const url = URL.createObjectURL(new Blob([source], {type:'text/javascript'}));
    try {
      const worker = new Worker(url);
      worker.onmessage = ({data}) => onMessage(data);
      worker.onerror = event => onError(event?.message || 'Worker stopped.');
      return {worker, url};
    } catch (error) {
      URL.revokeObjectURL(url);
      throw error;
    }
  }
  function txWorkerError(text) {
    txError = 'RDS TX : ' + text;
    for (const [id, job] of jobs) {
      job.reject(new Error(txError));
      jobs.delete(id);
    }
    if (txWorker) txWorker.terminate();
    if (txWorkerUrl) URL.revokeObjectURL(txWorkerUrl);
    txWorker = null;
    txWorkerUrl = null;
    txBuilding = false;
    preparePromise = null;
    txPrepared = null;
    render(true);
  }
  function sendTxJob({seconds=0, fresh=false, unchanged=false}={}) {
    if (!txWorker) return Promise.reject(new Error(txError || 'RDS TX worker is unavailable.'));
    const id = ++nextJob;
    return new Promise((resolve,reject) => {
      jobs.set(id, {resolve,reject});
      try {
        if (fresh) txWorker.postMessage({type:'reset',options:txOptions()});
        else txWorker.postMessage({type:'configure',options:txOptions()});
        txWorker.postMessage({type:'build', requestId:id, seconds, allowUnchanged:unchanged});
      } catch (error) {
        jobs.delete(id);
        reject(error);
      }
    });
  }
  function txWorkerMessage(data) {
    const job = jobs.get(data?.requestId);
    if (!job) return;
    jobs.delete(data.requestId);
    if (data.type === 'error') job.reject(new Error(data.message));
    else job.resolve(data);
  }
  function ensureTxWorker() {
    if (txWorker) return;
    const created = createRdsWorker(RDS_TX_WORKER_SOURCE, txWorkerMessage, txWorkerError);
    txWorker = created.worker;
    txWorkerUrl = created.url;
    txError = '';
  }
  function txOptions() {
    return {region:options.region, pi:parseInt(options.pi,16), ps:options.ps, rt:options.rt,
      rtVersion:options.rtVersion, pty:Number(options.pty), tp:false, ta:false,
      music:true, stereo:true, artificialHead:false, compressed:false, dynamicPty:false};
  }
  function prepareTx() {
    if (txPrepared && txPrepared.version === configVersion) return Promise.resolve();
    if (preparePromise) return preparePromise.then(() => prepareTx());
    const version = configVersion;
    try { ensureTxWorker(); }
    catch (error) { txWorkerError(error.message); return Promise.reject(error); }
    preparePromise = sendTxJob({seconds:0, fresh:true}).then(data => {
      if (data.type !== 'wave') throw new Error('RDS multiplex was not generated.');
      if (version === configVersion) { txPrepared = {...data,version}; txError = ''; }
    }).catch(error => { txError = error.message; throw error; }).finally(() => {
      preparePromise = null;
      render(true);
    });
    return preparePromise;
  }
  function beginTx(node, epoch) {
    activeTx = {node, epoch, startedAt:performance.now(), seconds:0};
    if (txPrepared && txPrepared.version === configVersion) {
      const initial = txPrepared;
      txPrepared = null;
      txText = initial.text;
      // The worklet takes ownership of this array and retains it for the full cycle.
      node.port.postMessage({type:'rds-wave',epoch,envelope:initial.envelope}, [initial.envelope.buffer]);
    } else {
      txError = 'RDS is not ready: transmission must not start.';
    }
    render(true);
  }
  function endTx() {
    activeTx = null;
    txText = '';
    txBuilding = false;
    txPrepared = null;
    render(true);
    // Prepare on return to RX for the next transmission.
    void prepareTx().catch(() => {});
  }
  function nextTxWave(seconds) {
    if (!activeTx || txBuilding) return;
    const mode = activeTx;
    txBuilding = true;
    const version = configVersion;
    sendTxJob({seconds,unchanged:true}).then(data => {
      if (activeTx !== mode || !activeTx || data.type === 'unchanged') return;
      // If settings changed during processing, wait for the next cycle.
      if (version !== configVersion) return;
      if (data.type !== 'wave') throw new Error('Invalid RDS response.');
      mode.pendingText = data.text;
      txError = '';
      mode.node.port.postMessage({type:'rds-wave',epoch:mode.epoch,envelope:data.envelope}, [data.envelope.buffer]);
      render(true);
    }).catch(error => { txError = 'RDS TX : ' + error.message; render(true); })
      .finally(() => { txBuilding = false; });
  }
  function workletMessage(data) {
    if (!['rds-cycle','rds-applied','rds-prepare'].includes(data?.type)) return false;
    if (!activeTx || data.epoch !== activeTx.epoch) return true;
    activeTx.seconds = Math.floor((data.elapsedSamples || 0) / MPX_RATE);
    if (data.type === 'rds-applied' && activeTx.pendingText) {
      txText = activeTx.pendingText;
      activeTx.pendingText = null;
      render(true);
    }
    // During the current cycle, generate text for the NEXT broadcast time,
    // not the calculation time.
    if (data.type === 'rds-prepare') {
      nextTxWave(Math.floor((data.targetSamples || data.elapsedSamples) / MPX_RATE));
    } else if (data.type === 'rds-cycle') {
      // Fallback if the new waveform was not ready.
      nextTxWave(Math.floor((data.elapsedSamples || 0) / MPX_RATE));
    }
    return true;
  }

  function stopRx() {
    if (rxTap) {
      rxTap.worker.terminate();
      URL.revokeObjectURL(rxTap.url);
      rxTap = null;
    }
    rxData = null;
    rxSkipped = 0;
    rxError = '';
    rxProfile = null;
    lastConfirmed = null;
    lastConfirmedPi = null;
    lastLockedAt = 0;
    lastStatusAt = 0;
    render(true);
  }
  function rxMessage(tap, data) {
    if (rxTap !== tap) return;
    if (data?.type === 'ack') { tap.inflight = Math.max(0, tap.inflight - 1); return; }
    if (data?.type === 'error') {
      rxError = data.message || 'Decoding error.';
      render(true);
      return;
    }
    if (data?.type === 'status') {
      rxData = data;
      lastStatusAt = performance.now();
      // The worker only declares lock after a CRC-valid four-block group.
      // Commit any newly completed fields, but do not overwrite a confirmed
      // RadioText/PS with null while the next message is being assembled.
      if (data.locked && data.fields?.pi) {
        const fields = data.fields;
        if (fields.pi !== lastConfirmedPi) {
          lastConfirmedPi = fields.pi;
          lastConfirmed = {pi:fields.pi}; // New station: no stale prior PS/RT.
        }
        if (!lastConfirmed) lastConfirmed = {pi:fields.pi};
        for (const field of ['ps','radioText','pty','radioTextMode','radioTextAB']) {
          if (fields[field] !== null && fields[field] !== undefined && fields[field] !== '') {
            lastConfirmed[field] = fields[field];
          }
        }
        lastLockedAt = lastStatusAt;
      }
      render(true);
    }
  }
  function startRx(profile) {
    stopRx();
    if (profile !== 'HQ') return;
    try {
      let tap;
      const created = createRdsWorker(RDS_RX_WORKER_SOURCE,
        data => rxMessage(tap,data),
        error => { if (rxTap===tap) { rxError=error; render(true); } });
      tap = {worker:created.worker,url:created.url,index:0,count:0,
        bytes:new Int8Array(RDS_BATCH),inflight:0};
      rxTap = tap;
      rxProfile = 'HQ';
      tap.worker.postMessage({type:'reset', sampleRate:MPX_RATE,profile:options.region.toUpperCase()});
    } catch (error) { rxError = error.message; }
    render(true);
  }
  function flushRx() {
    const tap = rxTap;
    if (!tap || !tap.count) return;
    if (tap.inflight >= RDS_MAX_INFLIGHT) {
      rxSkipped += tap.count;
      tap.count = 0;
      if (rxData?.locked) rxData = {...rxData, locked:false};
      render(true);
      return;
    }
    const samples = tap.bytes.subarray(0,tap.count);
    tap.worker.postMessage({type:'samples',index:tap.index,samples},[samples.buffer]);
    tap.bytes = new Int8Array(RDS_BATCH);
    tap.count = 0;
    tap.inflight++;
  }
  function feedRx(index, bytes) {
    if (!rxTap || !bytes?.length) return;
    let offset = 0;
    while (offset < bytes.length && rxTap) {
      const tap = rxTap;
      const incomingIndex = index + offset;
      if (tap.count && tap.index + tap.count !== incomingIndex) flushRx();
      if (!tap.count) tap.index = incomingIndex;
      const count = Math.min(bytes.length-offset, RDS_BATCH-tap.count);
      for (let n=0;n<count;n++) tap.bytes[tap.count+n] = bytes[offset+n];
      tap.count += count;
      offset += count;
      if (tap.count === RDS_BATCH) flushRx();
    }
  }
  function modeChange(mode, profile, epoch, node) {
    if (mode === 'rx' && profile === 'HQ') {
      // Audio resets are used to conceal missing MPX3 frames (>30 ms) while
      // remaining on the SAME station. Never restart the RDS Worker here:
      // startRx() calls stopRx(), which erases all previously validated PS,
      // RadioText, PI, and PTY. The RDS Worker can resynchronize MPX gaps itself.
      if (!rxTap || rxProfile !== 'HQ') startRx(profile);
    } else if (rxTap || rxProfile !== null || lastConfirmed) {
      // Actual mode exits, changes away from HQ and explicit retuning
      // (which first enters idle) still clear the old station.
      stopRx();
    }
    if (mode === 'tx') beginTx(node, epoch);
    else if (activeTx) endTx();
  }

  function currentRxLock(now=performance.now()) {
    // Ignore a stale "locked" report if the MPX transport has stopped.
    return Boolean(rxTap && rxData?.locked && now-lastStatusAt<700);
  }
  // In this radio a closed squelch is NOT proof that TX has ended: on a
  // weak signal it may close for seconds while the sender is still on-air.
  // Keep the last CRC-validated station until a confirmed new PI arrives,
  // an explicit retune, or an actual RX mode exit. This prevents all RDS
  // fields from disappearing during reception fades.

  function localTime(seconds) {
    const n = Math.max(0,Math.floor(seconds));
    return String(Math.floor(n/3600)).padStart(2,'0') + ':' +
      String(Math.floor(n/60)%60).padStart(2,'0') + ':' + String(n%60).padStart(2,'0');
  }
  function snapshot() {
    const now=performance.now();
    const rx = !activeTx && rxProfile === 'HQ';
    const locked = rx && currentRxLock(now);
    const tx = Boolean(activeTx);
    const fields = tx ? {ps:options.ps,radioText:txText || options.rt,
      pi:options.pi, pty:Number(options.pty)} : rx ? (lastConfirmed || {}) : {};
    const pty = fields.pty;
    const profile = tx ? options.region : (rx ? options.region : null);
    const hasText=Boolean(fields.ps || fields.radioText);
    const held=rx && !locked && Boolean(fields.pi || fields.ps || fields.radioText || fields.pty != null);
    const displayLocked=locked || (rx && !!lastLockedAt && now-lastLockedAt<LOCK_DISPLAY_GRACE_MS);
    let status = tx ? 'Active (TX)' : rx ? displayLocked ? 'Locked' : held ?
      'Last RDS retained · weak signal' : 'Searching for RDS…' : 'FM Stereo / HQ only';
    if (txError && tx) status = 'RDS TX error';
    if (rxError && rx) status = 'RDS RX error';
    const piValue = fields.pi == null ? '—' : String(fields.pi).replace(/^0x/i,'').toUpperCase().padStart(4,'0');
    return {mode:tx?'tx':rx?'rx':'idle', locked, held, displayLocked, hasText, status,
      ps:fields.ps || '—', radioText:fields.radioText || '—',
      pi:piValue === '—' ? piValue : '0x'+piValue,
      pty:pty == null ? '—' : (PTY_LABELS[profile]?.[pty] || 'PTY')+' ('+pty+')',
      region:options.region==='rbds'?'RBDS (North America)':'RDS (International)',
      seconds:tx ? Math.floor((now-activeTx.startedAt)/1000) : 0,
      transmittedText:txText || '', error:tx?txError:rxError,
      groups:rxData?.groups||0,errors:rxData?.errors||0,skipped:rxSkipped};
  }
  function render(force=false) {
    const s=snapshot();
    const values = {heading:s.mode==='tx'?'RDS (TX)':'RDS (RX)', status:s.status,
      ps:s.ps, rt:s.radioText, pi:s.pi, pty:s.pty, profile:s.region,
      timer:s.mode==='tx'?localTime(s.seconds):'—', errors:s.error||'',
      groups:String(s.groups), crc:String(s.errors), skipped:String(s.skipped)};
    const signature = JSON.stringify(values)+s.locked+s.held+s.displayLocked;
    if (!force && signature === lastRenderSignature) return;
    lastRenderSignature = signature;
    for (const element of document.querySelectorAll('[data-rds-value]')) {
      const value = values[element.dataset.rdsValue] ?? '';
      if (element.textContent !== value) element.textContent = value;
    }
    for (const element of document.querySelectorAll('[data-rds-state]')) {
      element.dataset.rdsState = s.mode === 'tx' ? 'tx' : s.displayLocked ? 'locked' :
        s.held ? 'held' : s.mode;
    }
    const profileInfo=$r('cb-rds-profile-description');
    if (profileInfo) profileInfo.textContent = s.region;
  }

  function mirroredControls() {
    return [...document.querySelectorAll('[data-rds-config]')];
  }
  function populatePty(select) {
    const chosen = select.value || options.pty;
    select.replaceChildren(...PTY_LABELS[options.region].map((name,code) =>
      new Option(code+' · '+name,String(code))));
    select.value = chosen;
  }
  function syncControls(skip=null) {
    for (const control of mirroredControls()) {
      if (control === skip) continue;
      const key=control.dataset.rdsConfig;
      if (key==='pty') populatePty(control);
      if (control.value !== options[key]) control.value = options[key];
    }
    const rtLimit = options.rtVersion === '2B' ? 32 : 64;
    const status = $r('cb-rds-settings-help');
    if (status) status.textContent = 'Profile '+(options.region==='rbds'?'RBDS · North America':'RDS · International')+
      ' · RadioText '+options.rtVersion+' ('+rtLimit+' characters, no transmitted TX timer).'+
      ' RadioText is always transmitted during TX; decoding is only available in FM Stereo HQ.';
  }
  function updateConfig(source) {
    const key=source.dataset.rdsConfig;
    let value=source.value;
    if (key==='pi') value=value.replace(/^0x/i,'').toUpperCase();
    if (key==='region' && value!=='rbds' && value!=='rds') return;
    if (key==='pi' && !/^[0-9A-F]{4}$/.test(value)) {
      source.setAttribute('aria-invalid','true');
      return;
    }
    source.removeAttribute('aria-invalid');
    if (key==='ps') value=value.slice(0,8);
    if (key==='rt') value=value.slice(0,64);
    if (options[key]===value) return;
    options[key]=value;
    if (key==='region') {
      if (rxTap) rxTap.worker.postMessage({type:'profile',profile:value.toUpperCase()});
    }
    configVersion++;
    txPrepared = null;
    if (!activeTx) void prepareTx().catch(() => {});
    syncControls(source);
    render(true);
  }
  function init() {
    // RBDS is the initial default; do not restore a previously selected profile implicitly.
    syncControls();
    for (const control of mirroredControls()) {
      control.addEventListener(control.tagName==='SELECT'?'change':'input',() => updateConfig(control));
    }
    setInterval(() => {
      if (rxTap && rxTap.count) flushRx();
      render();
    },120);
    // Prepare the signal before the first PTT (Chrome/Edge: blob Web Worker).
    void prepareTx().catch(() => {});
    render(true);
  }
  window.cbRds={prepareTx, modeChange, workletMessage, feedRx, snapshot, render,txOptions};
  init();
})();
