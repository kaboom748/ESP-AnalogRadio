<img width="1628" height="900" alt="Capture d’écran 2026-11-10 205938" src="https://github.com/user-attachments/assets/ed1c3892-af73-4f37-b016-f70ca070ec68" />
# CB Duplex Voice — EASY + Current Interface

**User manual, technical explanation, and development history**  
**Release:** EASY front-panel edition, built on the CB Duplex Voice V4 radio and audio system  
**Platform:** one ESP8266 per station, 2.4 GHz experimental FM, desktop Chrome/Edge, Web Serial/Web Audio  
**Language of the application:** English  
**Status:** experimental working prototype; not a certified commercial transceiver

> **The central achievement:** two people can speak and hear each other in an apparently continuous, hands-free conversation while each station uses **only one ESP8266 RF transceiver**. The radios are **not physically transmitting and receiving at the same instant**. Instead, a precisely timed, complementary **time-division duplex (TDD)** schedule switches TX/RX so quickly that the browser can buffer and reconstruct both audio streams. This is *full-duplex user experience*, not simultaneous in-band RF full-duplex.

---

## 1. What this edition does

CB Duplex Voice is a **standalone 2.4 GHz voice-radio experiment** inspired by the faceplate of the CB FM Stereo project. The EASY tab reproduces the physical CB-style console, while **Current interface** retains the V4 engineering controls, runtime counters, and RF/audio test bench.

- **One ESP8266 and one USB serial connection per station.** The same firmware supports Master, active Slave, and passive listener roles.
- **CALL (SLAVE)** joins an already detected Master; **ANSWER (MASTER)** starts a new TDD session; **HANG UP** ends the active role and returns to monitoring.
- **Hands-free speech in both directions** after the session is established—no need to hold PTT.
- **Multiple passive listeners** can monitor the two time slots, but **only one Slave may occupy the reply time slot**.
- Live microphone gain and speaker volume, channel presets, APWR power adjustment, audio-device selection, and **12-key DTMF** during a call.
- Improved weak-signal amplitude handling using selectable **reversible µ-law encoding**, plus an optional local filter for audible TDD switching harmonics.
- A built-in **RF & Audio Diagnostics** bench for frequency response, quiet levels, noise, local loopback, and CSV/WAV exports.

### Important distinction from CB FM Stereo

The original **CB FM Stereo V12** is an independent **half-duplex FM-stereo/RDS** project. **This Duplex Voice edition is a separate, mono TDD voice design.** It does **not** transmit the 19 kHz stereo pilot, stereo subcarrier, RDS/RadioText, or FM 75 µs broadcast emphasis. Do not assume the two projects share the same on-air voice format just because both use FM at 2.4 GHz.

---

## 2. What you need

| Item | Requirement |
|---|---|
| Stations | Two ESP8266 boards for a two-way call; additional ESP8266 boards for passive monitors |
| Firmware | The included `CB_DuplexVoice_V4_Arduino.ino`, or compatible V2/V3/V4 warm-TDD firmware already operating correctly |
| ESP8266 CPU | **160 MHz** (required by the timing-sensitive sketch) |
| Computer | One compatible host computer and USB-serial connection per station |
| Browser | Current desktop **Chrome or Edge**, with Web Serial and Web Audio support |
| Audio | A microphone and headphones or speakers on each speaking computer |
| Serial link | **460,800 baud, 8 data bits, no parity, 2 stop bits (8N2)**, configured automatically by the page |
| RF | The same permitted 2.4 GHz carrier frequency and matching TDD timing on all stations |

**Headphones are recommended.** In **Music / Raw**, browser echo cancellation, automatic gain control, and noise suppression are requested OFF; loudspeakers can create a real acoustic echo or feedback loop. Actual browser/driver processing can vary.

### Installation and browser permissions

1. Extract the distribution to a normal directory. Open `CB_DuplexVoice_EASY_HTML/index.html` in Chrome/Edge, **or** open the provided standalone HTML page.
2. If the browser refuses an AudioWorklet or a `file://` origin, serve the HTML folder locally (for example, using a local HTTP server at `http://localhost`). Do not use a random public hosting site for a serial/microphone application.
3. Connect the ESP8266 via USB. Make sure no Arduino Serial Monitor or other application owns that COM port.
4. Permit the browser's **serial-port** and **microphone** prompts when requested. Audio output selection may require a supported browser API.
5. Keep the initial RF settings identical on all stations **before** pressing PORT.

**Windows Arduino IDE note:** avoid Unicode punctuation in your project path. A folder name containing a long dash (`—`) previously caused an ESP8266 core build helper to fail with a CP850 `UnicodeEncodeError`. Use simple ASCII paths, such as `C:\CB_DuplexVoice\CB_DuplexVoice_V4_Arduino\`.

### Flashing the radio firmware

1. In Arduino IDE, select the correct ESP8266 board and **160 MHz** CPU frequency.
2. Open the `.ino` file from its matching Arduino sketch directory.
3. Select the COM port and upload as usual.
4. Flash the **same compatible firmware** to both station boards.
5. Disconnect Arduino Serial Monitor before opening the web page.

The EASY edition did **not** alter the V4 `.ino`, TDD switching routine, audio engine, voice DSP, or µ-law implementation. If your boards already run the compatible firmware and communicate, reflashing is generally unnecessary. The existing hardware-specific RF sketch uses ESP8266 internal registers/ROM entry points; it is not a generic Arduino library that will necessarily run on another chip.

---

## 3. Recommended default configuration

The following are the **EASY edition's actual initial values**. Set the same RF frequency, TDD schedule, and on-air encoding on every station.

| Setting | Default | Notes |
|---|---|---|
| RF frequency | **2,442,312 kHz** | 2,442.312 MHz, **CH 07** |
| TX Power (APWR) | **63** | Special ESP8266 RF control value; **not** dBm |
| TDD preset | **Longer + guard** | **20 data / 8 guard** |
| Data samples per slot | **20** | Even number required by passive sampling |
| Guard samples | **8** | TDD transition/settling margin |
| Audio profile | **Music / Raw** | Tries to preserve quiet audio; browser processing requested OFF |
| RX RF gain | **0** | Manual RF gain code |
| RX VGA gain | **0** | Receiver VGA setting |
| RX baseband gain | **16** | Receiver baseband setting |
| Speaker volume | **100%** | May be lowered independently on either station |
| Microphone gain | **0 dB** | Adjustable live, −18 to +18 dB |
| On-air amplitude encoding | **Medium (µ-law 63)** | **Must match** on Master, Slave, and monitors |
| TDD harmonic filter | **ON** | Local playback filtering only |

**APWR is inverse to the intuitive idea of a volume control:** in this firmware, **0 is the strongest setting** and **191 the weakest**. Values **192 or greater must never be used**. `63` is the recommended initial value; it is **not a calibrated transmit-power figure**. Do not change APWR while connected.

### Channel presets

| Channel | Nominal frequency |
|---|---|
| **01** | **2,412,312 kHz** (2412.312 MHz) |
| **07** | **2,442,312 kHz** (2442.312 MHz) — default |
| **11** | **2,462,312 kHz** (2462.312 MHz) |

These are three project presets, **not** the 40 standardized CB channels at 27 MHz. They do not imply interoperability with ordinary Wi-Fi or broadcast FM receivers.

---

## 4. The EASY front panel — complete control reference

The **EASY** tab uses the same physical CB-radio artwork as the original CB FM Stereo faceplate. Clicking the controls opens the settings or performs the action described below; the **Current interface** tab remains the detailed engineering view.

| Control on the faceplate | What it does |
|---|---|
| **PORT** | Opens the browser's serial-port picker, connects to the ESP8266 and enters **passive listening**. Press again to disconnect. |
| **ANSWER (MASTER)** | Starts a new Master session **if no Master is currently detected**. The Master supplies the TDD slot clock. |
| **Red microphone CALL button (SLAVE)** | Joins an **existing detected Master**, subject to the reply slot appearing free. It does *not* start a Master session when none is detected. |
| **HANG UP** | Stops the active Master or Slave role and returns to passive monitoring **without intentionally closing PORT**. |
| **CHANNEL large right-hand dial** | Selects preset channels **01 / 07 / 11**. Click to advance, wheel to move either way, or right-click for reverse. Disconnect before tuning. |
| **FREQUENCY small right-hand dial** | Opens exact numeric RF frequency entry, in **kHz**. Disconnect before changing. |
| **VOLUME outer left dial** | Changes speaker volume in 5% steps. Click adds 5%; mouse wheel adjusts up or down. |
| **MIC inner left dial** | Changes microphone gain in 1 dB steps, including during a live call. |
| **MIC GAIN key** | Opens a slider and numeric setting for **−18 to +18 dB**. Changes take effect live. Avoid clipping at high gain. |
| **RX MODE key** | Opens **On-air amplitude encoding**: **Off / Medium / Strong**. This is a µ-law selection, **not a change of FM receiver modulation**. Disconnect all stations before switching to a different mode. |
| **TX POWER key** | Opens the **APWR 0–191** control. It applies at radio START, not continuously. Disconnect before changing. |
| **AUDIO key** | Preselects microphone input and speaker output. Device names and available choices come from the browser/operating system. |
| **DTMF keypad** | Hold `0–9`, `*`, or `#` for a two-tone signal. Locally monitored and inserted into the outgoing encoded PCM **during an active call**. |

### The LCD and meter displays

- **OFF:** browser/port is not connected.
- **READY:** connected and listening for a Master.
- **MASTER:** station is providing the radio time-slot schedule.
- **SLAVE:** station has the complementary speaking slot.
- **Channel and frequency:** chosen RF preset and nominal carrier.
- **L / R or RX / TX VU bars:** approximate microphone/transmitted PCM and received PCM **audio levels**, not wattmeters. The duplex overlay represents TX and RX concurrently, **not FM stereo**.
- **S / signal bars:** indicative marker/synchronization quality. **Not calibrated RSSI or a standard S-meter**: the DPX1 data frames do not deliver an RF-strength reading in dBm.
- **Mode indicator:** `LIN`, `MU63`, or `MU255` according to the on-air amplitude encoding choice.

If a setting panel is open below the radio, use its **Close** button when finished. The DTMF keypad may be collapsed/expanded using its header.

### AUDIO: selecting microphone and speaker

1. Press **AUDIO**.
2. If a microphone is not listed, use the permission/enable-audio control to let the browser enumerate devices.
3. Select the preferred **microphone**. The choice is saved for the **next call**; the speaking microphone is normally opened when the Master/Slave role starts.
4. Select the preferred **speaker**. This works directly only in browsers supporting `AudioContext.setSinkId()`. Otherwise the application uses the system-default output and reports that limitation.
5. Test at moderate speaker volume to avoid feedback.

### DTMF: what it does and does not do

The keypad reproduces the CB-style **12-key arrangement** and standard dual-tone pairs. A key is held to extend its tone. The current interface ensures approximately **90 ms minimum tone duration** and **50 ms minimum gap**. While DTMF is being sent, that tone replaces microphone audio in the outgoing PCM stream. A local audible copy is also generated.

**This is in-band audio DTMF, not a complete telephone exchange.** It does not by itself provide automatic dialing by telephone number, a network switch, or a guaranteed remote DTMF command interpreter. DTMF cannot be transmitted to the other station before a duplex audio session is active.

---

## 5. A complete two-station call

This example uses two boards and two PCs. Both boards have the same firmware, RF settings, and µ-law encoding.

### Station A — start the Master

1. In **EASY**, check the intended **CHANNEL** and the common configuration (default CH 07 / 20+8 / Medium).
2. Press **AUDIO** to choose the microphone and output, if needed.
3. Press **PORT**, select the ESP8266 COM port, and allow the serial connection. The display should move from **OFF** to **READY**.
4. Confirm no existing session is detected, then press **ANSWER (MASTER)**.
5. The board starts transmitting synchronization headers and speech fragments in its assigned alternating slots.
6. Speak normally. **Do not hold the CALL button or PTT**: this edition is hands-free once joined.

### Station B — join as the speaking Slave

1. Match Station A's **RF frequency, TDD timing and µ-law mode**.
2. Press **PORT** to enter **READY** listening mode.
3. Allow a moment for the Master to be detected. The advanced view shows **Remote Master** activity.
4. Press the **red CALL button** on the microphone.
5. Station B joins the complementary TDD speaking slot. Both users should now be able to **talk and hear at the same time from the user's perspective**.

### End or change a call

- Press **HANG UP** on a speaking station: it ends that station's active role and **returns to listening**. The other station may still be in its own session; HANG UP is not a guaranteed remote hang-up command.
- Press **PORT** again to close the serial connection completely.
- To change **channel, APWR, TDD timing, or µ-law**, first stop the call and **disconnect PORT** on the affected station. Apply compatible values to every participant, then reconnect.
- **MIC GAIN** and **VOLUME** can be adjusted without stopping a call. The local TDD harmonic filter can also be toggled while listening.

### Passive listeners (three or more stations)

Any additional station can press **PORT** and stay in **READY**. It does not transmit. The listener receives and combines audio from the alternating slots belonging to the Master and the one speaking Slave. **Do not press CALL on multiple Slaves at once.** There is currently no robust distributed reservation protocol for the reply slot.

A passive listener's downsampled audio representation differs from an active speaker's: the design uses half-rate samples from both slots so the same serial interface can carry the combined conversation.

---

## 6. Current interface (ADVANCED) and diagnostic tools

Click **Current interface** to access the original V4 setup and observability tools. The underlying radio session is the same—switching tabs does **not** create a second radio link.

### Controls found there

- Serial connection status and role (`OFF`, Master, Slave, passive).
- **Synchronization**, detected **Remote Master**, reply-slot indication, and audio sample rate.
- RF frequency, TDD preset and individual **Data samples / slot** and **Guard samples**.
- **TX Power (APWR)**, RX RF gain, RX VGA gain, RX baseband gain.
- **Voice** or **Music / Raw** audio profile.
- On-air amplitude encoding **Off / Medium / Strong**, and the local **TDD harmonic filter**.
- Received-sample gaps, transport CRC failures, valid DPX1 frames, playback-buffer size, underruns, and old-audio discard count.
- RF & Audio Diagnostics test banks, test chart, measurements, and export buttons.

The advanced page may still display the legacy **PTT — Start session** action inherited from the experimental prototype. In EASY, the clearer role-specific actions are **ANSWER (MASTER)** and **CALL (SLAVE)**. The legacy PTT command auto-chooses a role based on activity it detects; it is not necessary for the EASY workflow.

### Audio profiles

| Profile | Browser microphone processing requested | Typical TX/RX filters | Best use |
|---|---|---|---|
| **Voice** | Echo cancellation, noise suppression, AGC **ON** | TX: high-pass 125 Hz, low-pass 3 kHz; RX: high-pass 95 Hz, low-pass 3.2 kHz | Speech and noisy environments |
| **Music / Raw** (default) | Echo cancellation, noise suppression, AGC **OFF** | Approx. 30 Hz high-pass and 5 kHz low-pass on TX/RX | Preserving quieter or wider-band content |

**Music / Raw is not unprocessed, lossless, hi-fi audio.** There is still band-limiting, 8-bit quantization, companding, resampling, FM modulation, and RF burst switching. Also, the browser requests processing settings; OS drivers and audio devices may still apply effects.

### On-air encoding and TDD filter

- **Off (`LIN`)** is the older linear PCM8 reference format.
- **Medium (`MU63`) — default:** reversible symmetric µ-law companding, parameter 63. Quiet input amplitudes become larger on-air PCM excursions, and the receiver expands them after reception.
- **Strong (`MU255`)** uses stronger companding; it can aid weak details but may trade away accuracy at louder levels.
- **TDD harmonic filter — ON by default:** narrow **playback-only** notches centered on the predicted TX-burst rate and its harmonics. At the default 20/8 schedule, the first notch is around **1,500 Hz**. It does not remove all wideband hiss or recover samples that never arrived.

**All receivers need the matching compander**, including passive listeners. A mismatch is an audio-format error; changing this setting only at one end can make sound severely distorted. The notch is a **local playback preference**, so it need not match between stations. The diagnostic bench deliberately measures before this notch to avoid scoring its deliberate attenuation as a radio failure.

### RF & Audio Diagnostics — step-by-step

1. **Local software check:** in **Current interface → RF & Audio Diagnostics**, select **Run digital loopback (no radio)**. It exercises the audio-test generator/analyser without an ESP8266. Passing loopback **does not prove the RF path is good**.
2. **Over-air test:** establish Master and Slave speaking roles on two ESP8266 radios.
3. Choose **Full bank** (or **Frequency**, **Quiet audio / level sweep**, or **TDD silence / residual noise**).
4. On the Slave, keep **Auto-return test from Slave to Master** checked if you want the return direction measured.
5. On the Master, press **Start RF benchmark**.
6. Watch the received test results on each receiving PC. The Slave measures **Master → Slave**; the Master measures **Slave → Master** on the return test.
7. Use **Export CSV** on **each** computer. Use **Export received WAV** to listen to the recorded PCM for a particular run. Compare both files.
8. For a meaningful A/B comparison, repeat the **same** test with identical settings and only **one experimental variable changed** (for example TDD 12/4 vs 20/8; or µ-law Off vs Medium on *all* stations).

**Interpreting results:** `PASS`, `WARN`, `FAIL`, and `BAND` are heuristic audio-link verdicts. The displayed SNR and THD estimates are not lab-calibrated RF readings. The benchmark captures **recovered PCM after µ-law expansion but before local playback notches**; the “filtered” stream includes Voice/Music processing, not the final notch. RF power in dBm, modulation deviation, and occupied spectrum require separate measurement equipment such as an SDR and appropriate RF instrumentation.

---

## 7. How the ESP8266 TDD “full-duplex” achievement works

### The radio cannot really TX and RX simultaneously

A single ESP8266 has one RF chain. Ordinary **half-duplex PTT** either transmits or receives. If two ordinary FM transmitters use the **same frequency at the same time**, their waveforms do not conveniently mix into two intelligible conversations: FM capture/interference normally dominates.

The V22 experiment solved a different problem: **separate transmissions in time, not in RF frequency**. Each board switches between transmit and receive in complementary, precisely scheduled intervals. To the user, audio capture and playback remain continuous through buffering and reconstruction.

```text
Timeline (conceptual; A and B share one nominal RF frequency)

              Slot 0       Slot 1       Slot 0       Slot 1
Station A     TX voice     RX voice     TX voice     RX voice
Station B     RX voice     TX voice     RX voice     TX voice
Listener L    RX A         RX B         RX A         RX B

On-air slot:  [SYNC marker][accelerated PCM samples][guard]
At speaker:   decoded / time-restored continuous audio via buffer
```

The physical timing-critical scheduling is performed **inside the ESP8266 firmware** using its 160 MHz cycle counter and warm RF register switching, **not** by JavaScript timers or Web Serial events. This avoids relying on unpredictable browser scheduling for microsecond-level switches.

### Why “warm” switching matters

Retuning the Wi-Fi/RF channel on every slot would be impractical: the original radio firmware has a slow `tune()` path, including an explicit **40 ms wait**. A voice slot lasts only a few hundred **microseconds**. The warm-TDD firmware tunes **once at the start** of a session, preserves the chosen RF frequency, and toggles the necessary TX/RX radio-register state between slots. It does not perform a full channel/PLL retune at each transition.

The code also uses direct ESP8266 RF register accesses and ROM functions. This is specialized, hardware-dependent engineering and one reason a normal Wi-Fi/socket application would not reproduce these particular timings.

### Actual timing with the required 20/8 default

The burst-time sample clock is **120,000 samples per second**, equivalent to **8.333 µs per physical sample**. The V22 marker length is **12 samples**.

| Segment | Samples | Approximate duration |
|---|---:|---:|
| SYNC marker | 12 | **100 µs** |
| Useful TX audio | 20 | **166.7 µs** |
| Guard | 8 | **66.7 µs** |
| **One station's occupied time slot** | **40** | **333.3 µs** |
| A + B complete pair | 80 | **666.7 µs** |

That yields **1,500 TX bursts/second per speaking station** and a logical audio-transfer rate of approximately **30,000 PCM8 samples/second in each direction**.

The governing formulas are:

```text
Half-slot duration (seconds) = (data + marker + guard) / 120000
TX bursts per second       = 120000 / [2 × (data + marker + guard)]
PCM samples per direction  = 120000 × data / [2 × (data + marker + guard)]

For data=20, marker=12, guard=8:
  TX bursts/s              = 120000 / 80        = 1500
  Logical samples/s        = 120000 × 20 / 80   = 30000
```

This is *not* the same as 30,000 bit/s of compressed speech, nor is the application still operating the old **1,200-bit/s V.22 telephone data modem**. The radio transports sampled 8-bit PCM. The physical burst runs at **120 ksample/s while transmitting**, so a sequence which is played/reconstructed at **30 ksample/s** is delivered around **4× faster on air**. The browser resamples ordinary microphone audio into this logical stream and presents received samples at normal speed; the voice is not left permanently high-pitched.

### The tiny synchronization marker

The RF **marker** is a roughly **100 µs timing signature** at the start of each transmitted slot, not a conventional audible single-frequency beep. The receiver detects it from changes in the FM/IQ phase response, estimates the slot boundary, and uses it to align the following PCM data. The marker is not meant to be sent to the speaker.

- **Master / START A:** supplies the reference slot clock.
- **Slave / START B:** first listens for Master markers, then schedules its own TX in the complementary intervals; the code makes small timing corrections after newly detected markers.
- **Listener / START L:** sends nothing; follows markers and watches both time slots. To fit both directions into the same serial capacity, the receiver takes **alternate physical samples** from each time slot and reconstructs/mixes the two audio streams.
- **A missed marker:** timing may be predicted from the last locked clock, but **an unconfirmed RX slot must not be played as real speech**. This distinction prevents the Master from hearing its own TX residue as a false Slave response.

### Why the UART is surprisingly important

The host sends/receives PCM through **460,800 baud, 8N2** UART. The firmware uses **DPX1** receive frames with 128 audio bytes, a header, and CRC16: **146 bytes per frame**. An additional conservative pacing allowance of **12 serial bit times per byte** protects the stream.

With 20/8 and 30,000 samples/second, the approximate paced serial budget is:

```text
30000 PCM bytes/s × (146 / 128 frame overhead) × 12 bits/byte
≈ 410,625 paced bits per second
```

This fits below 460,800 baud with some margin. A seemingly better 20/4 slot schedule pushes the logical PCM stream toward **33,333 samples/s**, close to **456,250 paced bit/s**—nearly all available UART capacity. Consequently, more data in each TDD slot is **not automatically better**. The HTML intentionally restricts timing choices to protect the stream.

### Why the sound can be continuous

Microphones collect audio continuously while the RF alternates. The DSP resamples it to the lower **logical PCM rate**, then the ESP8266 sends those samples in short fast bursts. On RX, the firmware and browser rebuild the sample timeline and the AudioWorklet plays a small buffered stream. The two users may speak at once because both audio pipelines run continuously **outside the RF time slots**.

The present AudioWorklet aims for roughly **120 ms** of buffered playback and discards old audio if queued playback exceeds roughly **250 ms**. These are **playback-queue settings, not a guaranteed end-to-end latency figure**; microphone, serial, resampling, synchronization, and RF transport also add delay.

---

## 8. The obstacles solved on the way here

This project was not a one-step conversion from FM half-duplex to full-duplex. The following sequence records the **observations, changes, and remaining limitations** behind the present edition.

### A. Proving alternating RF worked at all — V22/RadioBidi experiments

The earlier **V22 Duplex** project already moved signed 8-bit PCM samples between ESP8266 hosts over complementary radio time slots. A V.22 telephone-style modem running at 1,200 bit/s in both directions used this sampled link to exchange file data with CRC/FEC. This provided the key proof of concept: **one ESP8266 on each side can switch warm TX/RX rapidly enough to maintain a bidirectional sampled transport**. The voice version bypasses the V.22 telephone modulation and puts speech samples into that PCM transport directly.

### B. Finding the correct synchronization design

Two independent radios can both be RX or both TX unless their schedules are coordinated. The working design uses a **Master clock** and a **Slave that locks onto RF markers**. EASY makes the roles explicit: **ANSWER = Master**, **CALL = Slave**. A passive listener can wait for an existing Master. Some random contention delay and carrier detection reduce collision risk, but **simultaneous hidden-node attempts are not fully solved**.

### C. Avoiding slow RF retunes

An ordinary radio-tuning call with tens of milliseconds of delay cannot run thousands of times a second. The solution is **warm TX/RX register switching on one already-tuned carrier**, while the microsecond schedule stays on the chip.

### D. Separating a real incoming voice from our own RF residue

During early testing, the **Master alone could hear a radio-like echo** in the expected Slave RX slot. After the Slave started transmitting, the false echo was replaced by valid incoming audio. The receive path was changed to **gate speech to genuinely confirmed RF markers**, leaving unconfirmed predicted slots silent. This substantially improves the *logic* of empty-slot handling; it does not remove ordinary acoustic echo from speakers and microphones.

### E. The audible TX/RX switching whine

At the old **12 data / 4 guard** schedule, each station transmitted about **2,143 bursts/s**. A real recording exhibited narrow components consistent with this rate and harmonics. We added longer **20/8** timing as the default, reducing the switching repetition to **1,500 bursts/s** and increasing the guard margin. We later added a **playback-only harmonic-notch filter**. A notch suppresses narrow stable tones; it cannot fully remove broad-band RF hiss or switching clicks, and it can also suppress genuine musical notes at the same frequencies.

### F. Why quiet music disappeared while speech passed

The initial browser capture requested **echo cancellation, noise suppression, and automatic gain control**, all intended primarily for speech. Quiet music and sustained tones could be classified as noise or echo. The **Music / Raw** profile requests these browser features **OFF** and widens the audio filters compared with the Voice profile. This helps, but the transmitter remains a limited-bandwidth PCM8/FM experimental chain.

### G. Low-amplitude fidelity and pulse-width quantization

The ESP8266 FM waveform is produced through an RF-register pulse mechanism. Extremely small pulse-width requests are accumulated rather than always emitted immediately; at very low input levels the effective response can become nonlinear. Two V3 diagnostic CSVs (one in each direction, using the older 12/4 schedule) showed similar weak-level losses. For a nominal 1 kHz tone:

| Input level | Measured Master → Slave | Measured Slave → Master |
|---|---:|---:|
| −6 dBFS | −7.09 dBFS | −6.49 dBFS |
| −12 dBFS | −13.19 dBFS | −12.97 dBFS |
| −18 dBFS | −19.82 dBFS | −19.42 dBFS |
| −24 dBFS | −30.76 dBFS | −30.78 dBFS |
| −30 dBFS | −40.76 dBFS | −40.36 dBFS |
| −36 dBFS | −61.81 dBFS | −62.46 dBFS |

*Values are the earlier test bench's recovered-PCM level estimates; they are **not** calibrated RF power or evidence that a single firmware component is exclusively responsible.*

The V4 solution was **reversible µ-law amplitude companding** (Off / Medium / Strong) at the HTML DSP layer. It sends weak audio as larger PCM8 values and expands them back on receive, instead of adding a DC BIAS that could move the FM carrier center. **Medium µ-law 63 is now the required default**. This improves the representation of low-level signals in the software chain, but RF fidelity improvement and distortion trade-offs still require direct A/B measurements on hardware.

### H. Fixing a four-second audio delay

An early version showed a playback buffer around **3,983 ms**. Even with perfectly synchronized TDD slots, a four-second delayed voice is unusable for conversation. The low-latency revision capped the reader's queue to around **250 ms** and returned it to a **120 ms target** when necessary. It reports **Audio underruns** and **Old audio discarded** so unstable clocks or transport stalls can be diagnosed. A short buffer reduces delay but demands more consistent delivery.

### I. Browser loading, USB stability, and Windows build problems

- **AudioWorklet `blob:null/...` error:** local `file://` pages could fail to load a blob-based worklet. A `data:` module loader was used, with `http://localhost` as a fallback.
- **Serial disconnect/restart during TX:** observed during prototype testing and investigated around role transitions; the operator later reported the serial issue was no longer blocking. It must not be presented as conclusively root-caused or universally fixed.
- **APWR:** kept within **0–191** after reports that larger values destabilized an ESP8266.
- **Windows path encoding:** an en dash/em dash in a downloaded folder name caused an Arduino ESP8266 build helper's `UnicodeEncodeError`; simple ASCII paths avoid this issue.
- **File downloads:** browser/chat attachment links occasionally failed; a standalone HTML file and a personal project Page were used as alternative distribution routes. This is a distribution problem, not part of the RF algorithm.

### J. Making it look and behave like the original CB

Finally, the EASY front panel reused the **actual CB FM Stereo artwork** rather than inventing a different radio. The interface gained explicit CALL/ANSWER/HANG UP roles, the original 12-key DTMF layout, APWR, channel knobs, VU and signal indicators, AUDIO device presets, and live MIC GAIN. The **V4 radio firmware and core audio/DSP files remained unchanged** for this visual/interaction edition.

---

## 9. Troubleshooting by symptom

| Symptom | Likely area | What to check |
|---|---|---|
| `PORT` does not open a COM port | Browser permissions or OS port ownership | Use desktop Chrome/Edge; close Arduino Serial Monitor; check USB cable/driver; reconnect board if the OS truly loses the device. |
| `AudioWorklet ... blob:null` or module-load error | Local-file browser origin | Use the EASY HTML version with the `data:` loader or serve the folder from `http://localhost`. |
| `Firmware radio ... not ready` | Wrong board, sketch, or CPU speed | Confirm correct ESP8266 port, matching warm-TDD sketch, **160 MHz**, and correct serial settings. |
| `CALL` reports **No Master detected** | No stable remote synchronization | Press **ANSWER** on the Master first; verify channel, RF reception and shared TDD schedule. |
| `ANSWER` reports an existing Master | Channel already occupied | Remain listening or use **CALL** to join if the reply slot is free. |
| `CALL` reports reply slot busy | Another Slave already detected | Only one speaking Slave per session; remain passive. Detection is advisory, not collision-proof. |
| Loud hiss or high-pitched tone | TDD artifacts, RF noise, playback filtering | Compare notch ON/OFF, choose the default **20/8**, check sync/lost samples and headphone monitoring. |
| Master hears itself when alone | Empty RX slot, acoustic feedback, or RF residue | Confirm current firmware's marker-confirmed receive gating; use headphones; distinguish true RF leakage from local sound monitoring. |
| Speech is acceptable but soft music disappears | Browser enhancements, quantization, weak PCM fidelity | Choose **Music / Raw**, match **Medium µ-law 63** at all stations, and run the quiet-level test bank. |
| Distorted or unnatural volume | Different µ-law modes, too much mic gain, clipping | Confirm the same encoding on all stations; set MIC GAIN toward **0 dB** and check TX VU levels. |
| Delayed speech or buffer grows | Audio backlog, browser stalls, serial pacing | Check **Audio buffer**, **Audio underruns**, **Old audio discarded** and valid DPX1 frames. The worklet trims old audio but cannot repair RF outages. |
| DTMF heard locally but not remotely | No call active or mismatch in stream | Ensure a Master/Slave session is established; hold key; verify remote audio and matching on-air mode. |
| RX SIGNAL bars do not match SDR readings | Indicative synchronization display | Do not interpret these bars as calibrated RSSI, dBm, or S-units. |
| Arduino compilation fails before parsing sketch | Unicode folder path on Windows | Move to an ASCII-only project directory, then reopen the `.ino`. |
| Radio stops after browser disappears | Safety timeout | TX roles have a roughly 2-second no-host-PCM stop condition. Reconnect safely rather than assuming indefinite autonomous TX. |

For a reproducible engineering report, record the **firmware revision, channel, TDD timing, encoding mode, audio profile, APWR, RX gain, headphone setup, sample-loss/CRC counts, and a diagnostic CSV from each direction**.

---

## 10. Engineering limitations and responsible use

1. **This is TDD, not true simultaneous TX/RX RF.** Both voices can be live to users because the buffers reconstruct the alternate fragments.
2. **One Master and one active Slave maximum per time-slot pair.** Additional users are passive; the software does not implement a robust multi-speaker TDMA conference or collision-free hidden-node arbitration.
3. **No calibrated RF measurements in HTML.** Carrier power, RF deviation, occupied bandwidth, adjacent-channel energy, and actual spectrum purity require suitable RF instruments.
4. **Not hi-fi, not FM stereo, not HD Radio.** Music / Raw is still mono and frequency-limited; the on-air format is custom sampled FM TDD, not NRSC-5 or standard Wi-Fi networking.
5. **V4 companding is an on-air format choice.** Mismatched settings cause distortion; the local notch does not alter transmitted PCM.
6. **Timing and browser behavior vary.** 120 ms playback target does not guarantee a particular mouth-to-ear latency. CPU load, Web Serial scheduling, microphone drivers, and RF losses matter.
7. **Do not exceed the firmware's APWR range.** APWR does not map directly to certified radiated power. Do not assume default settings are legally compliant.
8. **RF compliance is not established by this project.** Observe local rules for 2.4 GHz emissions, transmitter authorization, spurious outputs, antenna configuration, and permissible power. Prefer shielded/attenuated bench tests during development.
9. **Hardware verification is ongoing.** The operating observations described here come from the project tests and measurements supplied during development; they are not a claim of independent third-party certification or calibrated laboratory validation.

---

## 11. File layout and unchanged engines

A complete EASY release typically contains these project directories:

```text
CB_DuplexVoice_EASY_HTML/
  index.html                 # EASY + Current interface
  style.css                  # V4 interface styling
  facade.css                 # CB-style front-panel overlay
  facade-ui.js               # front-panel controls, DTMF and live UI wiring
  facade-cb.png              # original CB-inspired radio artwork
  app.js                     # existing session controller + UI integration
  audio-engine.js            # unchanged V4 AudioWorklet capture/playback
  voice-dsp.js               # unchanged V4 voice PCM, resampling and notch
  signal-shaping.js          # unchanged V4 µ-law encoder/decoder
  diagnostics.js             # V3/V4 signal test engine
  diagnostics-ui.js          # test results and CSV/WAV exports
  CB_DuplexVoice_V4_STANDALONE.html
  README.md / README_EASY.txt

CB_DuplexVoice_V4_Arduino/
  CB_DuplexVoice_V4_Arduino.ino
  README.md
```

The standalone HTML builds all essential browser resources into one file for convenience. **Use either the HTML folder's `index.html` or the standalone file, not both simultaneously on the same serial port.**

The EASY implementation modifies **presentation and controller wiring only**; its Arduino sketch and core V4 audio/signal-processing files remain byte-identical to the verified V4 distribution. This manual documents that specific edition, not hypothetical unimplemented features.

---

## 12. Quick-reference checklist

**Before connecting:**

- [ ] Both ESP8266 boards run compatible firmware at **160 MHz**.
- [ ] Same **CH 07 / 2,442,312 kHz** (or another matching channel).
- [ ] Same **TDD 20/8** and **µ-law Medium (63)** on every radio.
- [ ] **APWR 63**, RX RF gain 0, RX VGA 0, baseband 16 initially.
- [ ] **Music / Raw**, local TDD filter ON, suitable volume and mic gain.
- [ ] Chrome/Edge permissions granted; headphones preferred.

**To talk:**

- [ ] Press **PORT** on both stations.
- [ ] First station presses **ANSWER (MASTER)**.
- [ ] Second station detects the Master, then presses the red **CALL (SLAVE)** button.
- [ ] Talk normally, hands-free. Use **MIC GAIN**, **VOLUME**, and DTMF as needed.
- [ ] Press **HANG UP** to return to listening; press **PORT** to disconnect.

**To diagnose:**

- [ ] Open **Current interface → RF & Audio Diagnostics**.
- [ ] Run **digital loopback** first, then a full over-air bank.
- [ ] Export **CSV and WAV on both receiving computers**; compare Master → Slave and Slave → Master.
- [ ] Change only one variable at a time and document the result.

---

**Bottom line:** CB Duplex Voice demonstrates that a single, carefully controlled ESP8266 RF chain can provide a compelling **hands-free duplex-like conversation** by switching its 2.4 GHz FM link between complementary microsecond TX/RX slots. The achievement rests on *warm RF switching, marker timing, a carefully budgeted UART, accelerated PCM bursts, software audio reconstruction, and a large amount of debugging*. The EASY front panel turns that experimental machinery into a CB-style radio that can actually be operated without living in the engineering controls.

