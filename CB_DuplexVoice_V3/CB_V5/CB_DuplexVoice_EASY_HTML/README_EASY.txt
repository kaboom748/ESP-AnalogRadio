CB DUPLEX VOICE — EASY + ADVANCED FACEPLATE
========================================

This is a USER INTERFACE update to CB Duplex Voice V4.
The ESP8266 .ino radio firmware is byte-for-byte identical to V4.
The existing audio-engine.js, voice-dsp.js, signal-shaping.js and diagnostics
scripts are byte-for-byte identical to V4. Only index.html, app.js and new
facade-ui.js/facade.css/facade-cb.png implement the user-facing additions.

QUICK START
-----------
1. Open index.html in Chrome or Edge (or use the standalone HTML).
2. Ensure both radios have the V4 .ino firmware (ESP8266 at 160 MHz).
3. Choose the same RF channel, TDD timing and on-air amplitude encoding on all radios.
4. Press PORT to choose the ESP8266 serial port and enter passive listening.
5. Station designated Master: press ANSWER (MASTER).
6. Another station: after detecting the Master, press CALL on the red microphone button.
7. Both stations can now speak hands-free. HANG UP returns to passive listening.
8. PORT disconnects the serial port.

DEFAULTS (same on all stations, when applicable)
-------------------------------------------------
RF frequency: 2442312 kHz (CH 07)
Other reference presets: CH 01 2412312 kHz; CH 11 2462312 kHz
APWR: 63 (0 strongest, 191 weakest — do not exceed 191)
TDD: 20 data / 8 guard (1500 TDD bursts/s)
Audio profile: Music / Raw
On-air amplitude encoding: Medium (mu-law 63)
TDD harmonic filter: ON
RX RF gain: 0; RX VGA: 0; RX baseband gain: 16
Speaker volume: 100%; microphone gain: 0 dB

EASY CONTROLS
-------------
Red microphone button: CALL (join as Slave). Requires a received Master.
ANSWER (MASTER): starts a new Master session if the channel seems free.
HANG UP: ends the active role; keeps port connected and resumes passive RX.
PORT: connects/disconnects the ESP8266 serial port.
CHANNEL big dial: cycles 01 / 07 / 11; mouse wheel changes direction.
Speaker volume left dial: click ring +5%, wheel +/-5% (0-100%).
MIC inner dial + MIC GAIN: live microphone gain -18 to +18 dB.
FREQUENCY small dial: opens numeric kHz tuning.
RX MODE: Off / Medium / Strong on-air amplitude encoding; change before PORT.
TX POWER: APWR 0..191, applies at radio start; disconnect to change.
AUDIO: choose speaker and microphone devices; allow microphone access to enumerate.
DTMF: original 12-key layout. Hold a key for local tone and simultaneous
      encoded RF PCM8 transmission during an active full-duplex call.
      Minimum 90 ms tone and 50 ms gap. Microphone audio is replaced by the
      DTMF tone while the key is active. The underlying audio engine is untouched.

ADVANCED TAB
------------
The original V4 current interface and RF & Audio Diagnostics test bench are retained.
V4 link parameters remain adjustable before connection as in the prototype.
The front panel knobs mirror the existing underlying settings.

LIMITATIONS
-----------
* RX LCD 'S' is an indicative TDD marker/sync-quality display, not a calibrated
  RF signal-strength meter, because DPX1 does not provide receiver RSSI.
* RX/TX VU meters estimate PCM levels, not RF power.
* Changing RF channel, APWR or mu-law during a live session is blocked for
  safety/peer compatibility: HANG UP then disconnect PORT before changing.
* Speaker selection requires AudioContext.setSinkId() support. If unavailable,
  the default system speaker is used; the UI reports this limitation.
* Master arbitration and hidden-node collisions remain original V4 limitations.
* All RF performance still needs physical ESP8266 testing.
* For Windows Arduino IDE compatibility, keep project folder names ASCII only.
