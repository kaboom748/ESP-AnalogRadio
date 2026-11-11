/* CB Duplex Voice V4 — link amplitude companding.
 * An 8-bit FM pulse modulator has an amplitude-dependent minimum pulse width.
 * Symmetric companding sends quiet audio using larger on-air deviations, then
 * expands it back after RX. This is NOT a DC frequency bias; no hardware
 * register or RF timing change is required. Both talkers and passive listeners
 * must select the same companding mode for compatible audio.
 */
(function (scope) {
'use strict';
const METHODS=Object.freeze({
  off:Object.freeze({label:'Off (V3 linear PCM8)',mu:0}),
  medium:Object.freeze({label:'Medium (mu-law 63)',mu:63}),
  strong:Object.freeze({label:'Strong (mu-law 255)',mu:255})
});
const clip=x=>Math.max(-1,Math.min(1,Number.isFinite(x)?x:0));
const table={};
for(const [key,{mu}] of Object.entries(METHODS)){
  const restore=new Float32Array(255);
  for(let code=-127;code<=127;code++){
    const y=code/127;
    restore[code+127]=mu ? Math.sign(y)*Math.expm1(Math.log1p(mu)*Math.abs(y))/mu : y;
  }
  table[key]=restore;
}
function encode(input,mode='strong') {
  const profile=METHODS[mode];if(!profile)throw RangeError('Invalid amplitude-companding mode');
  const x=clip(input),mu=profile.mu;
  const y=mu?Math.sign(x)*Math.log1p(mu*Math.abs(x))/Math.log1p(mu):x;
  // The source serial protocol reserves -128 for STOP. Never emit that byte.
  return Math.max(-127,Math.min(127,Math.round(127*y)));
}
function decode(code,mode='strong') {
  if(!METHODS[mode])throw RangeError('Invalid amplitude-companding mode');
  if(code===-128 || !Number.isInteger(code) || code < -127 || code > 127)return 0;
  return table[mode][code+127];
}
const api=Object.freeze({METHODS,encode,decode});
scope.CBDuplexSignalShaping=api;
if(typeof module==='object'&&module.exports)module.exports=api;
})(typeof globalThis==='object'?globalThis:this);
