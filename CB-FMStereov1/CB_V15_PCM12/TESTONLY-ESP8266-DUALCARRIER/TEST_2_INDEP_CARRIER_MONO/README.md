WARNING !!! ±50 kHz only... don't listen to ±75 kHz with this version.

***Lower RF TX Power on ESP32 and don't use V2 (ESP32 TX will be OK on RX from ESP8266)

# V15 dual-mono FM for ESP8266

Use the [V15 universal HTML](../CB_FM_STEREO-Universal.html) over **1.5 Mbaud, 8N1**. Press PTT with a stereo audio source. The browser sends PCM12 at **36 kSa/s per channel**, and the ESP8266 routes the channels to two independent mono FM carriers.

| Carrier | Audio | Nominal centre | Nominal peak deviation |
| --- | --- | --- | --- |
| 1 | Right | Selected frequency | ±100 kHz |
| 2 | Left | Selected frequency + 1.5625 MHz | ±100 kHz |

The spacing is 20 native NCO steps. Each channel has its own pulse timing and phase-error feedback. TX uses one pass of 2× interpolation, for **72 kSa/s per carrier**. This gives Timer1 roughly 2,222 CPU cycles per sample pair and leaves a continuous interval after both pulses for UART and audio work. The original stereo PCM frame structure and flow control remain compatible with the HTML. The RDS trailer is read as part of the frame and discarded. Both transmitted signals are audio-only mono.

The ±100 kHz scale is referenced to the peak PCM12 level used by the unchanged ESP8266 browser path. The two carriers share the original V15 PLL and RF gain setting. The available TX range for the first centre is **2,401–2,480.937 MHz**. In RX, the board uses the original MPX3 profiles over the full V15 tuning range.

## Build and upload

Open [CB_FM_STEREO_MPXRX_PCM12TX_ESP8266.ino](CB_FM_STEREO_MPXRX_PCM12TX_ESP8266.ino) with the **ESP8266 Arduino 3.1.2** core. For the reference **LOLIN/Wemos D1 mini**, select **160 MHz**, **4 MB DIO flash at 40 MHz**, **1 MB filesystem**, and **48 KiB IRAM / 16 KiB cache**. Run this command from the sketch folder:

```sh
arduino-cli compile --fqbn "esp8266:esp8266:d1_mini:xtal=160,eesz=4M1M,mmu=4816" .
```

Use Arduino IDE or Arduino CLI to upload. If flashing a separately built sketch image directly, its offset on this ESP8266 layout is `0x0`.

On an SDR, use a mono wideband FM demodulator on each carrier. A different test tone in each source channel makes routing easy to verify. Timer skips, audio underruns and FIFO errors are reported on the serial port when TX ends. RF timing and audio performance for this variant need bench verification.
