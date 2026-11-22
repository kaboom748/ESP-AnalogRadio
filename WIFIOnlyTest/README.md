# WIFIOnlyTest — upload over Wi-Fi, transmit from flash

Three ESP8266 sketches serve their own browser player. The ESP receives an encoded audio clip over the local network, verifies its CRC, and stores it in LittleFS. During RF playback, it takes the Wi-Fi station offline temporarily and reads the clip from flash.

| Sketch | Mode | Stored format |
| --- | --- | --- |
| [ESP8266_AM.ino](ESP8266_AM.ino) | Mono AM | AMF1, 32 kSa/s, 4 bytes per sample |
| [ESP8266_SSB.ino](ESP8266_SSB.ino) | USB or LSB | SBF1, 32 kSa/s, 4 bytes per sample |
| [Mono WFM sketch](B2_WFM_MONO_HIFI_48K_CONTROLE_MODULATION_APERCU.ino) | Wideband FM mono | WFM2, 48 kSa/s, signed 16-bit deviation at 4 Hz per count |

The nominal RF programme reference is **2442.000000 MHz** and warm-up CW is **2442.078125 MHz**. The 115,200-baud serial port is available for diagnostics. The browser interface is embedded in each sketch.

## Set up and play a clip

1. Copy one sketch into its own Arduino folder named after the file stem. Use Arduino ESP8266 core 3.1.2, a 160 MHz CPU, and a flash layout with a LittleFS partition.
2. Set `WIFI_SSID`, `WIFI_PASSWORD`, `WIFI_IP`, `WIFI_MASK`, `WIFI_GW` and `WIFI_DNS` in the local copy. Choose a free address for your LAN. The checked-in IP is `192.168.1.50`. Keep the router's 2.4 GHz centre at least 20 MHz from the configured transmitter centre. With the default RF centre, router CH 1 or CH 11 passes that check.
3. Upload and open the ESP's HTTP page. Check LittleFS capacity before importing a clip. The **Initialiser LittleFS** control erases files on the filesystem partition. Start the initial warm-up and wait for the Wi-Fi control page to be reachable again.
4. Select and encode an MP3 or WAV clip, upload it to flash, and wait for CRC verification. Start playback from the page. Use the matching AM, USB/LSB or mono WFM mode on a 2.4 GHz-capable SDR.
5. The browser temporarily loses access during playback. The stop command can be received in the next Wi-Fi control window. For an immediate stop while the page is unreachable, remove power.

Use the [wired browser projects](../README.md#choose-a-project) when you need streaming with a local USB serial controller.
