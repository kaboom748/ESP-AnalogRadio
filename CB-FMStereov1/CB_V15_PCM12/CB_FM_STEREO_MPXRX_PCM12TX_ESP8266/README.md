WARNING !!!  ±50 kHz only... don't listen to ±75 kHz with this version.
***Lower RF TX Power on ESP32 and don't use V2 (ESP32 will be OK on ESP8266)

# V15 PCM12 TX and MPX RX for ESP8266

Use this hybrid firmware with the [V15 universal HTML](../CB_FM_STEREO-Universal.html) over **1.5 Mbaud, 8N1**. The TX stream is **12-bit stereo PCM at 36 kSa/s per channel**. The ESP8266 builds the **120 kSa/s** multiplex. RX returns **MPX3** to the browser at 120, 96, or 48 kSa/s in HQ, ROBUST, or MONO. The RF range exposed to the page is **2,401–2,482.5 MHz**.

## Build and upload

Open [CB_FM_STEREO_MPXRX_PCM12TX_ESP8266.ino](CB_FM_STEREO_MPXRX_PCM12TX_ESP8266.ino) with the **ESP8266 Arduino 3.1.2** core. For the reference **LOLIN/Wemos D1 mini**, select **160 MHz**, **4 MB DIO flash at 40 MHz**, **1 MB filesystem**, and **48 KiB IRAM / 16 KiB cache**. Run this command from the sketch folder:

```sh
arduino-cli compile --fqbn "esp8266:esp8266:d1_mini:xtal=160,eesz=4M1M,mmu=4816" .
```

Use Arduino IDE or Arduino CLI to upload. If flashing a separately built sketch image directly, its offset on this ESP8266 layout is `0x0`.

## What was measured

The **78.125 kHz** native RF step is averaged through timed corrections. The final 16-bit internal multiplex path recorded about **70 Hz RMS** of residual FM on two 2.4 GHz channels. A 24-second HTML TX test and a 30-second high-level stereo bench run returned to reception without underruns. Cross-tests with the C5 and C6 verified two-way 2.4 GHz audio. Some sustained synthetic stress patterns still underfed TX.

The hybrid uses the same browser and TX frames as the other V15 builds, while leaving receive stereo and RDS decoding in the browser. For test conditions and board comparisons see the [V15 RF measurements](../README.md#radio-bench-results).
