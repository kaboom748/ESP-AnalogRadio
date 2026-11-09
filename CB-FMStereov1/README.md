# C.B. FM Stereo

C.B. FM Stereo is a two-station, half-duplex radio for ESP8266EX boards. Each station uses a browser for its microphone, speaker, and controls, and an ESP8266 for the 2.4 GHz FM radio link. Press and hold to transmit. Release to listen. Both stations use the same firmware and standalone HTML file.

**For voice, select MONO on both stations.** It decodes the main FM audio directly, needs no stereo pilot lock, and uses the least serial bandwidth. Use **ROBUST** when you need stereo with less serial traffic, and **HQ** when you want the widest stereo audio bandwidth.

## Project files

| File | Purpose |
| --- | --- |
| [CB_FM_STEREO.ino](CB_FM_STEREO.ino) | Arduino firmware for both ESP8266 boards. Handles RF tuning, FM transmission and reception, and serial mode switching. |
| [CB_FM_STEREO.html](CB_FM_STEREO.html) | Standalone English control panel. Includes both views, the audio processing code, and the faceplate artwork. |

The USB serial link is local to each station. The two stations exchange audio over RF and can operate without an internet connection after setup. The computer provides the microphone and speaker through the browser.

## What you need

- Two ESP8266EX boards. The reference build uses LOLIN/WEMOS D1 mini boards with 4 MB flash and a CH340 USB-to-serial bridge.
- A USB data cable and a USB serial connection capable of **2,000,000 baud** for each board.
- Desktop Chrome with Web Serial and Web Audio support. Desktop Edge is also an option when those APIs are available. Chrome on Windows was used for the local-file browser checks. [Chrome Web Serial documentation](https://developer.chrome.com/docs/capabilities/serial)
- A microphone and speakers or a headset available to the operating system running each station's browser.
- Arduino IDE or Arduino CLI and the **ESP8266 Arduino core 3.1.2** for building and flashing.

## 1. Flash both radios

1. Keep `CB_FM_STEREO.ino` in a folder named `CB_FM_STEREO`. Open it in Arduino IDE.
2. In Arduino IDE preferences, add the URL below to **Additional Boards Manager URLs**. In **Boards Manager**, install version **3.1.2** of the ESP8266 platform. [ESP8266 Arduino core installation guide](https://arduino-esp8266.readthedocs.io/en/3.1.2/installing.html)

   ```text
   https://arduino.esp8266.com/stable/package_esp8266com_index.json
   ```

3. Select **LOLIN(WEMOS) D1 R2 & mini** for the reference board. Set **CPU Frequency** to **160 MHz** and select the **4 MB flash / 1 MB filesystem** configuration (`4M1M`). Use **115200** for the reference upload speed. The browser uses a separate runtime baud rate.
4. Select the board's USB port and upload. Repeat with the second board. Close the serial monitor, uploader, or any other software using a port before opening it in the browser.

With core 3.1.2 installed, the equivalent Arduino CLI build and upload commands are run from the `CB_FM_STEREO` folder. Replace `COM27` with the actual port for each board, such as `/dev/ttyUSB0` on Linux.

```sh
arduino-cli compile --fqbn "esp8266:esp8266:d1_mini:xtal=160,eesz=4M1M" .
arduino-cli upload --port COM27 --fqbn "esp8266:esp8266:d1_mini:xtal=160,eesz=4M1M,baud=115200,wipe=none" .
```

## 2. Connect, tune, and make the first call

1. Open `CB_FM_STEREO.html` in desktop Chrome. Use one page instance per physical radio. When a board is passed through to a VM, open the page where the board's USB serial port is visible. Two instances on the same computer must select different ports.
2. In **Easy**, click **PORT**, select the board in the browser's chooser, and wait for the radio to enter listening mode. In **Current interface**, use **Connect serial port**.
3. Open **AUDIO**, select the **Microphone** and **Speaker**, then click **Prepare microphone** and grant microphone access. If per-device output selection is unavailable, choose the default speaker and set the desired output in your operating system.
4. Set the same channel or nominal frequency on both radios. Start with **CH 07** and leave **RX FINE** at **0 Hz**. The **CHANNEL** control opens the preset and manual tuning options. For manual entry, type the frequency in MHz, for example `2442.3125`, then select **Tune**.
5. Select **MONO** at both stations for the voice check. In **Easy**, click **RX MODE** to cycle through the profiles. In **Current interface**, use the **Receive profile** menu. The page initially selects **HQ**.
6. Set **MIC GAIN** to **0 dB** and **VOL** to a comfortable level. Let the receivers listen briefly while both stations are quiet so the noise estimate can settle. If needed for the first test, set **SQ** to **0** to hear what the receiver is decoding.
7. **Hold the spacebar to talk (PTT).** You can also hold the on-screen hand microphone or the **PUSH TO TALK** button. Wait for **TX** to light red, then speak. Release to return to listening and let the other operator reply. Confirm that each station can hear the other in turn.

Spacebar PTT works while the page or PTT control has focus. When another control has focus, Spacebar can operate that control instead. Press **Esc** to cancel a held transmission. Changing interface tabs, hiding the page, or leaving the browser window also releases held PTT and DTMF.

## 3. Choose a receive profile

| Profile | When to use it | Decoder | ESP I/Q sample rate | Serial MPX sample rate | Approx. RX audio cutoff | Serial RX traffic |
| --- | --- | --- | ---: | ---: | ---: | ---: |
| **MONO** | Everyday voice, weak or noisy speech, and first tests | Main audio `L+R`, played through both speaker channels | 192 kSa/s | 48 kSa/s | 15.00 kHz | 51,750 B/s |
| **ROBUST** | Stereo when you want a narrower audio passband and less serial traffic than HQ | Pilot-based stereo with a reduced audio passband | 192 kSa/s | 96 kSa/s | 8.30 kHz | 103,500 B/s |
| **HQ** | Music, stereo separation, and the widest stereo audio on a clean link | Pilot-based stereo with the widest stereo audio passband | 240 kSa/s | 120 kSa/s | 15.15 kHz | 129,375 B/s |

**MONO** uses the part of the FM multiplex shared by the left and right channels. Voice stays centered, and decoding continues without the 19 kHz pilot. Its serial traffic is 40% of HQ's, including framing. The receive audio cutoff is higher than in ROBUST. The lower serial load comes from carrying only the multiplex bandwidth needed for mono decoding.

**ROBUST** preserves left and right separation when the pilot is locked and trades high-frequency stereo detail for a lower stream rate. **HQ** preserves more of that detail. Both stereo decoders blend toward mono when pilot lock is lost. In **Current interface**, `Mono · waiting for stereo pilot` means that mono audio can still play while stereo is being acquired.

The profile is local to the receiver. You can choose **MONO** at one station and **HQ** at the other on the same channel. Transmission always uses FM stereo multiplex, even with **MONO** selected locally. The browser decoder produces 48 kHz PCM before resampling for the audio device. Serial traffic in the table includes the RX frame header.

## 4. Use the controls and indicators

**Easy** is the default CB-style faceplate. **Current interface** presents the same radio session as a form with explicit frequency, profile, signal, and audio controls. Settings entered in either view apply to the shared session.

| Easy control | What it does |
| --- | --- |
| **PORT** | Connects or disconnects the local ESP8266. |
| **AUDIO** | Selects the microphone and speaker and prepares microphone capture. |
| **CHANNEL** | Selects a preset or enters a manual frequency. Use the wheel or focused arrow keys to cycle presets. |
| **RX MODE** | Cycles **HQ**, **ROBUST**, and **MONO**. |
| **RX FINE** | Applies a local receive offset from **−50,000 to +50,000 Hz**. The wheel and focused arrow keys step by 500 Hz. Start at zero and make small adjustments while the other station transmits if reception sounds distorted. |
| **VOL** | Sets speaker playback and local DTMF feedback from **0 to 100%**, with **100%** as the default. |
| **SQ** | Sets the squelch threshold from **0 to 42 dB** above the estimated noise floor. The default is **12 dB**. **0** holds the audio gate open while RX data is arriving. |
| **MIC GAIN** | Adjusts transmitted voice and DTMF from **−24 to +18 dB**, with **0 dB** as the default. |
| **Hand microphone** | Hold to transmit voice. Release to listen. |
| **DTMF** | Opens the keypad for local feedback and on-air tones. |

To set squelch, listen with **SQ** low and no one transmitting. Raise it until idle noise mutes. Then have the other station speak and lower **SQ** if words are being cut off. Adjust **VOL** for listening comfort and **MIC GAIN** for the level you send to the other station.

### Status lights

At most one light is active at a time.

| Light | Meaning |
| --- | --- |
| **Green RX** | The receiver has decoded audio and squelch is open. Speech and incoming DTMF both count. |
| **Yellow SQ** | The radio is listening and squelch is muting the speaker path. |
| **Red TX** | The station is transmitting voice or DTMF. The light can stay on briefly while the radio switches back to listening. |
| **All dark** | The radio is disconnected, transitioning, or listening with squelch open and no detected audio. |

The **S-meter** shows signal level relative to the estimated noise floor, in dB. It can move with squelch closed. The separate **L/R audio meters** show digital audio level in dBFS. In **TX** they show voice or DTMF after **MIC GAIN**. In **RX** they show decoded audio after squelch and before **VOL**, so lowering speaker volume leaves the meter reading intact. A closed squelch clears the RX bars. Mono audio appears equally on both meters. If the red peak segments light repeatedly while speaking, lower **MIC GAIN** and check again.

### DTMF keypad

Open **DTMF** in **Easy**. Tap a digit for a short tone, or hold it to sustain the tone. You can also focus a key and hold **Enter** or **Spacebar**. A connected station keys the transmitter automatically from receive mode. During voice PTT, a DTMF key temporarily mutes the mic path. Releasing the key restores voice if PTT is still held.

**VOL** sets the feedback you hear from your selected speaker. **MIC GAIN** sets the DTMF level sent over RF. The local tone can play with the radio disconnected. On the other station, the tone is handled as ordinary received audio and lights green **RX** when it passes squelch.

| Low tone | 1209 Hz | 1336 Hz | 1477 Hz |
| ---: | :---: | :---: | :---: |
| 697 Hz | 1 | 2 | 3 |
| 770 Hz | 4 | 5 | 6 |
| 852 Hz | 7 | 8 | 9 |
| 941 Hz | `*` | 0 | `#` |

Each key combines one row and one column frequency. [ITU-T Q.23](https://www.itu.int/rec/T-REC-Q.23/en)

## Technical specifications

| Item | Value |
| --- | --- |
| Operating mode | Half-duplex analog FM over 2.4 GHz, with push-to-talk turn-taking |
| ESP CPU clock | 160 MHz |
| Nominal tuning range | 2,401.0000 to 2,482.5000 MHz |
| Presets | **CH 01** 2,412.3125 MHz, **CH 07** 2,442.3125 MHz (default), **CH 11** 2,462.3125 MHz |
| Receive fine tuning | ±50 kHz, applied only to the local receiver |
| FM deviation | ±50 kHz nominal at full scale |
| FM stereo multiplex | `L+R` main audio, 19 kHz pilot, and `L−R` on a suppressed 38 kHz subcarrier |
| TX serial audio | 120,000 signed 8-bit MPX samples/s, 120,000 B/s |
| USB serial settings | 2,000,000 baud, 8 data bits, no parity, 1 stop bit, no flow control |
| Runtime protocol | `CB_FM_STEREO 3` |

### Serial and PTT sequence

1. The browser sends `STATUS` and expects `OK STATUS CB_FM_STEREO 3`. It configures receive with `RX <frequency_hz> <profile> <fine_hz>`. The ESP reports `OK RX <actual_lo_hz> <mpx_samples_per_second> <profile>` and streams receive samples.
2. On PTT, the browser sends the byte `0xA5`. The ESP stops the receive stream and sends a burst of four `0xEF` ready bytes, repeated about every 25 ms until TX audio starts. If audio never arrives, the armed state times out after about 1.2 seconds.
3. Once ready, the browser streams raw MPX bytes continuously, with no per-block acknowledgement or CRC. The firmware uses a 16 KiB TX ring and starts RF after at least 4,096 samples are buffered. The browser sends audio in batches of up to 2,400 bytes and drops old queued samples if its send queue reaches the 240 ms limit.
4. On release, the browser stops sending audio. After 150 ms of UART audio inactivity the firmware ends TX, then retunes and resumes RX. The actual switch takes longer than the idle timer because the radio also needs time to retune.

In receive, each 138-byte `MPX3` frame consists of the four-byte ASCII signature `MPX3`, a little-endian 32-bit sample index, a little-endian 16-bit relative signal magnitude, and 128 signed 8-bit MPX samples. `0x80` marks an invalid RX sample. The sample index lets the browser detect stream gaps and resume decoding at the next frame. RX frames also carry no CRC.

## Troubleshooting

| Symptom | What to check |
| --- | --- |
| The serial port is missing or busy | Use a USB data cable and check that the host or VM sees the board. Close the Arduino Serial Monitor and any other browser session using the port. Check the port again after reconnecting USB. |
| The page reports an incompatible firmware or `ERR CPU160` | Upload this project's firmware to the selected port. Use the ESP8266 core and 160 MHz settings from the flashing section. The page requires protocol version 3. |
| The page reports a worklet, microphone, or browser permission error | Open the actual HTML file in desktop Chrome. Check site permissions and try the localhost method below if local-file permissions interfere. |
| PTT lights red but the other station hears silence | Verify the selected microphone and check that the TX meters move with your voice. Confirm the same nominal frequency on both stations. On the listener, select MONO, set RX FINE to 0, and temporarily set SQ to 0. |
| The receiver stays yellow or cuts off words | Lower SQ and watch the S-meter while the other station talks. If the signal rises but sounds distorted with SQ open, adjust RX FINE in small steps. |
| RX lights green but the speaker is quiet | Check VOL, the selected speaker, and the operating system's audio output. The RX indicator and meters are measured before playback volume. |
| Audio drops out during a long PTT or reception | Check for a serial error or a rising **Missing frames** count in **Current interface**. Try MONO to reduce receive traffic, then test the USB connection and Chrome tab as described below. |
| DTMF sounds locally but the other station hears nothing | Check that the local radio is connected and that TX turns red while the key is held. Check MIC GAIN, matching frequency, and the other station's SQ. |

### Improve noisy stereo or choppy audio

Change one thing at a time and repeat the same short call. This makes it easier to tell which change helped.

1. **Try another radio channel if FM stereo sounds noisy or distorted.** Nearby Wi-Fi and other 2.4 GHz devices can interfere with one frequency more than another. If you are on **CH 07**, switch **both** radios to **CH 01** or **CH 11** and keep the same stereo profile for the comparison. Listen for cleaner audio and check whether the stereo pilot holds lock in **Current interface**. For a voice call, use **MONO** if stereo remains unstable. [Cisco guide to 2.4 GHz channel interference](https://www.cisco.com/c/en/us/support/docs/smb/wireless/CB-Wireless-Mesh/2069-tz-Changing-RF-Channels.html)
2. **Move the ESP8266 to a different physical USB port if the serial stream stutters.** Start with a port directly on the computer. Devices on the same hub or dock share its upstream USB connection, so a busy webcam or external drive can contribute to uneven delivery. If possible, try a port on a different USB controller, or temporarily unplug a busy device from the shared hub. After moving the board, reconnect with **PORT** and select the current serial device. Its port name may have changed. The app still uses **2,000,000 baud**. [Microsoft USB bandwidth documentation](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/usb-bandwidth-allocation)
3. **Keep the radio tab open and visible in Chrome while testing and operating.** Make it the selected tab and leave the browser window visible. Chrome can throttle some background work, so keeping the radio page in the foreground can help audio and controls run more smoothly. Keep the transmitting page focused for the whole PTT hold, since this app releases PTT if the window loses focus or the page is hidden. [Chrome background tab scheduling](https://developer.chrome.com/blog/background_tabs/)

### Run from localhost

For localhost operation, run the following command from the project folder with Python 3. Open the resulting page in desktop Chrome. [Open C.B. FM Stereo on localhost](http://localhost:8000/CB_FM_STEREO.html)

```sh
python -m http.server 8000 --bind 127.0.0.1
```

## Verification and credits

The version 3 firmware was tested on one physical ESP8266 with all three RX modes, repeated PTT cycles, and a continuous 20-second TX serial stream followed by a return to RX. The standalone HTML was checked in Chrome on Windows with Web Audio, simulated serial traffic, and test audio for PTT, DTMF, the status lights, and both audio meters. The full two-radio RF path and operating range for this version still need bench verification.

The combined firmware is based on the original **FMStereoHQv3** transmitter and **FMStereoRX** receiver. It retains the SP8ESA MIT notice for the reused RF PHY tuning and I/Q estimator code. [Original transmitter](https://github.com/kaboom748/ESP-AnalogRadio/tree/37b16fb64fb4f4694bb27646d1b097afc5da6d68/FMStereoHQv3) · [SP8ESA ESP8266 radio project](https://github.com/SP8ESA/ESP8266_2.4GHz_SSB_TRX)
