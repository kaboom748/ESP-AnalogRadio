# CB Duplex Voice V4 — Audio Fidelity and TDD Noise Filtering

Independent experimental ESP8266 TDD prototype, **not** an update to CB FM Stereo V12. Preserves the V3 radio protocol, Master/Slave/passive roles, diagnostic banks and low-latency audio. All user-interface text is in English.

## What changed in V4

**Audio Fidelity / On-air amplitude encoding**

- `Off` — original linear PCM8 from V3 (baseline comparison).
- `Medium` — symmetric, reversible mu-law companding with mu=63.
- **`Strong` (default)** — symmetric, reversible mu-law companding with mu=255. It transmits weak signals as larger PCM8 excursions, then expands them on RX. This avoids the large DC RF-frequency offset associated with adding a constant bias. Strong settings may slightly increase quantization error on loud signals; compare modes by measurement.
- The compander operates in the HTML DSP **on TX and RX**. **All stations (including passive listeners) must select the same mode before connecting**. A mismatch causes nonlinear, incorrect audio. This is *not* a transparent over-the-air format to V3 when Medium/Strong is used.
- `-128` is still reserved for the serial STOP/erasure code and is never emitted as a PCM sample.

**TDD harmonic filter (checked by default)**

- A narrow bank of notch filters reduces the clock-related audible lines near the TX burst rate and its harmonics. At TDD 20/8 the first notch is around **1500 Hz**; at 12/4 it is **2142.9 Hz**.
- The filter is applied **locally to playback**. It does **not** change TX, RF timing, power or DPX1 frames, and it can be toggled while connected. The notch can remove a genuine music tone near exactly those frequencies; disable it to compare.
- The diagnostic results continue using restored PCM before the notch (otherwise the 1500 Hz benchmark tone would fail solely because of the local playback filter).

## How to start

1. Keep your **V2/V3 TDD Arduino firmware** if it is already working. V4 ships the same RF sketch (renamed only to match the Arduino directory); **reflashing is optional**.
2. Extract the project. Open `CB_DuplexVoice_V4_HTML/index.html` in current Chrome or Edge. Alternatively use the included `CB_DuplexVoice_V4_STANDALONE.html` (single file), or serve the directory from `http://localhost` if the browser blocks `file://` worklets.
3. On **both stations**, choose the same frequency, TDD timing, and **On-air amplitude encoding** before connecting. Start with **20/8 / Strong / TDD filter ON**.
4. Click `Connect & Listen`, then use `PTT` on one station to become Master, then on the other station to become the Slave speaker.
5. Compare Voice vs Music, weak sounds, and notch on/off. Use headphones to avoid acoustic feedback.

## RF & Audio Diagnostics

The existing V3 diagnostic banks, auto-return Master→Slave→Master, downloadable CSV and WAV, and offline loopback are preserved.

- Choose `Full bank`, start the RF test on the Master, enable auto-return at the Slave. Download **one CSV per direction**.
- `Raw RX` in V4 means **restored / expanded PCM** before Voice/Music and TDD-notch filters, **not the companded on-air byte value**. `Filtered RX` is after the Voice/Music profile but **before** the notch.
- Compare especially `1 kHz, -24/-30/-36/-42 dBFS` across **Off**, **Medium**, **Strong** (change mode on both stations between sessions). Compare audible TDD lines with notch ON versus OFF.
- The CSV SNR and THD are approximate baseband indicators; they are not calibrated RF or hardware measurements.

## Limitations

- Companding mitigates amplitude quantization and pulse-width nonlinearity, but cannot reconstruct lost RF samples or guarantee actual RF improvement before bench tests.
- A notch suppresses **stationary tonal harmonics**, not broadband RF hiss, clicks from lost packets, or all switching artifacts.
- This is *not* a DC-bias change: the firmware's FM center frequency, APWR validation and switching schedule remain unchanged.
- Tests performed here: JavaScript syntax; quantitative loopback at all three companding settings; illustrative pulse-width threshold model; notch selectivity; Chromium UI + CSV/WAV; mocked port role changes. **No two-board RF test** or actual Arduino core compilation in this environment.

## Files

- `index.html`, `style.css` — V4 interface and additive fidelity controls.
- `signal-shaping.js` — symmetric compander/expander and PCM8 mapping.
- `voice-dsp.js` — uses compander for TX/RX and local notch filter for playback.
- `diagnostics.js` — transmits/receives test tones using the chosen companding mode.
- `diagnostics-ui.js` — offline loopback uses the same mode.
- `app.js` — session UI and setting propagation.
- `audio-engine.js` — unchanged audio capture/worklet/low-latency playback from V3.
- `CB_DuplexVoice_V4_STANDALONE.html` — self-contained version of the multi-file UI.
