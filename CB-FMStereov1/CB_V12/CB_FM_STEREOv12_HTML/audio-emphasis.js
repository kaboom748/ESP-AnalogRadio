'use strict';

// Shared selection for Easy / Current. Applies to one worklet handling TX and RX.
(() => {
  let microseconds = 75; // FM broadcast default in North America.
  const labels = {0:'Off',50:'50 µs — Europe',75:'75 µs — North America'};
  const inputs = [...document.querySelectorAll('input[data-audio-emphasis]')];
  const readouts = [...document.querySelectorAll('[data-emphasis-label]')];

  function apply(value) {
    const next = Number(value);
    if (![0, 50, 75].includes(next)) return;
    microseconds = next;
    for (const input of inputs) input.checked = Number(input.value) === next;
    for (const readout of readouts) readout.textContent = labels[next];
    audio?.node.port.postMessage({type:'emphasis',us:next});
  }
  for (const input of inputs) input.addEventListener('change', () => {
    if (input.checked) apply(input.value);
  });
  window.cbAudioEmphasis = Object.freeze({value:() => microseconds});
  apply(75);
})();
