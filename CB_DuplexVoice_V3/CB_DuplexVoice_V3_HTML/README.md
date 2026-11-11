# CB Duplex Voice V3 — RF & Audio Diagnostics

This **standalone experimental prototype** is based on the CB Duplex Voice V2
HTML interface. It retains the existing Master/Slave TDD voice operating mode,
Voice/Music audio processing, low-latency playback and radio controls, and adds
an in-band **RF & Audio Diagnostics** section.

## Open

Extract this folder next to the Arduino folder. Open `index.html` (multi-file) or `CB_DuplexVoice_V3_STANDALONE.html`
(one self-contained HTML file) in current
Chrome/Edge, or serve the HTML directory from `http://localhost` if the browser
blocks Web Serial or AudioWorklet on `file://`.

## Local check (no ESP required)

1. Leave the radio disconnected.
2. Expand **RF & Audio Diagnostics — Test Bench** (open by default).
3. Choose **Full bank** and click **Run digital loopback (no radio)**.
4. A chart and table should appear. This is a PCM8 encoder/analyzer sanity check,
   **not** a measurement of the RF transmitter or receiver.
5. Use **Export CSV** / **Export received WAV** to retrieve the local test data.

## Two-station RF test

1. Use the **same RF frequency and TDD timing preset** on both stations.
2. Connect both ESP8266 radios with **Connect & Listen**.
3. Start a session with **PTT** on station A (Master), then press PTT on B to
   become the active Slave speaker (hands-free). Other passive listeners can
   continue listening, but only the active A/B stations produce reports.
4. On the **Master** webpage, choose a test bank and click
   **Start Master → Slave benchmark**.
5. The test generator replaces the Master microphone temporarily, without
   touching the firmware, and transmits a short coded audio preamble followed by
   measured tones. The Slave webpage recognizes the preamble and records the
   *incoming raw 8-bit PCM* and audio *after its local receive filter*.
6. If **Auto-return test from Slave to Master** is checked on B, B starts the
   same bank after a quiet return interval. The Master analyzes the reverse
   transmission.
7. **Export the CSV from each receiving computer** to compare A→B and B→A.
   There is deliberately no test-result telemetry in this version.

Each recorded CSV includes: expected TX frequency/amplitude, estimated RX
frequency, received level in **audio dBFS**, the post-filter level, level error
relative to a 1-kHz reference, an approximate SNR and harmonic-distortion
estimate, erasures and a practical PASS/WARN/FAIL/BAND interpretation.

Available banks: **full**, **frequency response**, **amplitude / low-level**,
**silence / TDD noise**. The full bank takes roughly 12 seconds per direction.

## Measurement notes / limitations

- The signal generator bypasses the microphone, browser AGC and TX voice/music
  filters. This isolates the PCM8/TDD/RF path for the outgoing test signal.
- The receiver measures samples **before** and **after** the chosen local DSP
  filter. It is expected for Voice to roll off below ~125 Hz / above ~3 kHz,
  and Music below ~30 Hz / above ~5 kHz. The `BAND` result reflects this.
- These are *baseband audio* measurements. dBFS is not dBm, and neither power,
  occupied RF bandwidth nor FM deviation can be calibrated by this page alone.
  Use an SDR or RF measurement equipment for those.
- The test preamble requires a usable link. Severe loss can prevent the Slave
  from recognizing the bank. This is a useful diagnostic failure in itself.
- The current in-band preamble and reverse-bank protocol is experimental and
  has **not** been validated on physical ESP8266 radios in this environment.
- An 8-bit PCM stream has a practical floor near -42 dBFS; do not interpret
  close-to-floor estimates as precision analog measurements.
- A bench test temporarily sends audible tones over RF. Keeping **Mute received
  test tones locally** enabled and using headphones is recommended.
- A manually ended test restores normal microphone output. The existing PTT
  session controls and the separate FM Stereo/RDS project are unaffected.

## Files

- `index.html` — existing V2 UI plus diagnostic drawer.
- `CB_DuplexVoice_V3_STANDALONE.html` — same interface with CSS and JS embedded.
- `style.css` — existing styles plus drawer/table/chart.
- `app.js` — hooks the test engine into the existing session and DPX1 stream.
- `voice-dsp.js` — adds a raw/filtered PCM observation callback only; audio
  processing unchanged.
- `diagnostics.js` — test signal generation, in-band preamble, measurements,
  CSV/WAV creation.
- `diagnostics-ui.js` — user interface, chart, exports and local loopback.
- `audio-engine.js` — V2 AudioWorklet unchanged.

The Arduino sketch in the second folder is byte-for-byte the same logic as V2,
renamed only to match the Arduino IDE sketch folder. No firmware change is
required when V2 was already flashed successfully.
