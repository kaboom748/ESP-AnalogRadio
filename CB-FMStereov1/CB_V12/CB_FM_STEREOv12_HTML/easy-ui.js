'use strict';

(() => {
  const tabs = [...document.querySelectorAll('.cb-mode-tabs [role="tab"]')];
  const panels = tabs.map(tab => document.getElementById(tab.getAttribute('aria-controls')));
  function activate(next, focus = false) {
    if (tabs[next].getAttribute('aria-selected') !== 'true') {
      window.cbFrontPanel.dtmfCancel();
      window.cbFrontPanel.release();
      tabs.forEach((tab, index) => {
        const selected = index === next;
        tab.setAttribute('aria-selected', String(selected));
        tab.tabIndex = selected ? 0 : -1;
        panels[index].hidden = !selected;
      });
    }
    if (focus) tabs[next].focus();
  }
  tabs.forEach((tab, index) => {
    tab.addEventListener('click', () => activate(index));
    tab.addEventListener('keydown', event => {
      let next;
      if (event.key === 'ArrowLeft' || event.key === 'ArrowRight') next = 1 - index;
      else if (event.key === 'Home') next = 0;
      else if (event.key === 'End') next = tabs.length - 1;
      else return;
      event.preventDefault();
      activate(next, true);
    });
  });
})();
(() => {
  'use strict';

  const byId = id => document.getElementById(id);
  const easy = name => byId('cb-easy-' + name);
  const lcd = name => byId('cb-lcd-' + name);
  const radio = window.cbFrontPanel;
  const panel = easy('panel');
  const face = byId('cb-face');
  const ptt = easy('ptt');
  const continuous = easy('continuous');
  const settings = easy('settings');
  const settingsTitle = easy('settings-title');
  const settingsBody = easy('settings-body');
  const settingsClose = easy('settings-close');
  const dtmfDrawer = byId('cb-dtmf-drawer');
  const dtmfButtons = [...dtmfDrawer.querySelectorAll('[data-dtmf-key]')];
  const indicatorNames = ['rx', 'sq', 'tx'];
  const vuLeft = byId('cb-vu-l');
  const vuRight = byId('cb-vu-r');
  const number = new Intl.NumberFormat('en-CA');
  const mhz = new Intl.NumberFormat('en-CA', {minimumFractionDigits: 4, maximumFractionDigits: 6});
  const channels = new Map([[2412312500, '01'], [2442312500, '07'], [2462312500, '11']]);
  const channelNames = [...channels.values()];
  const profileNames = ['HQ', 'ROBUST', 'MONO'];
  const readouts = {
    rxVolume: {label: 'Receive volume', short: 'VOL', unit: '%', wheelStep: 5},
    squelch: {label: 'Squelch', short: 'SQ', unit: 'dB', wheelStep: 1},
    micGain: {label: 'Microphone gain', short: 'MIC', unit: 'dB', wheelStep: 1, signed: true},
    fine: {label: 'Receive fine tuning', short: 'RX TUNE', unit: 'Hz', wheelStep: 500, signed: true},
  };

  let pointer = null;
  let heldKey = null;
  let dtmfGesture = null;
  let detail = '';
  let detailUntil = 0;
  let opener = null;
  let refreshSettings = null;

  function writeText(element, value) {
    if (element.textContent !== value) element.textContent = value;
  }

  function textOf(id, fallback) {
    return byId(id)?.textContent || fallback;
  }

  function valueAsHz(value) {
    const compact = String(value).replace(/\s/g, '');
    return Math.round(Number(compact.includes('.') ? compact.replace(/,/g, '') : compact.replace(',', '.')) * 1e6);
  }

  function valueLabel(control, value) {
    const info = readouts[control];
    if (control === 'squelch' && value === 0) return 'Open';
    return (info.signed && value > 0 ? '+' : '') + number.format(value) + (info.unit === '%' ? '%' : ' ' + info.unit);
  }

  function showDetail(value) {
    detail = value;
    detailUntil = performance.now() + 1600;
    render();
  }

  function fire(control, name) {
    control.dispatchEvent(new Event(name, {bubbles: true}));
  }

  function setValue(id, value) {
    const control = byId(id);
    if (control.disabled) return;
    control.value = String(value);
    fire(control, 'input');
    showDetail(readouts[id].short + ' ' + valueLabel(id, Number(control.value)));
  }

  function tuneIfListening() {
    if (radio.snapshot().state === 'rx') byId('tune').click();
  }

  function updateVu(meter, db, peak) {
    const value = Number.isFinite(db) ? Math.max(-60, Math.min(0, db)) : -60;
    const label = String(Math.round(value * 10) / 10);
    if (meter.dataset.db !== label) {
      meter.dataset.db = label;
      meter.setAttribute('aria-valuenow', label);
    }
    const count = Math.max(0, Math.ceil((value + 30) * meter.childElementCount / 30));
    if (meter.dataset.level !== String(count)) {
      meter.dataset.level = String(count);
      for (const [index, segment] of Array.from(meter.children).entries()) {
        segment.classList.toggle('is-lit', index < count);
      }
    }
    const peakState = String(Boolean(peak));
    if (meter.dataset.peak !== peakState) meter.dataset.peak = peakState;
  }

  function render() {
    const snapshot = radio.snapshot();
    if (panel.hidden) dtmfGesture = null;
    const receiving = snapshot.state === 'rx' || snapshot.state === 'arming';
    const transmitting = (snapshot.state === 'tx' && snapshot.txPrimed) ||
      (snapshot.state === 'returning' && face.dataset.mode === 'tx');
    const audioReceived = receiving && snapshot.squelchOpen && snapshot.rxAudioActive;
    const mode = transmitting ? 'tx' : audioReceived ? 'rx' : receiving && !snapshot.squelchOpen ? 'sq' : receiving ? 'stby' : 'off';
    face.dataset.mode = mode;
    for (const name of indicatorNames) {
      const light = byId('cb-led-' + name);
      const active = String(mode === name);
      if (light.dataset.active !== active) {
        light.dataset.active = active;
        light.setAttribute('aria-label', name.toUpperCase() + (mode === name ? ' on' : ' off'));
      }
    }
    const hz = snapshot.activeTune?.hz ?? valueAsHz(byId('frequency').value);
    const channel = channels.get(hz) ?? '– –';
    const profile = snapshot.activeTune?.profile ?? byId('profile').value;
    const signal = receiving && Number.isFinite(snapshot.signalDb) && Number.isFinite(snapshot.noiseDb)
      ? Math.max(0, Math.min(60, snapshot.signalDb - snapshot.noiseDb)) : 0;
    writeText(lcd('mode'), transmitting ? 'TX' : audioReceived ? 'RX' : receiving ? 'STBY' : snapshot.state === 'off' ? 'OFF' : '—');
    writeText(lcd('profile'), profile);
    writeText(lcd('channel'), channel);
    writeText(lcd('frequency'), Number.isFinite(hz) && hz > 0 ? mhz.format(hz / 1e6) + ' MHz' : '— MHz');
    // RadioText has its own line without hiding the frequency.
    // Temporary front-panel notifications take precedence.
    const rds = window.cbRds?.snapshot();
    const rdsVisible = profile === 'HQ' && ((transmitting && rds?.mode === 'tx') ||
      (receiving && rds?.mode === 'rx' && rds?.hasText));
    lcd('rds').hidden = !rdsVisible;
    face.querySelector('.cb-face-lcd').dataset.rdsDisplay = String(rdsVisible);
    // A valid RadioText may arrive before all four PS segments; never show
    // a literal dash as if it were the station name.
    const stationText = rdsVisible ? [rds.ps,rds.radioText].filter(text => text && text !== '—').join(' | ') : '';
    writeText(lcd('detail'), performance.now() < detailUntil ? detail : stationText);
    const meterActive = (snapshot.state === 'tx' && snapshot.txPrimed) || (receiving && snapshot.squelchOpen);
    updateVu(vuLeft, meterActive ? snapshot.meterL : -60, meterActive && snapshot.peakL);
    updateVu(vuRight, meterActive ? snapshot.meterR : -60, meterActive && snapshot.peakR);
    lcd('meter').style.setProperty('--cb-bars', Math.round(signal / 60 * 100) + '%');
    lcd('meter').setAttribute('aria-valuenow', String(Math.round(signal)));
    face.dataset.squelch = snapshot.squelchOpen ? 'open' : 'closed';
    face.dataset.stereo = snapshot.playback?.pilotLocked && profile !== 'MONO' ? 'true' : 'false';
    ptt.dataset.held = String(Boolean(snapshot.pttHold));
    ptt.setAttribute('aria-pressed', ptt.dataset.held);
    ptt.disabled = byId('ptt').disabled;
    const continuousOn = snapshot.pttHold === 'continuous';
    continuous.setAttribute('aria-checked', String(continuousOn));
    continuous.disabled = byId('ptt').disabled || (Boolean(snapshot.pttHold) && !continuousOn);
    easy('channel').disabled = byId('preset').disabled;
    easy('fine').disabled = byId('fine').disabled;
    easy('mode').disabled = byId('profile').disabled;
    const powerLabel = 'TX POWER · APWR ' + (snapshot.apwr === null ? '—' : snapshot.apwr);
    easy('power').title = powerLabel + ' · 0 maximum · 191 minimum';
    easy('power').setAttribute('aria-label', powerLabel + ', 0 maximum, 191 minimum');
    easy('port').disabled = byId('serial').disabled;
    easy('port').setAttribute('aria-label', snapshot.state === 'off' ? 'Connect serial port' : 'Disconnect serial port');
    const status = easy('status');
    writeText(status, snapshot.state === 'off' && !snapshot.statusError ? 'Press PORT to connect the ESP8266.' : snapshot.status);
    status.dataset.error = String(snapshot.statusError === true || snapshot.statusError === 'true');
    const activeKey = dtmfGesture?.key ?? snapshot.dtmfKey;
    for (const button of dtmfButtons) {
      const pressed = String(activeKey === button.dataset.dtmfKey);
      if (button.getAttribute('aria-pressed') !== pressed) button.setAttribute('aria-pressed', pressed);
    }
    writeText(byId('cb-dtmf-speaker'), valueLabel('rxVolume', Number(byId('rxVolume').value)));
    writeText(byId('cb-dtmf-mic-gain'), valueLabel('micGain', Number(byId('micGain').value)));
    if (!settings.hidden) refreshSettings?.();
  }

  function openSettings(button, title) {
    if (!settings.hidden && opener === button) {
      closeSettings();
      return false;
    }
    opener?.setAttribute('aria-expanded', 'false');
    opener = button;
    opener.setAttribute('aria-expanded', 'true');
    settingsTitle.textContent = title;
    settingsBody.replaceChildren();
    refreshSettings = null;
    settings.hidden = false;
    return true;
  }

  function closeSettings() {
    settings.hidden = true;
    refreshSettings = null;
    opener?.setAttribute('aria-expanded', 'false');
    if (!panel.hidden) opener?.focus();
    opener = null;
  }

  function element(tag, properties) {
    return Object.assign(document.createElement(tag), properties);
  }

  function labeled(label, control) {
    const wrapper = document.createElement('label');
    wrapper.append(element('span', {textContent: label}), control);
    return wrapper;
  }

  function openRange(button, id) {
    const control = byId(id);
    if (!openSettings(button, readouts[id].label)) return;
    const slider = element('input', {id: 'cb-easy-value', type: 'range', min: control.min, max: control.max,
      step: control.step || '1', value: control.value});
    const output = element('output', {value: valueLabel(id, Number(slider.value))});
    output.setAttribute('for', slider.id);
    slider.oninput = () => setValue(id, slider.value);
    if (id === 'fine') slider.onchange = tuneIfListening;
    refreshSettings = () => {
      slider.disabled = control.disabled;
      if (slider.value !== control.value) slider.value = control.value;
      output.value = valueLabel(id, Number(control.value));
      slider.setAttribute('aria-valuetext', output.value);
    };
    settingsBody.append(labeled(readouts[id].label, slider), output);
    refreshSettings();
    slider.focus();
  }

  function openTxPower() {
    if (!openSettings(easy('power'), 'TX POWER')) return;
    const source = byId('apwr');
    const numberInput = element('input', {id: 'cb-easy-apwr', type: 'number', min: '0', max: '191',
      step: '1', inputMode: 'numeric', autocomplete: 'off', value: source.value});
    const slider = element('input', {id: 'cb-easy-apwr-range', type: 'range', min: '0', max: '191',
      step: '1', value: byId('apwrRange').value});
    slider.setAttribute('aria-label', 'APWR: 0 maximum TX power, 191 minimum TX power');
    numberInput.setAttribute('aria-describedby', 'cb-easy-power-help cb-easy-power-error');
    const scale = element('div', {className: 'tx-power-scale'});
    scale.setAttribute('aria-hidden', 'true');
    scale.append(element('span', {textContent: 'MAX · 0'}), element('span', {textContent: 'MIN · 191'}));
    const help = element('p', {id: 'cb-easy-power-help', className: 'cb-easy-setting-note',
      textContent: '0 = maximum TX power · 191 = minimum TX power. Applies to both PTT and DTMF.'});
    const note = element('p', {className: 'cb-easy-setting-note'});
    note.setAttribute('aria-live', 'polite');
    const error = element('p', {id: 'cb-easy-power-error', className: 'tx-power-error', textContent: 'Enter an integer from 0 to 191.'});
    error.setAttribute('role', 'alert');
    function update(raw) {
      if (source.disabled) return;
      source.value = raw;
      fire(source, 'input');
      const value = radio.snapshot().apwr;
      if (value !== null) showDetail('TX POWER ' + value);
      refreshSettings?.();
    }
    numberInput.oninput = () => update(numberInput.value);
    slider.oninput = () => update(slider.value);
    refreshSettings = () => {
      numberInput.disabled = slider.disabled = source.disabled;
      if (numberInput.value !== source.value) numberInput.value = source.value;
      slider.value = byId('apwrRange').value;
      slider.setAttribute('aria-valuetext', byId('apwrRange').getAttribute('aria-valuetext'));
      const invalid = radio.snapshot().apwr === null;
      numberInput.setAttribute('aria-invalid', String(invalid));
      error.hidden = !invalid;
      writeText(note, byId('txPowerState').textContent);
    };
    settingsBody.append(labeled('APWR · 0 to 191', numberInput), scale, slider, help, note, error);
    refreshSettings();
    if (!source.disabled) numberInput.focus();
  }

  function setChannel(value) {
    const preset = byId('preset');
    if (preset.disabled) return;
    preset.value = value;
    fire(preset, 'change');
    render();
  }

  function stepChannel(direction) {
    const presets = Array.from(byId('preset').options, option => option.value).filter(value => value !== 'manual');
    const current = presets.indexOf(byId('preset').value);
    const next = current < 0 ? (direction > 0 ? 0 : presets.length - 1) : (current + direction + presets.length) % presets.length;
    setChannel(presets[next]);
    showDetail('CH ' + channelNames[next]);
  }

  function openChannel() {
    const button = easy('channel');
    if (!openSettings(button, 'Channel and frequency')) return;
    const picks = document.createElement('div');
    for (const [hz, channel] of channels) {
      const preset = element('button', {type: 'button', textContent: 'CH ' + channel});
      preset.onclick = () => { setChannel(String(hz / 1e6)); closeSettings(); };
      picks.append(preset);
    }
    const form = document.createElement('form');
    const frequency = element('input', {id: 'cb-easy-frequency', type: 'text', inputMode: 'decimal',
      autocomplete: 'off', required: true, value: byId('frequency').value.replace(',', '.')});
    const apply = element('button', {id: 'cb-easy-frequency-apply', type: 'submit', textContent: 'Tune'});
    frequency.oninput = () => frequency.setCustomValidity('');
    form.onsubmit = event => {
      event.preventDefault();
      const hz = valueAsHz(frequency.value);
      if (!Number.isFinite(hz) || hz < 2401000000 || hz > 2482500000) {
        frequency.setCustomValidity('Enter a frequency from 2,401 to 2,482.5 MHz.');
        frequency.reportValidity();
        return;
      }
      if (byId('frequency').disabled) return;
      byId('frequency').value = String(hz / 1e6);
      fire(byId('frequency'), 'input');
      tuneIfListening();
      closeSettings();
      render();
    };
    form.append(labeled('Manual frequency (MHz)', frequency), apply);
    settingsBody.append(picks, form);
    refreshSettings = () => {
      for (const control of [frequency, apply, ...picks.children]) control.disabled = byId('preset').disabled;
    };
    frequency.focus();
  }

  function mirrorSelect(id, label) {
    const source = byId(id);
    const select = element('select', {id: 'cb-easy-' + id});
    let signature = '';
    function refresh() {
      const next = JSON.stringify(Array.from(source.options, option => [option.value, option.text]));
      if (signature !== next) {
        signature = next;
        select.replaceChildren(...Array.from(source.options, option => new Option(option.text, option.value)));
      }
      select.value = source.value;
      select.disabled = source.disabled;
    }
    select.onchange = () => {
      source.value = select.value;
      fire(source, 'change');
    };
    settingsBody.append(labeled(label, select));
    refresh();
    return refresh;
  }

  function openAudio() {
    if (!openSettings(easy('audio'), 'Audio devices')) return;
    const refreshMic = mirrorSelect('mic', 'Microphone');
    const refreshSpeaker = mirrorSelect('speaker', 'Speaker');
    const prepare = element('button', {type: 'button', textContent: 'Prepare microphone'});
    prepare.onclick = () => byId('enableMic').click();
    const note = element('p', {className: 'cb-easy-setting-note'});
    settingsBody.append(prepare, note);
    refreshSettings = () => {
      refreshMic();
      refreshSpeaker();
      prepare.disabled = byId('enableMic').disabled;
      writeText(note, textOf('audioStatus', ''));
    };
    refreshSettings();
    easy('mic').focus();
  }

  function knob(button, open, step) {
    button.setAttribute('aria-expanded', 'false');
    button.setAttribute('aria-controls', settings.id);
    button.addEventListener('click', open);
    button.addEventListener('wheel', event => {
      if (button.disabled || !event.deltaY) return;
      event.preventDefault();
      step(event.deltaY < 0 ? 1 : -1);
    }, {passive: false});
    button.addEventListener('keydown', event => {
      if (!['ArrowUp', 'ArrowRight', 'ArrowDown', 'ArrowLeft'].includes(event.key)) return;
      event.preventDefault();
      step(event.key === 'ArrowUp' || event.key === 'ArrowRight' ? 1 : -1);
    });
  }

  for (const [name, id] of [['vol', 'rxVolume'], ['sq', 'squelch'], ['gain', 'micGain'], ['fine', 'fine']]) {
    const button = easy(name);
    knob(button, () => openRange(button, id), direction => {
      const control = byId(id);
      const value = Math.max(Number(control.min), Math.min(Number(control.max),
        Number(control.value) + direction * readouts[id].wheelStep));
      setValue(id, value);
      if (id === 'fine') tuneIfListening();
    });
  }
  knob(easy('channel'), openChannel, stepChannel);
  easy('power').setAttribute('aria-expanded', 'false');
  easy('power').setAttribute('aria-controls', settings.id);
  easy('power').addEventListener('click', openTxPower);
  easy('audio').setAttribute('aria-expanded', 'false');
  easy('audio').setAttribute('aria-controls', settings.id);
  easy('audio').addEventListener('click', openAudio);
  easy('port').addEventListener('click', () => byId('serial').click());
  easy('mode').addEventListener('click', () => {
    const profile = byId('profile');
    profile.value = profileNames[(profileNames.indexOf(profile.value) + 1) % profileNames.length];
    fire(profile, 'change');
    showDetail(profile.value);
  });
  settingsClose.addEventListener('click', closeSettings);
  settings.addEventListener('keydown', event => {
    if (event.key === 'Escape') closeSettings();
  });

  function canTalk() {
    const snapshot = radio.snapshot();
    return snapshot.state === 'rx' && !snapshot.pttHold && !panel.hidden;
  }
  function stopTalking() {
    if (radio.snapshot().pttHold === 'front') radio.release();
    render();
  }
  continuous.addEventListener('click', () => {
    const snapshot = radio.snapshot();
    if (snapshot.pttHold === 'continuous') radio.release();
    else if (canTalk()) void radio.pressContinuous();
    render();
  });
  ptt.addEventListener('pointerdown', event => {
    if (event.button !== 0 || pointer !== null || !canTalk()) return;
    event.preventDefault();
    ptt.focus();
    pointer = event.pointerId;
    ptt.setPointerCapture(pointer);
    void radio.press();
    render();
  });
  for (const name of ['pointerup', 'pointercancel', 'lostpointercapture']) {
    ptt.addEventListener(name, event => {
      if (pointer !== event.pointerId) return;
      pointer = null;
      stopTalking();
    });
  }
  ptt.addEventListener('keydown', event => {
    if (event.code !== 'Space' && event.key !== 'Enter') return;
    event.preventDefault();
    event.stopPropagation();
    if (event.repeat || !canTalk()) return;
    heldKey = event.code;
    void radio.press();
    render();
  });
  document.addEventListener('keyup', event => {
    if (event.code !== heldKey) return;
    heldKey = null;
    event.preventDefault();
    stopTalking();
  });
  // Keyup or window focus loss releases keyboard PTT.
  // A DTMF click during held PTT retains the same session and APWR.

  function holdDtmf(button, trigger) {
    if (dtmfGesture || panel.hidden || !dtmfDrawer.open) return false;
    const key = button.dataset.dtmfKey;
    if (!radio.dtmfPress(key)) return false;
    dtmfGesture = {button, key, ...trigger};
    render();
    return true;
  }

  function releaseDtmf(cancel = false) {
    if (!dtmfGesture) return;
    const {key} = dtmfGesture;
    dtmfGesture = null;
    if (cancel) radio.dtmfCancel();
    else radio.dtmfRelease(key);
    render();
  }

  for (const button of dtmfButtons) {
    button.addEventListener('pointerdown', event => {
      if (event.button !== 0 || dtmfGesture) return;
      event.preventDefault();
      button.focus();
      if (holdDtmf(button, {pointerId: event.pointerId})) button.setPointerCapture(event.pointerId);
    });
    for (const name of ['pointerup', 'pointercancel', 'lostpointercapture']) {
      button.addEventListener(name, event => {
        if (dtmfGesture?.button === button && dtmfGesture.pointerId === event.pointerId) releaseDtmf(name !== 'pointerup');
      });
    }
    button.addEventListener('keydown', event => {
      if (!['Enter', 'Space', 'NumpadEnter'].includes(event.code)) return;
      event.preventDefault();
      event.stopPropagation();
      if (!event.repeat) holdDtmf(button, {code: event.code});
    });
    button.addEventListener('blur', () => {
      if (dtmfGesture?.button === button && dtmfGesture.code) releaseDtmf(true);
    });
    button.addEventListener('click', event => {
      if (event.detail === 0 && holdDtmf(button, {})) releaseDtmf();
    });
  }
  document.addEventListener('keyup', event => {
    if (!dtmfGesture?.code || dtmfGesture.code !== event.code) return;
    event.preventDefault();
    releaseDtmf();
  });
  dtmfDrawer.addEventListener('toggle', () => {
    if (!dtmfDrawer.open) releaseDtmf(true);
  });
  window.addEventListener('blur', () => releaseDtmf(true));
  document.addEventListener('visibilitychange', () => {
    if (document.hidden) releaseDtmf(true);
  });

  setInterval(render, 100);
  render();
})();
