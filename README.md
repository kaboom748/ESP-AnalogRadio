# ESP-AnalogRadio

<img width="1087" height="1011" alt="Capture d’écran 2026-11-07 145341" src="https://github.com/user-attachments/assets/f1d2ebef-0edf-4123-a1dd-574c613a6238" />
<img width="1007" height="874" alt="2" src="https://github.com/user-attachments/assets/ccd4b288-7853-4c76-a9a0-d599a4caa745" />
<img width="735" height="553" alt="Capture d’écran 2026-11-07 143836" src="https://github.com/user-attachments/assets/a2b465e2-7f78-475f-94e8-6807bf7e265f" />

Experimental **2.4 GHz analog radio transmitters for the ESP8266**. The collection includes amplitude modulation (AM), mono wideband FM (WFM), stereo FM with a 19 kHz pilot, and single-sideband transmission in USB and LSB. Each Web Serial build pairs an Arduino sketch with a browser player that turns a local audio file into a stream the ESP8266 can transmit.

On the HackRF bench, the mono FM build reached **27.49 dB tone SNR**, and the stereo FM build measured **30.90 dB left-to-right separation** at 997 Hz. The measurement conditions and results for all four Web Serial builds are documented below.

> [!WARNING]
> **Install a suitable heatsink before continuous operation.** The RF section can become hot during sustained transmission. Count warm-up and idle CW as transmit time. Fit the heatsink to the module's heat-spreading surface with a suitable thermal interface, keep metal away from the PCB antenna and exposed contacts, and provide airflow. Monitor temperature in the actual enclosure and stop if the module overheats, resets, or becomes unstable. Preserve the module's antenna keep-out area. [Espressif hardware design guidelines](https://documentation.espressif.com/esp8266_hardware_design_guidelines_en.pdf)

> [!CAUTION]
> **Follow local RF emission laws.** Use a shielded bench setup with appropriate RF attenuation for initial tests. Before radiating, verify the authorization, allowed frequency and emission type, occupied bandwidth, output or EIRP limits, duty cycle, unwanted emissions, RF exposure, and interference rules in your jurisdiction. Assess analog transmission and CW separately from the board's original Wi-Fi certification. In Canada, consult ISED's applicable standards and the amateur-radio content restrictions before transmitting. Keep music and other programme material in a contained bench setup. [ISED RSS-Gen](https://ised-isde.canada.ca/site/spectrum-management-telecommunications/en/devices-and-equipment/radio-equipment-standards/radio-standards-specifications-rss/rss-gen-general-requirements-compliance-radio-apparatus) · [ISED RSS-247](https://ised-isde.canada.ca/site/spectrum-management-telecommunications/en/devices-and-equipment/radio-equipment-standards/radio-standards-specifications-rss/rss-247-digital-transmission-systems-dtss-frequency-hopping-systems-fhss-and-licence-exempt-local) · [Canadian Radiocommunication Regulations, section 47](https://laws-lois.justice.gc.ca/eng/regulations/SOR-96-484/section-47.html)

> [!IMPORTANT]
> **All four Web Serial sketches start a CW carrier at boot**, before the browser connects. Playback stop and serial disconnect leave the carrier on. Use **ARRÊT RF** while connected, or remove power. Power cycling starts CW again. Set up RF containment before flashing, resetting, or powering the board.

## Contents

- [Choose a build](#choose-a-build)
- [Hardware and firmware setup](#hardware-and-firmware-setup)
- [Run a Web Serial build](#run-a-web-serial-build)
- [AM over Web Serial](#am-over-web-serial)
- [Mono FM over Web Serial](#mono-fm-over-web-serial)
- [Stereo FM over Web Serial](#stereo-fm-over-web-serial)
- [USB and LSB over Web Serial](#usb-and-lsb-over-web-serial)
- [Wi-Fi-only builds](#wi-fi-only-builds)
- [Bench measurements](#bench-measurements)
- [Troubleshooting](#troubleshooting)
- [Gallery and credits](#gallery-and-credits)

## Choose a build

| Project | Mode | Audio delivery | Nominal programme frequency | Nominal warm-up CW |
| --- | --- | --- | ---: | ---: |
| [AMSerialHQv1](AMSerialHQv1/) | AM mono | Browser → USB serial | 2442.312500 MHz | 2442.625000 MHz |
| [FMSerialHQv1](FMSerialHQv1/) | Wideband FM mono | Browser → USB serial | 2442.312500 MHz | 2442.625000 MHz |
| [FMStereoHQv1](FMStereoHQv1/) | Wideband FM stereo | Browser → USB serial | 2442.312500 MHz | 2442.625000 MHz |
| [SSBTestv1](SSBTestv1/) | SSB in USB or LSB | Browser → USB serial | 2442.312500 MHz suppressed-carrier reference | 2442.390625 MHz |
| [WIFIOnlyTest](WIFIOnlyTest/) | AM mono, SSB, or WFM mono | Browser → Wi-Fi → LittleFS → RF | 2442.000000 MHz | 2442.078125 MHz |

The frequency table gives the configured values. All checked-in builds use the 2442 MHz channel-7 synthesizer base and a nominal **78.125 kHz per K step**. The ESP8266 oscillator and the receiver reference affect the frequency displayed on the SDR. Locate the actual signal on the waterfall before choosing an exact tuning frequency.

**CW** is an unmodulated carrier used for warm-up and as a tuning reference. **APWR** is a unitless RF power control code. Measuring output power in dBm or estimating EIRP requires a calibrated RF setup.

## Hardware and firmware setup

- **Transmitter** — An ESP8266 board such as a D1 mini with a reliable USB data cable and power supply. The Web Serial builds need a USB-to-UART bridge and driver that sustain **2,000,000 baud, 8N1**. Choose **160 MHz** for the ESP8266 CPU.
- **Receiver** — A 2.4 GHz-capable SDR such as a HackRF. Use AM, WFM, or USB/LSB demodulation as appropriate. Stereo reception also needs a 19 kHz-pilot stereo multiplex decoder. Use appropriate attenuation and start with low receiver gain to avoid overloading the input.
- **Development tools** — Arduino IDE with the **ESP8266 Arduino core 3.1.2** as used for the bench builds. For Wi-Fi-only playback, use a board with a flash layout that includes LittleFS.
- **Browser** — Desktop Chrome or another desktop Chromium browser with Web Serial enabled for the four standalone HTML players. The Wi-Fi-only builds use the ESP8266's own HTTP page.

1. In Arduino IDE **Preferences**, add this board manager URL, then install **esp8266 by ESP8266 Community** in Boards Manager. [ESP8266 Arduino core installation guide](https://arduino-esp8266.readthedocs.io/en/3.1.2/installing.html)

   ```text
   https://arduino.esp8266.com/stable/package_esp8266com_index.json
   ```

2. Download the repository or clone it.

   ```sh
   git clone https://github.com/kaboom748/ESP-AnalogRadio.git
   cd ESP-AnalogRadio
   ```

3. Copy **one** `.ino` into a working folder whose name matches the file stem. For example, place `B2_AM_LIVE_SERIAL_HIFI_OV2.ino` in a folder called `B2_AM_LIVE_SERIAL_HIFI_OV2`. Each Wi-Fi-only sketch also needs its own folder.
4. Open the copied sketch. Select your ESP8266 board, the serial port, and **CPU Frequency → 160 MHz**. For a Wi-Fi-only sketch, set the network constants and select a flash layout with a LittleFS partition before uploading.
5. Upload at a rate your board and cable handle reliably. The Arduino upload speed is independent of the player's **2 Mbaud** runtime link. Close the Serial Monitor after uploading a Web Serial sketch so the browser can claim the port.

## Run a Web Serial build

These four builds decode and encode the audio in the browser, stream binary samples through USB serial, and use the ESP8266 for RF timing without joining Wi-Fi. With Python 3 installed, run the following command from the repository root to serve the player in a secure loopback context. Use `python3` or `py -3` in place of `python` if that is how you launch Python on your system. [Chrome Web Serial documentation](https://developer.chrome.com/docs/capabilities/serial)

```sh
python -m http.server 8000 --bind 127.0.0.1
```

| Build | Player URL on the computer running the local server |
| --- | --- |
| AM | `http://localhost:8000/AMSerialHQv1/AM_LIVE_WEBSERIAL_HIFI.html` |
| Mono FM | `http://localhost:8000/FMSerialHQv1/FM_LIVE_WEBSERIAL_WFM16_PROFIL_MESURE.html` |
| Stereo FM | `http://localhost:8000/FMStereoHQv1/FM_LIVE_WEBSERIAL_STEREO.html` |
| SSB | `http://localhost:8000/SSBTestv1/SSB_LIVE_WEBSERIAL_HIFI.html` |

1. Open the matching player. Click **Connecter le port** and select the ESP8266's USB serial adapter. **État / PING** checks the connection. The controls are currently labelled in French.
2. Choose an audio file the browser can decode, such as MP3 or WAV, and trim a short selection. Use the source preview to check it. SSB also offers built-in signals for tuning and bench tests.
3. Choose the mode settings, then click **Préencoder l’extrait**. Pre-encoding converts the selection to the firmware's sample format. In mono FM, **Écouter les échantillons transmis** plays a local preview of the encoded samples.
4. Set up the SDR on the selected mode. Click **Diffuser vers ESP8266**. The firmware runs its pre-transmit CW interval, then transmits the clip. Watch for serial errors, underruns, and missed sample deadlines where the player reports them.
5. To end the clip while keeping CW, use **Stopper l’envoi**. In mono FM, the corresponding control is **Stopper les envois (timeout ESP)**, which stops the host writes and lets the device time out. To stop RF, click **ARRÊT RF** and confirm the carrier is gone on the SDR before disconnecting. Use **Reprendre CW** when ready to restart the carrier.

The firmware uses an **8 KiB sample ring buffer**. At 2 Mbaud with 8N1 framing, the theoretical line payload is 200 kB/s before protocol overhead. The audio rates below are binary payload rates, with kB/s in decimal units. Boot CW uses APWR **127** in AM and both FM builds, and **63** in SSB. All four browser players default to APWR **63** when starting a clip. Keep the player tab active and avoid letting the computer sleep mid-stream.

## AM over Web Serial

[Firmware](AMSerialHQv1/B2_AM_LIVE_SERIAL_HIFI_OV2.ino) · [HTML player](AMSerialHQv1/AM_LIVE_WEBSERIAL_HIFI.html)

The player downmixes the source to mono and sends amplitude samples for a fixed RF carrier. Start at **48 kSa/s**, **60% depth**, and the **14 kHz** audio filter target. Tune the SDR in AM mode to the actual programme carrier. A **1 kHz** source tone makes it easy to check that the sidebands are symmetric and the recovered audio is clean. Match the receiver filter to the source bandwidth and the local RF conditions.

| Specification | Value |
| --- | --- |
| Programme carrier and CW | 2442.312500 MHz (K+4) and 2442.625000 MHz (K+8), nominal |
| CW timing | At boot, between clips, and for 1 second before each clip |
| Envelope sample rates | 32, **48**, or 64 kSa/s |
| Serial sample format | Unsigned little-endian Q8.8 envelope, 2 bytes per sample |
| Serial payload | 64, **96**, or 128 kB/s |
| RF amplitude update | Twice the envelope rate, at 64, **96**, or 128 kHz |
| Audio low-pass targets | 6, 10, **14**, or 18 kHz, capped at 44% of the lower of the source or stream sample rate |
| Modulation depth | 10–90% control, **60%** default |
| Browser peak headroom | 1 dB |
| Browser APWR | **63** by default. Measured-envelope lookup for APWR 0 and 63, with nominal fallback at other settings |

The measured envelope lookup comes from the development bench. Verify modulation depth and distortion on your own module, especially if using a high depth setting. At a 32 kSa/s stream rate, the 18 kHz selection is capped at 14.08 kHz or below.

## Mono FM over Web Serial

[Firmware](FMSerialHQv1/B2_FM_LIVE_SERIAL_48K_WFM_B2_K4_TUNE1450_UART_FLOW8K_2000000.ino) · [HTML player](FMSerialHQv1/FM_LIVE_WEBSERIAL_WFM16_PROFIL_MESURE.html)

This build sends mono instantaneous frequency deviation. Start with the default **±25 kHz nominal deviation**, **60% modulation level**, and **0 µs pre-emphasis**. Select WFM on the SDR, centre on the actual signal and match de-emphasis to the encoder setting. Begin with a receiver passband wide enough for the deviation and audio band, then adjust it while monitoring noise and distortion.

| Specification | Value |
| --- | --- |
| Programme carrier and CW | 2442.312500 MHz (K+4) and 2442.625000 MHz (K+8), nominal |
| CW timing | At boot, between clips, and for 1 second before each clip |
| Stream sample rate | 48 kSa/s mono |
| Serial sample format | WFM2, signed 16-bit little-endian deviation, 4 Hz per count |
| Serial payload | 96 kB/s |
| Nominal peak deviation settings | **±25**, ±50, or ±75 kHz |
| Browser audio bands | 200 Hz–2.8 kHz or 100 Hz–6, 8, 10, 12, or **15 kHz** |
| Browser modulation level | **60%** by default |
| Pre-emphasis | **Off**, 50 µs, or 75 µs |
| Browser APWR and processing | **63**, natural dynamics, automatic normalization and soft limiter off by default |

Nominal deviation sets the encoder's reference scale. The actual excursion also depends on the source level, modulation percentage, and any processing. The local encoded preview helps isolate clipping or aggressive processing before transmission.

## Stereo FM over Web Serial

[Firmware](FMStereoHQv1/B2_FM_LIVE_SERIAL_STEREO_120K_MPX8.ino) · [HTML player](FMStereoHQv1/FM_LIVE_WEBSERIAL_STEREO.html)

The browser builds the FM stereo multiplex (MPX) from the left and right channels. It combines the **L+R** audio baseband, a **19 kHz pilot**, and double-sideband suppressed-carrier **L−R** around **38 kHz**. An FM mono decoder recovers the sum. A stereo MPX decoder uses the pilot and difference channel to reconstruct left and right.

Use a known stereo file and start with **±50 kHz**, **80% programme level**, and **0 µs pre-emphasis**. Select a wide enough receiver passband to preserve the FM signal and its multiplex through 53 kHz. Enable stereo decoding and match de-emphasis to the encoder. Use a left-only tone and a right-only tone to check channel routing and separation.

| Specification | Value |
| --- | --- |
| Programme carrier and CW | 2442.312500 MHz (K+4) and 2442.625000 MHz (K+8), nominal |
| CW timing | At boot, between clips, and for 1 second before each clip |
| MPX sample rate and format | 120 kSa/s, signed 8-bit |
| Serial payload | 120 kB/s |
| Programme audio | Up to 15 kHz |
| Pilot | 19 kHz at 9% of nominal deviation in the encoder |
| Difference channel | 38 kHz DSB-SC, approximately 23–53 kHz in the multiplex |
| Nominal peak deviation settings | ±25, **±50**, or ±75 kHz |
| Browser programme level and APWR | **80%** and **63** |
| Pre-emphasis | **Off**, 50 µs, or 75 µs |
| Difference-channel correction | 1.145× L−R scaling in the ±50 kHz profile |

## USB and LSB over Web Serial

[Firmware](SSBTestv1/B2_SSB_LIVE_SERIAL_HIFI.ino) · [HTML player](SSBTestv1/SSB_LIVE_WEBSERIAL_HIFI.html)

The browser downmixes to mono and converts the filtered audio to a polar representation. It sends an amplitude and an instantaneous frequency increment for each sample. **USB** places the audio above the suppressed-carrier reference, while **LSB** mirrors it below. Both use the same firmware and player.

1. Leave the idle CW running from boot in the contained setup. The player defaults to a **60-second continuous CW warm-up before every clip**, adjustable from **1 to 900 seconds**. The pre-transmit interval is separate from the CW that has already been running at idle.
2. Find the warmed-up CW peak on the SDR. Subtract **78.125 kHz** to estimate the suppressed-carrier reference, then select the same sideband as the player. For a finer adjustment, transmit the built-in **1 kHz** tone and tune until the receiver plays it at 1 kHz. With USB, the reference is approximately **1 kHz below** the RF tone peak. With LSB, it is approximately **1 kHz above**.
3. Use **200 Hz–2.8 kHz** for a speech test. For wider audio start at **32 kSa/s** and **100 Hz–12 kHz**, then widen the SDR's SSB filter to match. A narrow SSB receiver filter will cut the highs from the wide profiles.
4. Start with **6 dB headroom** and the nominal envelope mapping. The optional rare-peak limiter can raise the useful signal on files whose level is set by a few exceptionally large peaks. Compare it on and off with the same receiver tuning. The limiter is off by default.

| Specification | Value |
| --- | --- |
| Suppressed-carrier reference | 2442.312500 MHz (K+4), nominal |
| CW reference | 2442.390625 MHz (K+5), nominal |
| CW timing | Continuous at boot and idle, 1–900 seconds before each clip, **60 seconds** by default |
| Sideband selection | **USB** or LSB |
| Stream sample rate | 24 or **32 kSa/s** |
| Serial sample format | 4 bytes per sample, with unsigned Q8.8 amplitude and signed Q16 frequency increment |
| Serial payload | 96 or **128 kB/s** |
| Audio passbands | 200 Hz–2.8 kHz or 100 Hz–6, **12**, or 14 kHz |
| Browser headroom and APWR | **6 dB** and **63** by default, with headroom adjustable from 0–18 dB |
| Built-in sources | Tone, two-tone, sweep, and silence |

The 12 and 14 kHz profiles require **32 kSa/s**. The encoder also checks the decoded source sample rate. A frequency error shifts every recovered audio tone in SSB, so recheck tuning after a reset or a large temperature change. The published player uses a generic nominal envelope mapping without per-board calibration constants.

## Wi-Fi-only builds

These experimental sketches embed the browser player in the firmware. The ESP8266 receives the encoded clip over Wi-Fi, checks its CRC and stores it in **LittleFS**. During programme transmission it temporarily takes the Wi-Fi station offline and plays from flash. Use the separate Web Serial builds if your test requires continuous idle CW from boot and control over a wired connection.

### Network and playback

1. In the sketch copy, set `WIFI_SSID`, `WIFI_PASSWORD`, `WIFI_IP`, `WIFI_MASK`, `WIFI_GW`, and `WIFI_DNS` for your network. The checked-in static IP is **192.168.1.50**. Choose an unused address in your LAN's subnet. Keep credentials in your local sketch copy.
2. Set the 2.4 GHz router to a fixed channel with a centre frequency **at least 20 MHz from the configured RF centre**. With the default 2442 MHz transmitter base, router channel 1 or 11 satisfies the firmware's separation check. A 4 MB ESP8266 with a LittleFS partition is a practical starting point. The optional serial diagnostics run at **115200 baud**.
3. Flash the sketch and open `http://192.168.1.50/` or the address you assigned. Check the flash status and available clip length. LittleFS playback capacity depends on the partition and the remaining free space. The firmware keeps **128 KiB** free. **Initialiser LittleFS (effacement confirmé)** formats the file-system partition and erases its files when initialization is needed.
4. Set the initial warm-up and click **Démarrer le préchauffage**. It defaults to **300 seconds**, adjustable from **1 to 900 seconds**. The browser loses the connection during the initial CW interval. Wait for the ESP8266 to rejoin and the page to show that playback is ready. In the warmed-up state the firmware alternates CW with periods when Wi-Fi control is available.
5. Import an **MP3/WAV**, trim and preview it, select the mode settings, and use **Encoder l’extrait…**. File import works on the local HTTP page. Live microphone capture depends on browser secure-context rules.
6. Click **Envoyer … en flash + vérifier CRC** and wait for verification. Start playback with the **CW 1 s + …** button for the stored clip. Wait for Wi-Fi to return when playback ends. A receiver can monitor the carrier while the browser is out of contact.
7. Click **Arrêter dès prochaine fenêtre Wi-Fi** to stop when the ESP8266 can receive the command. For an immediate stop while the page is unreachable, remove power.

### Wi-Fi AM

[ESP8266_AM.ino](WIFIOnlyTest/ESP8266_AM.ino)

Use the **100 Hz–6 kHz** preset for a first clip and set the SDR to AM. The sketch transmits a fixed-frequency carrier with a variable envelope. The browser defaults to **60%** modulation depth. Select a wider source band only if the receiver can pass the matching AM sidebands.

| Specification | Value |
| --- | --- |
| Programme carrier and CW | 2442.000000 MHz (K) and 2442.078125 MHz (K+1), nominal |
| Flash clip format and rate | AMF1, 32 kSa/s, 4 bytes per sample, 128 kB/s |
| Audio bands | 200 Hz–2.8 kHz or 100 Hz–**6**, 12, or 14 kHz |
| Modulation depth | 30–75%, **60%** by default |

### Wi-Fi SSB

[ESP8266_SSB.ino](WIFIOnlyTest/ESP8266_SSB.ino)

For the stored clip, use **CW 1 s + Micro USB** or **CW 1 s + Micro LSB**, then match the sideband at the receiver. Start with the **200 Hz–2.8 kHz** voice preset. Find the warm-up CW and subtract **78.125 kHz** to estimate the suppressed-carrier reference. Fine-tune on known audio.

| Specification | Value |
| --- | --- |
| Suppressed-carrier and CW references | 2442.000000 MHz (K) and 2442.078125 MHz (K+1), nominal |
| Flash clip format and rate | SBF1, 32 kSa/s, 4 bytes per sample, 128 kB/s |
| Sidebands | USB and LSB |
| Audio bands | **200 Hz–2.8 kHz** or 100 Hz–6, 12, or 14 kHz |

### Wi-Fi mono FM

[B2_WFM_MONO_HIFI_48K_CONTROLE_MODULATION_APERCU.ino](WIFIOnlyTest/B2_WFM_MONO_HIFI_48K_CONTROLE_MODULATION_APERCU.ino)

Choose **WFM mono** on the SDR. Start at **±50 kHz**, with no de-emphasis when encoder pre-emphasis is off. A receiver bandwidth around **150–200 kHz** is a starting point at that setting. The browser's encoded WFM2 preview reconstructs the audio from the saved deviation values before the RF stage.

| Specification | Value |
| --- | --- |
| Programme carrier and CW | 2442.000000 MHz (K) and 2442.078125 MHz (K+1), nominal |
| Flash clip format and rate | WFM2, 48 kSa/s, signed 16-bit deviation in 4 Hz steps, 96 kB/s |
| Audio bands | 200 Hz–2.8 kHz or 100 Hz–6, 8, 10, 12, or **15 kHz** |
| Nominal peak deviation | ±25, **±50**, or ±75 kHz |
| Modulation level | 20–100%, **60%** by default |
| Pre-emphasis | **Off**, 50 µs, or 75 µs |

## Bench measurements

These are development measurements from **ESP8266 and HackRF** test setups. Receiver gain, filtering, tuning, clock error, audio source, and test method all affect the result. Treat the frequencies reported by the receiver as examples from those captures.

| Build and test | Observed RF frequency | Measured result |
| --- | ---: | --- |
| **Web Serial AM**, 48 kSa/s, APWR 63, calibrated envelope, controlled tones | — | **−21.35 dB THD+N** at 997 Hz by envelope detection. **−1.97 dB** at 18 kHz relative to 997 Hz. **17.97 dB** two-tone signal-to-residual. |
| **Web Serial AM**, 10-second MP3 selection, 48 kSa/s, APWR 63 | 2442.285122 MHz | **1.89 dB** source-aligned envelope audio signal-to-residual and **0.779** source correlation. **960,000 bytes** streamed with zero reported serial errors. |
| **Web Serial mono FM**, direct-serial 1 kHz tone at half fixture amplitude, ±25 kHz profile, 60% level, APWR 63 | ≈2442.2854 MHz | **27.49 dB SNR** excluding measured harmonics, **27.37 dB SINAD**, and **0.73% THD**. Recovered deviation was **7.51 kHz** against **7.50 kHz** expected. At 15 kHz, response was **−0.03 dB** relative to 1 kHz on the same fixture. HackRF captured at 4 MS/s, with a ±55 kHz analysis IF. |
| **Web Serial stereo FM**, separate L/R tones, ±50 kHz, APWR 63 | 2442.306829 MHz | **30.90 dB L→R separation** at 997 Hz and **25.51 dB R→L separation** at 1499 Hz. Pilot **18,999.94 Hz**, **8.40%** of nominal deviation, and **25.41 dB** above the adjacent 1 kHz noise reference. Wanted tone-to-residual **19.20 dB L / 19.12 dB R**. |
| **Web Serial stereo FM**, 10-second music selection | 2442.291523 MHz | **1.20 MB** sent with zero serial errors. Maximum absolute encoded deviation **38.6 kHz**. The recovered pilot was **18,999.84 Hz** and **18.12 dB** above the adjacent 1 kHz noise reference. |
| **Web Serial USB**, 700 Hz and 1900 Hz tones, 32 kSa/s, APWR 63, nominal envelope, 2-second CW interval | — | Estimated in-band noise-floor **SNR 20.18 dB** across 100 Hz–12 kHz. Tone **SINAD 15.00 dB** after phase tracking. Integrated opposite sideband **−24.11 dBc**. |
| **Web Serial LSB**, 30-second browser transport, 32 kSa/s | — | **960,000 samples**, **3,840,000 bytes**, and zero reported transport errors. |
| **Wi-Fi AM, Wi-Fi SSB, Wi-Fi mono FM** | — | Bench SNR and distortion figures are unavailable for these three builds. Their configured specs are listed above. |

### SSB limiter comparison

The same 10-second MP3 selection was captured with the rare-peak limiter off and on. The receiver analysis corrected frequency drift before applying the sideband filter. Wanted RF power rose by about **6 dB** in these two comparisons at the same headroom setting. The effect depends on the source's peak distribution.

| Sideband | Source-aligned signal-to-residual, off → on | Integrated opposite sideband, off → on |
| --- | ---: | ---: |
| USB | 9.72 → **10.16 dB** | −24.33 → **−26.55 dBc** |
| LSB | 9.77 → **10.11 dB** | −24.59 → **−27.71 dBc** |

One startup capture showed roughly **3.94 kHz peak-to-peak drift** over **16.76 seconds** of uninterrupted CW. In another run, the last five seconds of a **60-second** warm-up showed about **198 Hz peak-to-peak drift**. These captures include the receiver reference and illustrate why SSB has an adjustable warm-up and a separate CW tuning marker.

### How to read the numbers

- **SNR** compares signal power to estimated noise power. The FM tone result removes measured harmonics from the noise term. The SSB tone result estimates the noise floor from the median residual spectral density and integrates it over the analysis band, discounting discrete spurs.
- **SINAD** compares signal, noise, and distortion together against noise plus distortion. **THD+N** in the AM row gives the noise and distortion residual relative to the fundamental, in dB.
- **Source-aligned signal-to-residual** includes noise, distortion, and differences from the reference after alignment. Music, two-tone, and noise-floor results use different methods and bandwidths.
- **Stereo separation** compares the wanted channel with leakage into the other channel. **Pilot-to-adjacent-noise** measures the pilot against a nearby noise region. **Opposite sideband in dBc** is referenced to the wanted sideband.

### Repeat a receiver check

1. Record the firmware build, sample rate, APWR, modulation depth or deviation, pre-emphasis, and CW time. Use the same source file, receiver sample rate, gain, and passband between comparisons.
2. Capture the warmed-up CW and the programme as IQ. Keep the receiver below clipping and record the acquired RF frequency. For SSB, use the tone and check drift before computing audio metrics.
3. Use a single tone for deviation, THD+N, or SINAD, and a separate pair of left-only and right-only tones for FM stereo separation. Use an identical source clip and reference-alignment method for audio comparisons.
4. Record transport counters alongside the RF measurements. A clean local preview and a stream without underruns help distinguish source or transport problems from demodulation and RF problems.

## Troubleshooting

| Symptom | Check |
| --- | --- |
| Serial adapter is missing or busy | Close Arduino Serial Monitor and other serial clients. Check the USB data cable and driver. If using a VM or USB redirection, confirm that the browser's machine owns the adapter. |
| The browser reports Web Serial is unavailable | Use a desktop browser with Web Serial and serve the player on `localhost` or HTTPS. Confirm the selected HTML matches the uploaded sketch. |
| Playback stutters or reports underruns | Check the 2 Mbaud link, cable, USB bridge and host workload. Keep the tab active and start with a short pre-encoded clip. For AM, try the default 48 kSa/s. |
| The receiver only shows a narrow peak | Check whether the board is still warming up or has returned to CW. Apply the frequency offset from the project table to find the programme. |
| SSB sounds tinny, nasal, or pitch-shifted | Select the matching sideband and fine-tune using the 1 kHz reference. For wide audio, choose the matching SDR filter width. Let the CW settle after reset and compare with the original preview. |
| Stereo FM comes through as mono | Verify a stereo source, pilot lock, stereo MPX decoding, and sufficient receiver bandwidth. A left-only or right-only tone makes channel leakage easy to hear. |
| FM sounds dull, harsh, or clipped | Match encoder pre-emphasis and receiver de-emphasis. Check the encoded preview, deviation, SDR filter width, and receiver input gain. |
| Wi-Fi playback page stops responding | Initial warm-up and offline playback take the ESP8266 off Wi-Fi. Wait for the next control window. If it never reconnects, check network settings, router channel spacing, and the 115200 baud diagnostics. |
| Wi-Fi clip upload is too large | Check reported LittleFS capacity, use a shorter trim and verify the CRC again. |
| CW remains visible after disconnect | Reconnect and use **ARRÊT RF**, or remove power. The four Web Serial sketches start CW again at boot. |

## Gallery and credits

<details>
<summary>Original bench screenshots</summary>

![ESP-AnalogRadio bench screenshot 1](https://github.com/user-attachments/assets/f1d2ebef-0edf-4123-a1dd-574c613a6238)

![ESP-AnalogRadio bench screenshot 2](https://github.com/user-attachments/assets/ccd4b288-7853-4c76-a9a0-d599a4caa745)

![ESP-AnalogRadio bench screenshot 3](https://github.com/user-attachments/assets/a2b465e2-7f78-475f-94e8-6807bf7e265f)

</details>

- **Project** — Guillaume / [kaboom748](https://github.com/kaboom748).
- **RF techniques and reference code** — SP8ESA's MIT-licensed [ESP8266 2.4 GHz SSB transceiver](https://github.com/SP8ESA/ESP8266_2.4GHz_SSB_TRX).
- **Wi-Fi RF handoff** — The PM1/PM0 approach in the Wi-Fi-only sketches builds on [ESPBoOm8266](https://github.com/kaboom748/ESPBoOm8266).
- **License** — [GNU General Public License v3](LICENSE). See the notices in the individual sketches for upstream attribution.

