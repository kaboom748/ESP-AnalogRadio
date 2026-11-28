# ESP-AnalogRadio — Windows Build Guide

**Reference archive** · `0cabfa13ae883506af4aad99d9d34442b978aca3` · Windows 10/11 on 64-bit Intel or AMD · 51 Arduino sketches and 1 ESPHome variant.

**To compile the 51 sketches in Arduino IDE, select two packages in Boards Manager.** Install **esp8266 3.1.2** for the D1 mini and **esp32 3.3.12** for all three ESP32 models. The esp32 package includes the ESP-IDF 5.5.5 libraries for the Classic, C5 and C6. The D1 mini uses the NONOS SDK supplied with its own package. Chapter 1 covers installation and lists the GUI settings for every project.

## 1. Download the packages, install them and select the settings in Arduino IDE

### Versions and direct download links

| Item | Version and purpose | Official download |
|---|---|---|
| Arduino IDE for Windows x64 | **2.3.11**, the IDE selected for this guide. It includes Arduino CLI **1.6.0**. | [Windows x64 ZIP](https://github.com/arduino/arduino-ide/releases/download/2.3.11/arduino-ide_2.3.11_Windows_64bit.zip) |
| **esp8266** board package | **3.1.2**, for ESP8266. Bundled libraries include `ESP8266WiFi`, `ESP8266WebServer` and `LittleFS`. | [Boards Manager index](https://arduino.esp8266.com/stable/package_esp8266com_index.json) · [Core archive](https://github.com/esp8266/Arduino/releases/download/3.1.2/esp8266-3.1.2.zip) |
| **esp32** board package | **3.3.12**, based on **ESP-IDF 5.5.5**. A single installation covers the ESP32 Classic, ESP32-C5 and ESP32-C6. | [Boards Manager index](https://espressif.github.io/arduino-esp32/package_esp32_index.json) · [Core archive](https://github.com/espressif/arduino-esp32/releases/download/3.3.12/esp32-core-3.3.12.zip) |
| Precompiled Espressif libraries | The three library archives used by the ESP32 projects in this repository. Boards Manager places them in the correct directories. | [Classic](https://github.com/espressif/arduino-esp32/releases/download/3.3.12/esp32-libs-3.3.12.zip) · [C5](https://github.com/espressif/arduino-esp32/releases/download/3.3.12/esp32c5-libs-3.3.12.zip) · [C6](https://github.com/espressif/arduino-esp32/releases/download/3.3.12/esp32c6-libs-3.3.12.zip) |

The two core archives alone do not contain all the Windows compilers. Their indexes also list tools, chip-specific libraries and checksums. The official esp32 3.3.12 installation requires the full dependency set declared by the package, including some archives for other ESP32 variants.

The downloader uses Windows PowerShell 5.1 and the `curl.exe` and `tar.exe` utilities included in modern Windows 10/11. Allow about **2.33 GB for the archives** of the IDE and Windows packages, plus the indexes. Extracting the packages later takes several additional gigabytes. A successful download writes `SHA256SUMS.txt` in the download directory.

### First download and offline installation

1. Place `ESP-AnalogRadio.bat` in a writable directory, for example `C:\Radio`, then double-click it **with an internet connection**. It downloads the Windows archives and indexes into `Arduino-telechargements` beside the batch file. Keep this folder and your repository ZIP. The download can be done on a different PC from the one that will build the projects.
2. On the build PC, **close Arduino IDE**. Extract the IDE ZIP to a short path. To prepare an offline installation on Windows, open `%LOCALAPPDATA%` in File Explorer and copy the `Arduino15` folder from `Arduino-telechargements` into it. The resulting path must be `%LOCALAPPDATA%\Arduino15\staging\packages`. If an installation already exists, back up your current `Arduino15` folder before merging the contents. Keep the package archives compressed in `staging\packages`.
3. Start **Arduino IDE**. Under **File → Preferences → Additional boards manager URLs**, add each of the following lines. The IDE also uses these names to identify the local index files.

```text
https://arduino.esp8266.com/stable/package_esp8266com_index.json
https://espressif.github.io/arduino-esp32/package_esp32_index.json
```

4. Open **Boards Manager** in the sidebar. Search for `esp8266` and install **esp8266 by ESP8266 Community → 3.1.2**. Then search for `esp32` and install **esp32 by Espressif Systems → 3.3.12**. Select the version before clicking **Install**. Boards Manager extracts the cached archives into `%LOCALAPPDATA%\Arduino15\packages`. An automatic index refresh can display a network error on a disconnected PC.
5. If the GUI does not show the versions on its first offline start, use the engine included in the IDE ZIP. From a PowerShell terminal **opened at the root of the extracted IDE**, with the cache already in place and the IDE closed, run the commands below. They install the packages from the cache on the build PC. Then reopen the IDE.

```powershell
$cli = (Get-ChildItem -Path . -Filter arduino-cli.exe -File -Recurse |
    Select-Object -First 1).FullName
$urls = 'https://arduino.esp8266.com/stable/package_esp8266com_index.json,https://espressif.github.io/arduino-esp32/package_esp32_index.json'
& $cli core install 'esp8266:esp8266@3.1.2' --additional-urls $urls
if ($LASTEXITCODE -ne 0) { throw 'esp8266:esp8266@3.1.2' }
& $cli core install 'esp32:esp32@3.3.12' --additional-urls $urls
if ($LASTEXITCODE -ne 0) { throw 'esp32:esp32@3.3.12' }
```

6. Extract the repository. For each target, place **one primary sketch** in a folder matching the sketch name without `.ino`. Copy the source and header files from **that** variant alongside it, including adjacent `.cpp` files, `build_opt.h` and the `src/` tree when present. Create three separate folders for the sketches in `WIFIOnlyTest`. Open the sketch, choose its board under **Tools → Board**, apply its table row, then click **Verify** (✓). You can compile with no board connected.
7. For long-term storage, copy the download folder, repository, guides and, after a successful offline build, the **installed Arduino15 directory** to a second medium. An image of a Windows x64 virtual machine that has reproduced the builds also preserves the host environment.

### GUI settings for every project

Each row identifies **one exact repository path**. The values correspond to the profiles used to compile all 51 sketches and were checked against the `boards.txt` files in packages 3.1.2 and 3.3.12. The option values are the English labels used by the board packages. In the menu, `CPU Frequency` is the processor clock and `Flash Frequency` is the flash clock. `Upload Speed` affects uploading and does not affect **Verify**.

- **ESP8266** · Under `MMU`, select **16KB cache + 48KB IRAM (IRAM)**. Under `Flash Size`, select **4MB (FS:1MB OTA:~1019KB)**. The D1 mini board definition fixes DIO and 40 MHz. The USB CDC and PSRAM menus do not apply.
- **ESP32 Classic, C5 and C6** · These board definitions do not expose an MMU menu for choosing the amount of IRAM. The MMU column therefore shows a dash. On Classic and C5, set `PSRAM` to `Disabled`. On C5 and C6, set `USB CDC On Boot` to `Disabled`. On Classic, keep `Arduino Runs On` and `Events Run On` set to `Core 1`. For every board, leave the remaining options at the defaults for the specified package version.
- **Three C5 projects with 8 MB flash** · Projects 02, 04 and 48 were documented on an 8 MB board. The standard 8 MB partition scheme below is the one used for the reference builds. Check the physical flash capacity before uploading. The other nine C5 projects use the 4 MB profile.
- **Target 52** · The YAML file is a separate ESPHome target. Its row gives the active YAML settings. Chapter 6 covers its separate build system.

| No. | Repository project / sketch | Tools → Board | CPU Frequency | MMU / IRAM | Flash Size · Partition Scheme | Flash Mode · Flash Frequency |
|---:|---|---|---|---|---|---|
| 01 | `AM-RX-YEAHHHHHv1/AMReceiverHQ.ino` | `LOLIN(WEMOS) D1 R2 & mini` | `160 MHz` | `16KB cache + 48KB IRAM (IRAM)` | `4MB (FS:1MB OTA:~1019KB)` | `DIO` · `40 MHz` (fixed) |
| 02 | `AM-RX-YEAHHHHHv1/ESP32-C5v1/AMReceiverHQ_C5/AMReceiverHQ_C5.ino` | `ESP32C5 Dev Module` | `240MHz (WiFi)` | — | `8MB (64Mb)` · `8M with spiffs (3MB APP/1.5MB SPIFFS)` | `DIO` · `80MHz` |
| 03 | `AMSerialHQv1/B2_AM_LIVE_SERIAL_HIFI_OV2.ino` | `LOLIN(WEMOS) D1 R2 & mini` | `160 MHz` | `16KB cache + 48KB IRAM (IRAM)` | `4MB (FS:1MB OTA:~1019KB)` | `DIO` · `40 MHz` (fixed) |
| 04 | `AMSerialHQv1/ESP32-C5v1/AMSerialHQv1_C5/AMSerialHQv1_C5.ino` | `ESP32C5 Dev Module` | `240MHz (WiFi)` | — | `8MB (64Mb)` · `8M with spiffs (3MB APP/1.5MB SPIFFS)` | `DIO` · `80MHz` |
| 05 | `CB-FMStereov1/CB_FM_STEREOv5.ino` | `LOLIN(WEMOS) D1 R2 & mini` | `160 MHz` | `16KB cache + 48KB IRAM (IRAM)` | `4MB (FS:1MB OTA:~1019KB)` | `DIO` · `40 MHz` (fixed) |
| 06 | `CB-FMStereov1/CB_V10/CB_FM_STEREOv10_Arduino/CB_FM_STEREOv10_Arduino.ino` | `LOLIN(WEMOS) D1 R2 & mini` | `160 MHz` | `16KB cache + 48KB IRAM (IRAM)` | `4MB (FS:1MB OTA:~1019KB)` | `DIO` · `40 MHz` (fixed) |
| 07 | `CB-FMStereov1/CB_V12/CB_FM_STEREOv12_Arduino/CB_FM_STEREOv12_Arduino.ino` | `LOLIN(WEMOS) D1 R2 & mini` | `160 MHz` | `16KB cache + 48KB IRAM (IRAM)` | `4MB (FS:1MB OTA:~1019KB)` | `DIO` · `40 MHz` (fixed) |
| 08 | `CB-FMStereov1/CB_V13/CB_FM_STEREOv13_Arduino/CB_FM_STEREOv13_Arduino.ino` | `LOLIN(WEMOS) D1 R2 & mini` | `160 MHz` | `16KB cache + 48KB IRAM (IRAM)` | `4MB (FS:1MB OTA:~1019KB)` | `DIO` · `40 MHz` (fixed) |
| 09 | `CB-FMStereov1/CB_V13/CB_FM_STEREOv13_C5/CB_FM_STEREOv13_C5.ino` | `ESP32C5 Dev Module` | `240MHz (WiFi)` | — | `4MB (32Mb)` · `Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)` | `DIO` · `80MHz` |
| 10 | `CB-FMStereov1/CB_V13/CB_FM_STEREOv13_C6/CB_FM_STEREOv13_C6.ino` | `ESP32C6 Dev Module` | `160MHz (WiFi)` | — | `4MB (32Mb)` · `Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)` | `QIO` · `80MHz` |
| 11 | `CB-FMStereov1/CB_V13/CB_FM_STEREOv13_ESP32/CB_FM_STEREOv13_ESP32.ino` | `ESP32 Dev Module` | `240MHz (WiFi/BT)` | — | `4MB (32Mb)` · `Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)` | `DIO` · `40MHz` |
| 12 | `CB-FMStereov1/CB_V13_5GHZ_C5_BETA/CB_FM_STEREOv16_C5/CB_FM_STEREOv16_C5.ino` | `ESP32C5 Dev Module` | `240MHz (WiFi)` | — | `4MB (32Mb)` · `Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)` | `DIO` · `80MHz` |
| 13 | `CB-FMStereov1/CB_V14/CB_FM_STEREOv13_C5/CB_FM_STEREOv13_C5.ino` | `ESP32C5 Dev Module` | `240MHz (WiFi)` | — | `4MB (32Mb)` · `Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)` | `DIO` · `80MHz` |
| 14 | `CB-FMStereov1/CB_V14/CB_FM_STEREOv13_C6/CB_FM_STEREOv13_C6.ino` | `ESP32C6 Dev Module` | `160MHz (WiFi)` | — | `4MB (32Mb)` · `Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)` | `QIO` · `80MHz` |
| 15 | `CB-FMStereov1/CB_V14/CB_FM_STEREOv13_ESP32/CB_FM_STEREOv13_ESP32.ino` | `ESP32 Dev Module` | `240MHz (WiFi/BT)` | — | `4MB (32Mb)` · `Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)` | `DIO` · `40MHz` |
| 16 | `CB-FMStereov1/CB_V14/CB_FM_STEREOv13_ESP8266/CB_FM_STEREOv13_ESP8266.ino` | `LOLIN(WEMOS) D1 R2 & mini` | `160 MHz` | `16KB cache + 48KB IRAM (IRAM)` | `4MB (FS:1MB OTA:~1019KB)` | `DIO` · `40 MHz` (fixed) |
| 17 | `CB-FMStereov1/CB_V15_PCM12/CB_FM_STEREO_MPXRX_PCM12TX_ESP8266/CB_FM_STEREO_MPXRX_PCM12TX_ESP8266.ino` | `LOLIN(WEMOS) D1 R2 & mini` | `160 MHz` | `16KB cache + 48KB IRAM (IRAM)` | `4MB (FS:1MB OTA:~1019KB)` | `DIO` · `40 MHz` (fixed) |
| 18 | `CB-FMStereov1/CB_V15_PCM12/CB_FM_STEREO_MPXRX_PCM12TX_ESP8266v2/CB_FM_STEREO_MPXRX_PCM12TX_ESP8266.ino` | `LOLIN(WEMOS) D1 R2 & mini` | `160 MHz` | `16KB cache + 48KB IRAM (IRAM)` | `4MB (FS:1MB OTA:~1019KB)` | `DIO` · `40 MHz` (fixed) |
| 19 | `CB-FMStereov1/CB_V15_PCM12/PCM12_C6/CB_FM_STEREO_PCM12_C6.ino` | `ESP32C6 Dev Module` | `160MHz (WiFi)` | — | `4MB (32Mb)` · `Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)` | `QIO` · `80MHz` |
| 20 | `CB-FMStereov1/CB_V15_PCM12/PCM12_C6v2/CB_FM_STEREO_PCM12_C6.ino` | `ESP32C6 Dev Module` | `160MHz (WiFi)` | — | `4MB (32Mb)` · `Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)` | `QIO` · `80MHz` |
| 21 | `CB-FMStereov1/CB_V15_PCM12/PCM12_ESP32/CB_FM_STEREO_PCM12_ESP32.ino` | `ESP32 Dev Module` | `240MHz (WiFi/BT)` | — | `4MB (32Mb)` · `Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)` | `DIO` · `40MHz` |
| 22 | `CB-FMStereov1/CB_V15_PCM12/PCM12_ESP32C5/CB_FM_STEREO_PCM12_C5.ino` | `ESP32C5 Dev Module` | `240MHz (WiFi)` | — | `4MB (32Mb)` · `Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)` | `DIO` · `80MHz` |
| 23 | `CB-FMStereov1/CB_V15_PCM12/PCM12_ESP32C5v2/CB_FM_STEREO_PCM12_C5.ino` | `ESP32C5 Dev Module` | `240MHz (WiFi)` | — | `4MB (32Mb)` · `Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)` | `DIO` · `80MHz` |
| 24 | `CB-FMStereov1/CB_V15_PCM12/PCM12_ESP32v2/CB_FM_STEREO_PCM12_ESP32.ino` | `ESP32 Dev Module` | `240MHz (WiFi/BT)` | — | `4MB (32Mb)` · `Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)` | `DIO` · `40MHz` |
| 25 | `CB-FMStereov1/CB_V15_PCM12/TESTONLY-C5-DUALCARRIER/CB_FM_STEREO_PCM12_C5.ino` | `ESP32C5 Dev Module` | `240MHz (WiFi)` | — | `4MB (32Mb)` · `Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)` | `DIO` · `80MHz` |
| 26 | `CB-FMStereov1/CB_V15_PCM12/TESTONLY-C5-DUALCARRIER/TEST_2_INDEP_CARRIER_MONO/CB_FM_STEREO_PCM12_C5.ino` | `ESP32C5 Dev Module` | `240MHz (WiFi)` | — | `4MB (32Mb)` · `Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)` | `DIO` · `80MHz` |
| 27 | `CB-FMStereov1/CB_V15_PCM12/TESTONLY-C6-DUALCARRIER/CB_FM_STEREO_PCM12_C6.ino` | `ESP32C6 Dev Module` | `160MHz (WiFi)` | — | `4MB (32Mb)` · `Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)` | `QIO` · `80MHz` |
| 28 | `CB-FMStereov1/CB_V15_PCM12/TESTONLY-ESP32-DUALCARRIER/CB_FM_STEREO_PCM12_ESP32.ino` | `ESP32 Dev Module` | `240MHz (WiFi/BT)` | — | `4MB (32Mb)` · `Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)` | `DIO` · `40MHz` |
| 29 | `CB-FMStereov1/CB_V15_PCM12/TESTONLY-ESP8266-DUALCARRIER/CB_FM_STEREO_MPXRX_PCM12TX_ESP8266.ino` | `LOLIN(WEMOS) D1 R2 & mini` | `160 MHz` | `16KB cache + 48KB IRAM (IRAM)` | `4MB (FS:1MB OTA:~1019KB)` | `DIO` · `40 MHz` (fixed) |
| 30 | `CB-FMStereov1/CB_V15_PCM12/TESTONLY-ESP8266-DUALCARRIER/TEST_2_INDEP_CARRIER_MONO/CB_FM_STEREO_MPXRX_PCM12TX_ESP8266.ino` | `LOLIN(WEMOS) D1 R2 & mini` | `160 MHz` | `16KB cache + 48KB IRAM (IRAM)` | `4MB (FS:1MB OTA:~1019KB)` | `DIO` · `40 MHz` (fixed) |
| 31 | `CB_DuplexVoice_V3/CB_V4/CB_DuplexVoice_V4_Arduino/CB_DuplexVoice_V4_Arduino.ino` | `LOLIN(WEMOS) D1 R2 & mini` | `160 MHz` | `16KB cache + 48KB IRAM (IRAM)` | `4MB (FS:1MB OTA:~1019KB)` | `DIO` · `40 MHz` (fixed) |
| 32 | `CB_DuplexVoice_V3/CB_V5/CB_DuplexVoice_V4_Arduino/CB_DuplexVoice_V4_Arduino.ino` | `LOLIN(WEMOS) D1 R2 & mini` | `160 MHz` | `16KB cache + 48KB IRAM (IRAM)` | `4MB (FS:1MB OTA:~1019KB)` | `DIO` · `40 MHz` (fixed) |
| 33 | `CB_DuplexVoice_V3/CB_V6/CB_DuplexVoice_V6_Arduino/CB_DuplexVoice_V6_Arduino.ino` | `LOLIN(WEMOS) D1 R2 & mini` | `160 MHz` | `16KB cache + 48KB IRAM (IRAM)` | `4MB (FS:1MB OTA:~1019KB)` | `DIO` · `40 MHz` (fixed) |
| 34 | `CB_DuplexVoice_V3/CB_V6/CB_DuplexVoice_V6_C5/CB_DuplexVoice_V6_C5.ino` | `ESP32C5 Dev Module` | `240MHz (WiFi)` | — | `4MB (32Mb)` · `Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)` | `DIO` · `80MHz` |
| 35 | `CB_DuplexVoice_V3/CB_V7beta/CB_DuplexVoice_V6_C5/CB_DuplexVoice_V6_C5.ino` | `ESP32C5 Dev Module` | `240MHz (WiFi)` | — | `4MB (32Mb)` · `Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)` | `DIO` · `80MHz` |
| 36 | `CB_DuplexVoice_V3/CB_V7beta/CB_DuplexVoice_V6_C6/CB_DuplexVoice_V6_C6.ino` | `ESP32C6 Dev Module` | `160MHz (WiFi)` | — | `4MB (32Mb)` · `Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)` | `QIO` · `80MHz` |
| 37 | `CB_DuplexVoice_V3/CB_V7beta/CB_DuplexVoice_V6_ESP32/CB_DuplexVoice_V6_ESP32.ino` | `ESP32 Dev Module` | `240MHz (WiFi/BT)` | — | `4MB (32Mb)` · `Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)` | `DIO` · `40MHz` |
| 38 | `CB_DuplexVoice_V3/CB_V7beta/CB_DuplexVoice_V6_ESP8266/CB_DuplexVoice_V6_ESP8266.ino` | `LOLIN(WEMOS) D1 R2 & mini` | `160 MHz` | `16KB cache + 48KB IRAM (IRAM)` | `4MB (FS:1MB OTA:~1019KB)` | `DIO` · `40 MHz` (fixed) |
| 39 | `FMSerialHQv1/B2_FM_LIVE_SERIAL_48K_WFM_B2_K4_TUNE1450_UART_FLOW8K_2000000.ino` | `LOLIN(WEMOS) D1 R2 & mini` | `160 MHz` | `16KB cache + 48KB IRAM (IRAM)` | `4MB (FS:1MB OTA:~1019KB)` | `DIO` · `40 MHz` (fixed) |
| 40 | `FMStereoHQv1/B2_FM_LIVE_SERIAL_STEREO_120K_MPX8.ino` | `LOLIN(WEMOS) D1 R2 & mini` | `160 MHz` | `16KB cache + 48KB IRAM (IRAM)` | `4MB (FS:1MB OTA:~1019KB)` | `DIO` · `40 MHz` (fixed) |
| 41 | `FMStereoHQv2/B2_FM_LIVE_SERIAL_STEREO_120K_MPX8.ino` | `LOLIN(WEMOS) D1 R2 & mini` | `160 MHz` | `16KB cache + 48KB IRAM (IRAM)` | `4MB (FS:1MB OTA:~1019KB)` | `DIO` · `40 MHz` (fixed) |
| 42 | `FMStereoHQv3/B2_FM_LIVE_SERIAL_STEREO_120K_MPX8.ino` | `LOLIN(WEMOS) D1 R2 & mini` | `160 MHz` | `16KB cache + 48KB IRAM (IRAM)` | `4MB (FS:1MB OTA:~1019KB)` | `DIO` · `40 MHz` (fixed) |
| 43 | `FMStereoRX-YEAHHHHHv1/FMStereoRX.ino` | `LOLIN(WEMOS) D1 R2 & mini` | `160 MHz` | `16KB cache + 48KB IRAM (IRAM)` | `4MB (FS:1MB OTA:~1019KB)` | `DIO` · `40 MHz` (fixed) |
| 44 | `MODEM-SimplexV1/FMFileModemRX/FMFileModemRX.ino` | `LOLIN(WEMOS) D1 R2 & mini` | `160 MHz` | `16KB cache + 48KB IRAM (IRAM)` | `4MB (FS:1MB OTA:~1019KB)` | `DIO` · `40 MHz` (fixed) |
| 45 | `MODEM-SimplexV1/FMFileModemTX/FMFileModemTX.ino` | `LOLIN(WEMOS) D1 R2 & mini` | `160 MHz` | `16KB cache + 48KB IRAM (IRAM)` | `4MB (FS:1MB OTA:~1019KB)` | `DIO` · `40 MHz` (fixed) |
| 46 | `SSB-RX-YEAHHHHHv1/SSBReceiverHQ.ino` | `LOLIN(WEMOS) D1 R2 & mini` | `160 MHz` | `16KB cache + 48KB IRAM (IRAM)` | `4MB (FS:1MB OTA:~1019KB)` | `DIO` · `40 MHz` (fixed) |
| 47 | `SSBTestv1/B2_SSB_LIVE_SERIAL_HIFI.ino` | `LOLIN(WEMOS) D1 R2 & mini` | `160 MHz` | `16KB cache + 48KB IRAM (IRAM)` | `4MB (FS:1MB OTA:~1019KB)` | `DIO` · `40 MHz` (fixed) |
| 48 | `SSBTestv1/ESP32-C5v1/B2_SSB_LIVE_SERIAL_HIFI_C5/B2_SSB_LIVE_SERIAL_HIFI_C5.ino` | `ESP32C5 Dev Module` | `240MHz (WiFi)` | — | `8MB (64Mb)` · `8M with spiffs (3MB APP/1.5MB SPIFFS)` | `DIO` · `80MHz` |
| 49 | `WIFIOnlyTest/B2_WFM_MONO_HIFI_48K_CONTROLE_MODULATION_APERCU.ino` | `LOLIN(WEMOS) D1 R2 & mini` | `160 MHz` | `16KB cache + 48KB IRAM (IRAM)` | `4MB (FS:1MB OTA:~1019KB)` | `DIO` · `40 MHz` (fixed) |
| 50 | `WIFIOnlyTest/ESP8266_AM.ino` | `LOLIN(WEMOS) D1 R2 & mini` | `160 MHz` | `16KB cache + 48KB IRAM (IRAM)` | `4MB (FS:1MB OTA:~1019KB)` | `DIO` · `40 MHz` (fixed) |
| 51 | `WIFIOnlyTest/ESP8266_SSB.ino` | `LOLIN(WEMOS) D1 R2 & mini` | `160 MHz` | `16KB cache + 48KB IRAM (IRAM)` | `4MB (FS:1MB OTA:~1019KB)` | `DIO` · `40 MHz` (fixed) |
| 52 | `CB-FMStereov1/CB_FM_STEREO.yaml` | ESPHome · `d1_mini` | `160000000L` | — | YAML / PlatformIO configuration | `dout` (YAML) |

**Folders to prepare in this archive.** These 26 entries ship in folders whose names differ from the main sketch. Apply step 6 to targets **01, 03, 05, 18–30, 39–43, 46, 47 and 49–51**. When a project includes `build_opt.h`, its local build options are already written in that file. On Classic, targets 21, 24 and 28 use it to set `E32_TX_MPX_MIN_CYCLES`. Keep the file alongside the sketch.

**Reference results.** All 51 Arduino sketches in the ZIP compiled and produced a binary and a linker map on Linux with the profiles in the table. All 29 Windows core and tool archives were checked and all 38 manifest entries verified. The complete cache was about 2.40 GB. A second downloader run with downloads blocked produced the same manifest. These checks ran under PowerShell on Linux using the Windows files. One additional build per chip family used Arduino CLI 1.6.0, the version bundled with the selected IDE. The ESPHome YAML was built separately. A second, offline build produced the same 261,040 bytes. These checks cover the firmware builds. First use of the Windows GUI still needs to be verified on the destination PC.

Source ZIP SHA-256 · `347a654890dd90950524cc14dda18bf0b8666f1805e454c4cf16246315d3f9fc`. Reference ESPHome binary SHA-256 · `eeafc4e4b871fc6f4f5b0c4ecc39cfd865ef6863724b80d3d46032dfa132d503`.

## 2. The four layers the code depends on

| Layer | Example in this repository | Records to keep |
|---|---|---|
| Silicon and mask ROM | On the ESP8266, function pointers are initialized with literal addresses. On the Classic, the code path actually taken also depends on the silicon revision. | Module and chip part numbers, revision/ECO, crystal, flash size and type, dated errata, memory map, and ROM symbol files for the target. An address is meaningful in that specific context. |
| Hardware logic and memory-mapped I/O (MMIO) registers | UART FIFOs, counters, I/Q acquisition logic, clock selection, and interrupt registers. | Register names and masks from the source, access types and widths, assembly code, memory barriers, access order, SoC headers, and the technical reference manual for the same silicon. Reading a register at its mapped address accesses a peripheral. |
| SDK binaries and runtime state in RAM | `libphy.a`, `phy_param`, `g_phyFuns`, imported functions, calibration tables, and initialization patches. | The complete `.a` archive for each target, its SHA-256 hash, matching headers and `.ld` scripts, link order, and the `.map` file from the working firmware. An `extern` declaration lets the compiler check a call. The linker then needs to find its definition. |
| Rebuildable code and tools | Sketch, `.c/.cpp/.S` files, Arduino Core, ESPHome generator, Python scripts, assembler, compiler, linker, and the tool that produces the `.bin`. | Exact sources, package archives and installed copies, each executable’s version, generator options, full command line, and host environment. |

**If only the ROM survives.** An archived firmware binary lets you inspect what the linker produced. With its ELF and MAP files you can also identify which symbols came from libraries and which had absolute definitions. None of those files can, by itself, recover a reusable library, the startup scripts, or the compiler. If every legitimate copy of an essential routine outside ROM has disappeared, the surviving material is insufficient to establish an equivalent build. You need to recover the original archive or port and qualify a replacement implementation. An SDK update can change the ABI even when the function name and public prototype stay the same.

## 3. Versions, boards, and options to preserve

| Target | Reference build profile and FQBN options | Timing or hardware dependency |
|---|---|---|
| ESP8266 / D1 mini | `esp8266:esp8266@3.1.2` · `esp8266:esp8266:d1_mini:xtal=160,eesz=4M1M,mmu=4816` | Xtensa LX106 at 160 MHz. A 4 MB flash layout with 1 MB for the filesystem. MMU setting of 48 KiB of IRAM and 16 KiB of cache. The `xtal=160` menu option sets the CPU frequency here. Record the physical crystal separately. |
| ESP32 Classic | `esp32:esp32@3.3.12` · `esp32:esp32:esp32:CPUFreq=240,FlashMode=dio,FlashFreq=40` | Two Xtensa LX6 cores. The Classic reference runs at 240 MHz with 40 MHz DIO flash. Several variants require dual-core FreeRTOS, interrupt-level IPC, and the Xtensa tick. |
| ESP32-C5 | Same Arduino-ESP32 core. `esp32:esp32:esp32c5:CPUFreq=240,FlashMode=dio,FlashFreq=80,FlashSize=4M,PartitionScheme=default,PSRAM=disabled,CDCOnBoot=default` | The timed loops use a 240 MHz RISC-V core. The C.B. reference uses 80 MHz DIO flash, 4 MB, default partitions, disabled PSRAM and UART0. For targets 02, 04 and 48, select `FlashSize=8M,PartitionScheme=default_8MB`. Their READMEs document an 8 MB board. The verified builds used this standard partition table. |
| ESP32-C6 | Same Arduino-ESP32 core. `esp32:esp32:esp32c6` | 160 MHz RISC-V core. The six C6 builds used the defaults from package 3.3.12, including QIO and 80 MHz. To reproduce them in the IDE, use the corresponding row in chapter 1. |
| YAML on ESP8266 | ESPHome `2026.10.1`, PlatformIO Espressif8266 platform `4.2.1`, Arduino framework `3.1.2`, PlatformIO source `3.30102.0` | The **active keys** set `board: d1_mini`, `board_flash_mode: dout`, `board_build.f_cpu: 160000000L`, and NONOS `22x_190703`. An older comment mentions `esp01_1m`. The generator reads the active keys. |

Arduino-ESP32 **3.3.12** is built on ESP-IDF **5.5.5**. Some source files mention an IDF **6.2** tree the author used to compare a library. The toolchain described here builds with Arduino package 3.3.12. For the ESP8266, keep the NONOS SDK supplied with core 3.1.2, including its default selection 2.2.1+100 (190703), startup files, and libraries. The `ESP8266WiFi`, `LittleFS`, and `ESP8266WebServer` dependencies come from this package. The ESP32 SDK contains its own FreeRTOS components, HAL, drivers, and chip-specific libraries.

The *board and options* together specify how to build the reference image. A different flash capacity or board revision calls for a separate compatibility check. Flash and CPU clock frequencies are separate settings. A native USB serial pin may behave differently from UART0, which feeds these FIFOs directly.

## 4. Nonstandard dependencies by chip family

### ESP8266 — literal pointers, NONOS SDK, and the IRAM budget

- Open `hardware_esp8266.h` in the ESP8266 PCM12 directory. The source associates the `SetTxClkFn` and `SetAnaScaleFn` types with the literal values `0x4000650C` and `0x4000678C`. The firmware depends on a specific calling convention at those addresses. The linker can produce an image even if the convention is wrong for another silicon revision. Preserve the prototypes, values, and reference revision.
- `user_interface.h` exposes the NONOS APIs. Some projects declare `set_rf_freq_offset` or `chip6_phy_init_ctrl` separately. A symbol’s declaration in a header, its presence in an archive, its address at link time, and its value at runtime are four separate facts. Keep the header **and** the file identified as the provider in the linker map.
- The loops read UART registers and cycle counters directly. The Xtensa assembly instructions (`rsr` and `memw`, depending on the file), the use of `volatile`, the interrupt macros, and the placement of functions in IRAM are architecture-dependent. In the sources that use Timer1, the divider, CPU frequency, and ISR execution time together determine the audio timing.
- Check the instruction sections and the constants the ISRs read. A call from an IRAM routine to a helper in flash can compile successfully and then fail when the cache is unavailable. Keep the MMU option and check the build log for `.iram*` section overflows. For the large Wi-Fi sketches, preserve the flash allocation between the application and LittleFS, along with the data embedded in flash.
- The standalone receiver tests use different transport conventions from the transmitters. The SSB receiver produces I/Q pairs, for example, while the AM receiver produces mono PCM. A board that connects to the browser but produces noise may simply be paired with the wrong interface.

### ESP32 Classic — Xtensa ABI, SDK patches, and two cores

- `src/e32_radio.c` and `src/arduino_phy_compat.c` use `rom_*` and `ram_*` symbols and the `g_phyFuns` table. A source comment describes a dispatch table patched by the SDK, including on ECO1. Preserve the table initialization, its ABI, and the binary that supplies it. Its contents in RAM can differ from the entries in a ROM table.
- `soc/dport_access.h`, DPORT accesses, the UART HAL, and TIMERG structures are specific to this target. In `e32_radio.c`, a frequency offset is expressed in units of **1/1024 MHz**. The C6 code elsewhere uses whole kilohertz. A replacement can compile with the same integer type and still behave differently if it interprets the value in different units.
- The Classic PCM12 path uses `e32_pcm_worker.cpp`, `e32_dram_cursor.h`, and `e32_tick.S`. Shared cursors are aligned 32-bit words in internal DRAM, with explicit memory ordering around Xtensa accesses. The assembly tick routine resides in `.iram1`. Its contract specifies a **CALLX0** call, the argument in `a2`, and a restricted set of usable registers. Preserve the `.S` extension, preprocessing, global symbols, and calling convention.
- In code that checks these settings, `CONFIG_FREERTOS_UNICORE` must be disabled and `CONFIG_ESP_IPC_ISR_ENABLE` enabled. The tick must use CCOUNT and the expected timer index. `e32_adc_bench.c` also references linker-defined bounds. The bench capture path is conditional on `CB13_BENCH_IQ`. It reserves DIRAM and checks its IRAM aliases and the heap bounds. Keep the scripts, flags, and linker map. A library that adds a lock or a helper call in the critical path can change timing without causing a compilation error.
- Read **the variant’s own `build_opt.h`**. The original Classic PCM12 requires `-O2 -DE32_TX_MPX_MIN_CYCLES=56`. Classic PCM12 v2 and the dual-carrier test require `-O2 -DE32_TX_MPX_MIN_CYCLES=80`. Associate local settings with the full project path.

### ESP32-C5 — the C5-specific PHY library and differences between projects

- The C.B. projects that use `src/arduino_phy_compat.c` check the target and IDF version. The conditional code path depends on `CONFIG_ESP_PHY_ENABLE_CERT_TEST`. A comment records that the standard Arduino package omits `librftest.a`. The repository already provides the adaptation for the primitives exported by **this version of** `libphy.a`. Archive the adapter together with its matching library.
- These sources use private data such as `phy_param`, functions with the `phy_` prefix, and calibration conventions. An identical prototype does not guarantee the same table layout, units, or initialization effects. `c5_radio.c`, `c5_tx_radio.cpp`, and, in the relevant variants, `c5_bands.h` encode assumptions that differ from the C6 implementation.
- The PCM12 compile-time guards require the expected CPU frequency and exclude dynamic power management in time-critical paths. The runtime depends on the watchdog HAL, FreeRTOS, timer group configuration, and UART0 routing. The C5 C.B. reference hardware uses GPIO11/TX and GPIO12/RX. Preserve the port configuration, interrupt assumptions, and local buffers.
- The three C5 projects for AM reception, AM transmission, and SSB transmission have separate `.cpp/.h` files **alongside the sketch**. They use `driver/gptimer.h` and `hal/uart_ll.h`. The C.B. projects use a different layout with `src/` and `build_opt.h`. Include `rx_radio.cpp` for the receiver. For each transmitter, keep the version of `radio.cpp` from its own directory.

### ESP32-C6 — shared RISC-V tools and a different radio ABI

- The code selects `CONFIG_IDF_TARGET_ESP32C6`, and the PCM12 path requires 160 MHz. The C5 and C6 use the same RISC-V compiler. The SoC headers, ROM linker script, MMIO addresses, libraries, and some prototypes are specific to the C6.
- `src/c6_radio.c` contains, among others, `chip_v7_set_chan`, `phy_set_freq`, `ram_stop_tx_tone`, `rom_chip_i2c_readReg`, and `rom_chip_i2c_writeReg`. In `arduino_phy_compat.c`, several function names and contracts differ from their C5 counterparts. For a function with the `rom_` prefix, use the ELF and linker map to identify the actual provider and revision.
- `arduino_runtime.cpp` checks the USB configuration and effective clock frequency. The C.B. reference hardware uses UART0 on GPIO16/TX and GPIO17/RX through a serial bridge. Acquisition, interrupt handling, and FIFO access use the C6 hardware. A browser page using the wrong baud rate, or a logging console on the same UART, can lead to a misleading diagnosis.

### Verified hashes of the recovered library files

The hashes were recalculated from the officially distributed board packages installed for testing. All three ESP32 values match those recorded by the authors in the adapter source comments. Each hash identifies the contents of the **extracted** static archive. The repository’s historical comparison cites `esp-phy-lib` at commit `e294ff039e26b3486d6c9e5853d24d98ee3300b2`. Compare the file extracted from your own kit before accepting it as a verified reference copy.

| File extracted from the board package | Verified SHA-256 |
|---|---|
| ESP8266 · `tools/sdk/lib/NONOSDK22x_190703/libphy.a` | `6320879d112258c817c0e9825b9c19a15b8f4711448feb89b4d8fd29e48780e5` |
| ESP8266 · `tools/sdk/ld/eagle.rom.addr.v6.ld` | `0ec3902b6b35e553f5bbc6daff297e11afaa7227436200b60b438cba1dccbba7` |
| ESP32 Classic · `esp32-libs/3.3.12/ld/libphy.a` | `c2521071695734afa8f6f1ca3d3b604a1d08dd63fe52060449a11cfe8bac5740` |
| ESP32-C5 · `esp32c5-libs/3.3.12/ld/libphy.a` | `dbf33c418c8d408d4005c849d12a1432deea82e2e5e57de3c8ddf914d104fffb` |
| ESP32-C6 · `esp32c6-libs/3.3.12/ld/libphy.a` | `fdba95c59c03560620c399f43a72a16230a8ab6fced172f3103ac6a29c36f43a` |

```powershell
Get-ChildItem "$env:LOCALAPPDATA\Arduino15\packages" -Recurse -File -Filter libphy.a |
    Get-FileHash -Algorithm SHA256 | Format-List Path, Hash
```

## 5. Static catalog of all 52 build targets

The numbers match the GUI settings table in chapter 1. Each entry links an exact variant path to its behavior, build inputs or hardware dependencies.

| No. | Target and repository entry point | Details to preserve |
|---:|---|---|
| 01 | **ESP8266** · `AM-RX-YEAHHHHHv1/AMReceiverHQ.ino` | Standalone AM receiver. It estimates I/Q, detects the envelope and outputs AM01 PCM16 at 48 ksample/s over a 1.5 Mbaud 8N2 UART. Preserve the receive gain and the convention for marking missing samples. |
| 02 | **C5** · `AM-RX-YEAHHHHHv1/ESP32-C5v1/AMReceiverHQ_C5/AMReceiverHQ_C5.ino` | C5 port of the AM receiver. The rx_radio.cpp backend sets up I/Q acquisition using GPTimer and low-level UART access. AM01 at 2 Mbaud 8N1. The reference test board has 8 MB of flash. |
| 03 | **ESP8266** · `AMSerialHQv1/B2_AM_LIVE_SERIAL_HIFI_OV2.ino` | ESP8266 serial AM transmitter. Uses amplitude-control samples, a local table and a stream with flow control based on buffer fill level. Keep the 2 Mbaud AM encoder page. |
| 04 | **C5** · `AMSerialHQv1/ESP32-C5v1/AMSerialHQv1_C5/AMSerialHQv1_C5.ino` | C5 AM transmitter. The adjacent radio.cpp file uses GPTimer and the UART HAL. Amplitude conversion depends on the selected band. Keep the C5 page and the 8 MB profile. |
| 05 | **ESP8266** · `CB-FMStereov1/CB_FM_STEREOv5.ino` | Legacy monolithic C.B. stereo v5 firmware. It uses protocol 4 at 2 Mbaud. The YAML embeds the earlier protocol 3 firmware. |
| 06 | **ESP8266** · `CB-FMStereov1/CB_V10/CB_FM_STEREOv10_Arduino/CB_FM_STEREOv10_Arduino.ino` | C.B. V10. The sketch includes the .h modules in its root directory. Keep the V10 browser page and its RadioText decoding for MPX and RDS. 2 Mbaud, protocol 4. |
| 07 | **ESP8266** · `CB-FMStereov1/CB_V12/CB_FM_STEREOv12_Arduino/CB_FM_STEREOv12_Arduino.ino` | C.B. V12. Builds on the V10 architecture and supports 50/75 µs pre-emphasis and de-emphasis in the associated signal chain. 2 Mbaud, protocol 4. |
| 08 | **ESP8266** · `CB-FMStereov1/CB_V13/CB_FM_STEREOv13_Arduino/CB_FM_STEREOv13_Arduino.ino` | C.B. V13 for ESP8266. The serial transport changes to 1.5 Mbaud 8N1. MPX and protocol 4. Keep this directory’s .h files and the V13 browser interface. |
| 09 | **C5** · `CB-FMStereov1/CB_V13/CB_FM_STEREOv13_C5/CB_FM_STEREOv13_C5.ino` | C5 port of V13. Uses the V13 frontend and supports only the 2.4 GHz band in this release. The C5 runtime, PHY adapter and src/ tree are specific to this branch. |
| 10 | **C6** · `CB-FMStereov1/CB_V13/CB_FM_STEREOv13_C6/CB_FM_STEREOv13_C6.ino` | C6 port of V13. Protocol 4, hardware I/Q and a 160 MHz clock. Both UART0 and libphy are specific to the C6. |
| 11 | **ESP32** · `CB-FMStereov1/CB_V13/CB_FM_STEREOv13_ESP32/CB_FM_STEREOv13_ESP32.ino` | Classic port of V13. Keep the e32_* files, the target-specific adapter and the Classic runtime. Uses MPX for both transmit and receive. |
| 12 | **C5** · `CB-FMStereov1/CB_V13_5GHZ_C5_BETA/CB_FM_STEREOv16_C5/CB_FM_STEREOv16_C5.ino` | C5 5 GHz beta branch, with a sketch named v16. Protocol 7 at 2 Mbaud, with a dedicated browser page. Keep its band-selection code, limits and diagnostics together. |
| 13 | **C5** · `CB-FMStereov1/CB_V14/CB_FM_STEREOv13_C5/CB_FM_STEREOv13_C5.ino` | C5 V14 with a sketch named v13. Uses MPX and protocol 4 at 1.5 Mbaud. This branch also supports 5 GHz. The sketch of the same name in the V13 directory operates at 2.4 GHz. |
| 14 | **C6** · `CB-FMStereov1/CB_V14/CB_FM_STEREOv13_C6/CB_FM_STEREOv13_C6.ino` | C6 V14 with the v13 name. MPX in both directions. Keep the complete set of build inputs from this directory for comparisons between V13 and V14. |
| 15 | **ESP32** · `CB-FMStereov1/CB_V14/CB_FM_STEREOv13_ESP32/CB_FM_STEREOv13_ESP32.ino` | Classic V14 with the v13 name. Uses the Classic acquisition path and runtime. Keep the V14 page and the local versions of the e32_* modules. |
| 16 | **ESP8266** · `CB-FMStereov1/CB_V14/CB_FM_STEREOv13_ESP8266/CB_FM_STEREOv13_ESP8266.ino` | ESP8266 V14 with the v13 name. V14 MPX target, protocol 4. The parent directory determines which source and header files the build uses. |
| 17 | **ESP8266** · `CB-FMStereov1/CB_V15_PCM12/CB_FM_STEREO_MPXRX_PCM12TX_ESP8266/CB_FM_STEREO_MPXRX_PCM12TX_ESP8266.ino` | V15 hybrid PCM12 firmware for ESP8266. The browser sends stereo PCM12 and receives MPX3. The microcontroller generates the multiplex. Check IRAM usage and Timer1. |
| 18 | **ESP8266** · `CB-FMStereov1/CB_V15_PCM12/CB_FM_STEREO_MPXRX_PCM12TX_ESP8266v2/CB_FM_STEREO_MPXRX_PCM12TX_ESP8266.ino` | ESP8266 v2 variant. Two modulation outputs carry the same program. The ESP8266 documentation retains a reference deviation of ±50 kHz. The sketch has the same name as in the first release. |
| 19 | **C6** · `CB-FMStereov1/CB_V15_PCM12/PCM12_C6/CB_FM_STEREO_PCM12_C6.ino` | C6 V15 PCM12. Returns PCM12 and RDS and generates the multiplex on-chip. Separate modules handle sampling, transmission and PCM decoding. |
| 20 | **C6** · `CB-FMStereov1/CB_V15_PCM12/PCM12_C6v2/CB_FM_STEREO_PCM12_C6.ino` | C6 V15 v2. Uses two overlapping carriers for the stereo program and a different target deviation. Copy the c6_tx* files from v2. |
| 21 | **ESP32** · `CB-FMStereov1/CB_V15_PCM12/PCM12_ESP32/CB_FM_STEREO_PCM12_ESP32.ino` | Classic V15 PCM12. Dual-core DSP, shared cursors in DRAM and a tick routine in assembly. E32_TX_MPX_MIN_CYCLES is set to 56. |
| 22 | **C5** · `CB-FMStereov1/CB_V15_PCM12/PCM12_ESP32C5/CB_FM_STEREO_PCM12_C5.ino` | C5 V15 PCM12. The transmit engine and acquisition path are specific to the C5 and its two bands. The V15 reference package uses 4 MB of flash. |
| 23 | **C5** · `CB-FMStereov1/CB_V15_PCM12/PCM12_ESP32C5v2/CB_FM_STEREO_PCM12_C5.ino` | C5 V15 v2. Both oscillators carry the same program, with a different target deviation. The tables in this directory and the update timing distinguish this variant. |
| 24 | **ESP32** · `CB-FMStereov1/CB_V15_PCM12/PCM12_ESP32v2/CB_FM_STEREO_PCM12_ESP32.ino` | Classic V15 v2. The cross-core synchronization files and the 80-cycle minimum are part of this version’s build profile. |
| 25 | **C5** · `CB-FMStereov1/CB_V15_PCM12/TESTONLY-C5-DUALCARRIER/CB_FM_STEREO_PCM12_C5.ino` | C5 test with two separate carriers carrying the same multiplex. The band policy file and the transmit backend differ. Track this test separately from v2. |
| 26 | **C5** · `CB-FMStereov1/CB_V15_PCM12/TESTONLY-C5-DUALCARRIER/TEST_2_INDEP_CARRIER_MONO/CB_FM_STEREO_PCM12_C5.ino` | Nested C5 test with two independent mono programs. Left/right routing and interpolation are specific to this directory. The directory tree contains a second sketch of the same name. |
| 27 | **C6** · `CB-FMStereov1/CB_V15_PCM12/TESTONLY-C6-DUALCARRIER/CB_FM_STEREO_PCM12_C6.ino` | C6 test with two separate carriers sharing one multiplex. Changes to the C6-specific calculations and tables are in the supporting files. The top-level .ino is very short. |
| 28 | **ESP32** · `CB-FMStereov1/CB_V15_PCM12/TESTONLY-ESP32-DUALCARRIER/CB_FM_STEREO_PCM12_ESP32.ino` | Classic test with two separate carriers. Uses an 80-cycle minimum interval and dual-core synchronization. The assembly code uses the Xtensa Classic calling convention. |
| 29 | **ESP8266** · `CB-FMStereov1/CB_V15_PCM12/TESTONLY-ESP8266-DUALCARRIER/CB_FM_STEREO_MPXRX_PCM12TX_ESP8266.ino` | ESP8266 test with two separate carriers sharing one multiplex. The hybrid protocol still uses PCM12 TX / MPX RX. The nested test directory is a separate target. |
| 30 | **ESP8266** · `CB-FMStereov1/CB_V15_PCM12/TESTONLY-ESP8266-DUALCARRIER/TEST_2_INDEP_CARRIER_MONO/CB_FM_STEREO_MPXRX_PCM12TX_ESP8266.ino` | Nested ESP8266 test with two mono programs. Independent audio channels change the transmit DSP. Build this sketch separately from its parent. |
| 31 | **ESP8266** · `CB_DuplexVoice_V3/CB_V4/CB_DuplexVoice_V4_Arduino/CB_DuplexVoice_V4_Arduino.ino` | Duplex Voice V4. TDD alternates time slots and uses PCM/companding and DPX1 frames with the advanced V4 page. The serial link runs at 460,800 baud, 8N2. |
| 32 | **ESP8266** · `CB_DuplexVoice_V3/CB_V5/CB_DuplexVoice_V4_Arduino/CB_DuplexVoice_V4_Arduino.ino` | Duplex Voice V5. The sketch keeps its V4 name. The EASY interface distinguishes this edition. The serial link still runs at 460,800 baud, 8N2. |
| 33 | **ESP8266** · `CB_DuplexVoice_V3/CB_V6/CB_DuplexVoice_V6_Arduino/CB_DuplexVoice_V6_Arduino.ino` | Duplex Voice V6 on ESP8266. Captures time slots in passive mode and uses DPX1 at 921,600 baud, 8N2. The V5 page uses a different baud rate. |
| 34 | **C5** · `CB_DuplexVoice_V3/CB_V6/CB_DuplexVoice_V6_C5/CB_DuplexVoice_V6_C5.ino` | Duplex Voice V6 on C5. Runs at 921,600 baud, 8N2. The C5 backend replaces the ESP8266 hardware dependencies. Keep the V6 runtime, timing and c5_cb_radio.cpp file. |
| 35 | **C5** · `CB_DuplexVoice_V3/CB_V7beta/CB_DuplexVoice_V6_C5/CB_DuplexVoice_V6_C5.ino` | Duplex Voice V7 beta on C5. Named V6 in the source tree. Adds DPT1 telemetry and identification in the interface. Preserve time-slot negotiation and the C5 backend. |
| 36 | **C6** · `CB_DuplexVoice_V3/CB_V7beta/CB_DuplexVoice_V6_C6/CB_DuplexVoice_V6_C6.ino` | Duplex Voice V7 beta on C6. DPT1/DPX1 at 921,600 baud, 8N2, with the C6 runtime and PHY. The reference board uses the hardware UART0. |
| 37 | **ESP32** · `CB_DuplexVoice_V3/CB_V7beta/CB_DuplexVoice_V6_ESP32/CB_DuplexVoice_V6_ESP32.ino` | Duplex Voice V7 beta on Classic. The peer_tick.S file rearms the tick when CONFIG_FREERTOS_SYSTICK_USES_CCOUNT is set. Preserve CALLX0, the argument in a2 and placement in IRAM. |
| 38 | **ESP8266** · `CB_DuplexVoice_V3/CB_V7beta/CB_DuplexVoice_V6_ESP8266/CB_DuplexVoice_V6_ESP8266.ino` | Duplex Voice V7 beta on ESP8266. This is a distinct version from the single-file V6 release. Keep the telemetry-frame decoding and the V7 page. |
| 39 | **ESP8266** · `FMSerialHQv1/B2_FM_LIVE_SERIAL_48K_WFM_B2_K4_TUNE1450_UART_FLOW8K_2000000.ino` | Serial mono FM transmitter. Converts samples into frequency deviation and uses a 2 Mbaud 8N1 UART stream. Keep the table and flow-control commands associated with this long sketch name. |
| 40 | **ESP8266** · `FMStereoHQv1/B2_FM_LIVE_SERIAL_STEREO_120K_MPX8.ino` | V1 stereo FM transmitter. The browser generates the signed MPX8 multiplex at 120 ksample/s. The channel is fixed. Stereo recovery depends on the pilot tone encoded by the page. |
| 41 | **ESP8266** · `FMStereoHQv2/B2_FM_LIVE_SERIAL_STEREO_120K_MPX8.ino` | V2 stereo FM transmitter. Adds channel selection and retains the MPX8 serial interface. Keep the page that configures the channel. |
| 42 | **ESP8266** · `FMStereoHQv3/B2_FM_LIVE_SERIAL_STEREO_120K_MPX8.ino` | V3 stereo FM transmitter. The firmware matches V2. The new 57 kHz RDS feature is in the HTML encoder. Archive that file too so the V3 feature can be reproduced. |
| 43 | **ESP8266** · `FMStereoRX-YEAHHHHHv1/FMStereoRX.ino` | Standalone stereo receiver. Outputs MPX8. The browser recovers the audio, pilot tone and RDS. 1.5 Mbaud 8N2, with selectable acquisition profiles. |
| 44 | **ESP8266** · `MODEM-SimplexV1/FMFileModemRX/FMFileModemRX.ino` | Simplex modem, RX station. The ESP8266 acquisition firmware and the FM_FILE_MODEM_RX.html page form the reference pair. Serial stream at 1.5 Mbaud 8N2. |
| 45 | **ESP8266** · `MODEM-SimplexV1/FMFileModemTX/FMFileModemTX.ino` | Simplex modem, TX station. Uses the modulation firmware and the FM_FILE_MODEM_TX.html page at 2 Mbaud 8N1. The pages handle file framing, redundancy and final verification. |
| 46 | **ESP8266** · `SSB-RX-YEAHHHHHv1/SSBReceiverHQ.ino` | SSB receiver. IQ10 frames carry complex sample pairs with 10-bit components, packed two pairs per five bytes, at 48,000 complex samples/s. The browser demodulates USB/LSB. |
| 47 | **ESP8266** · `SSBTestv1/B2_SSB_LIVE_SERIAL_HIFI.ino` | ESP8266 serial SSB transmitter. Encoding runs in the browser and the chip handles update timing. Keep the sampling profile and the encoder that produces the data. |
| 48 | **C5** · `SSBTestv1/ESP32-C5v1/B2_SSB_LIVE_SERIAL_HIFI_C5/B2_SSB_LIVE_SERIAL_HIFI_C5.ino` | C5 SSB transmitter. Keep radio.cpp and its .h alongside the sketch. The firmware uses frequency adjustment and GPTimer. The reference test board has 8 MB of flash, and the serial input is specific to the C5 port. |
| 49 | **ESP8266** · `WIFIOnlyTest/B2_WFM_MONO_HIFI_48K_CONTROLE_MODULATION_APERCU.ino` | Wi-Fi-only mono WFM transmitter. The sketch embeds the HTTP server and page, and LittleFS stores the WFM2 clip. Serial diagnostics run separately from the audio stream. Compile this .ino on its own. |
| 50 | **ESP8266** · `WIFIOnlyTest/ESP8266_AM.ino` | Wi-Fi-only AM transmitter. The firmware verifies the AMF1 clip and stores it in LittleFS. The server, page and embedded data all belong to this independent .ino. |
| 51 | **ESP8266** · `WIFIOnlyTest/ESP8266_SSB.ino` | Wi-Fi-only SSB transmitter. Uses LittleFS storage in SBF1 format and USB/LSB encoding. The flash partition layout determines how much space remains for a clip after the build. |
| 52 | **ESPHome** · `CB-FMStereov1/CB_FM_STEREO.yaml` | Standalone ESPHome YAML. Uses inline C++ code, PlatformIO source filtering and linker aliases. Archive the generated code as well. Legacy protocol 3 at 2 Mbaud. |

### Build inputs and their hashes

For each target, paths to supporting files are relative to the sketch directory. The SHA-256 digest covers **all build inputs for that target found in the supplied repository archive**, including header files. To reproduce it, sort the relative paths in ordinal order. For each path, feed its UTF-8 encoding with `/` separators into the hash, followed by one null byte and the 32 bytes of the SHA-256 digest of the file’s raw contents. Use the original name of the main file when calculating this digest. SDK tools have their own hashes. Two sketches with the same name in the preceding table can therefore have very different sets of build inputs.

| No. | Input files | Implementation files beside the sketch or under src/, and local options | SHA-256 of the input set |
|---:|---:|---|---|
| 01 | 1 | `.ino only` | `6b65d32bc1c331255614fcc1d200a980d7941ce6a895c7ed0572c40a16d12aed` |
| 02 | 3 | `rx_radio.cpp` | `635ef253587c2383415b8c6fc8fc7fda5b43126aabfa12f76738bb15485f436c` |
| 03 | 1 | `.ino only` | `073e07678cfb41348d4e7cd31a92b84eaeb7b1216c32537501116e8bcae55ad6` |
| 04 | 3 | `radio.cpp` | `41667880f2dfe9d4578c4ea21bb4f157e2aa2536e898ad23a79e101e0a72b3b9` |
| 05 | 1 | `.ino only` | `c65f42da0c4dbe83a7b8e3435822033a67aa2c7549f8cbb331b8d5dfebe0fc32` |
| 06 | 10 | `config.h`, `fm_tx.h`, `hardware_esp8266.h`, `iq_rx.h`, `radio_phy.h`, `rf.h`, `serial_commands.h`, `tx_control.h`, `uart.h` | `232d5417893bb74c23579e05aa6ec8b0031ba7b3d9bf94f9799a5e7f6edbebcf` |
| 07 | 10 | `config.h`, `fm_tx.h`, `hardware_esp8266.h`, `iq_rx.h`, `radio_phy.h`, `rf.h`, `serial_commands.h`, `tx_control.h`, `uart.h` | `8f35c033f1efa8fa374cd6a0889de0f5a6636d58daba83f5c450b9666f715d59` |
| 08 | 10 | `config.h`, `fm_tx.h`, `hardware_esp8266.h`, `iq_rx.h`, `radio_phy.h`, `rf.h`, `serial_commands.h`, `tx_control.h`, `uart.h` | `6376b2925780e292f0fbe7fb25104c3b36b0820888fa1367500f43a7665f3908` |
| 09 | 14 | `src/arduino_phy_compat.c`, `src/arduino_runtime.cpp`, `src/c5_radio.c`, `src/c5_rx.c`, `src/c5_tx.c`, `src/c5_tx_radio.cpp`, `src/firmware.cpp`, `build_opt.h → -O2` | `07251dcf58ead851fc4256a536fa50a9a086c0d096ee02d020740b5f211f17bc` |
| 10 | 14 | `src/arduino_phy_compat.c`, `src/arduino_runtime.cpp`, `src/c6_radio.c`, `src/c6_rx.c`, `src/c6_tx.c`, `src/c6_tx_radio.cpp`, `src/firmware.cpp`, `build_opt.h → -O2` | `be8cb14be8f84ac7df90ca0ebf03f1e5a219077c08cb6649a8daab8ee5ac5988` |
| 11 | 16 | `src/arduino_phy_compat.c`, `src/arduino_runtime.cpp`, `src/e32_adc_bench.c`, `src/e32_radio.c`, `src/e32_rx.c`, `src/e32_tx.c`, `src/e32_tx_radio.cpp`, `src/firmware.cpp`, `build_opt.h → -O2` | `80f2bc950bb0f8ff57b61b15bd060dd1484edc5ad9a88d8b560eb21c3ae54618` |
| 12 | 17 | `src/arduino_phy_compat.c`, `src/arduino_runtime.cpp`, `src/c5_radio.c`, `src/c5_rx.c`, `src/c5_tx.c`, `src/c5_tx_radio.cpp`, `src/firmware.cpp`, `build_opt.h → -O2` | `7b64f192903688585fa793ebf010c4ef6a1c680496ba0531056eb18ba68450ff` |
| 13 | 16 | `src/arduino_phy_compat.c`, `src/arduino_runtime.cpp`, `src/c5_radio.c`, `src/c5_rx.c`, `src/c5_tx.c`, `src/c5_tx_radio.cpp`, `src/firmware.cpp`, `build_opt.h → -O2` | `db1ac29b73e9e08a7e690d58d91bd40ac3ba6e1f825077e087f38ce6474cdb5a` |
| 14 | 14 | `src/arduino_phy_compat.c`, `src/arduino_runtime.cpp`, `src/c6_radio.c`, `src/c6_rx.c`, `src/c6_tx.c`, `src/c6_tx_radio.cpp`, `src/firmware.cpp`, `build_opt.h → -O2` | `46ef94ada8dde5297db52d654a587f414119ca77288513a01c3de97beb94c818` |
| 15 | 16 | `src/arduino_phy_compat.c`, `src/arduino_runtime.cpp`, `src/e32_adc_bench.c`, `src/e32_radio.c`, `src/e32_rx.c`, `src/e32_tx.c`, `src/e32_tx_radio.cpp`, `src/firmware.cpp`, `build_opt.h → -O2` | `732650c4f6c19a56a05647b548c9fdc5c990b4cb5afdfdc5859f10fffa44108e` |
| 16 | 10 | `config.h`, `fm_tx.h`, `hardware_esp8266.h`, `iq_rx.h`, `radio_phy.h`, `rf.h`, `serial_commands.h`, `tx_control.h`, `uart.h` | `2146f5993315851b64d985ea0ef818a058839f858eb04e8ca1a4f9a26006676e` |
| 17 | 11 | `config.h`, `fm_tx.h`, `hardware_esp8266.h`, `iq_rx.h`, `pcm_tx.h`, `radio_phy.h`, `rf.h`, `serial_commands.h`, `tx_control.h`, `uart.h` | `666a78c4d04172694e54ec619534b18ffceec3af58ed0b4e5ad869798401e611` |
| 18 | 11 | `config.h`, `fm_tx.h`, `hardware_esp8266.h`, `iq_rx.h`, `pcm_tx.h`, `radio_phy.h`, `rf.h`, `serial_commands.h`, `tx_control.h`, `uart.h` | `b45b2fc6edd48238d404276644d337f3cec9bde785ddb94c00fbce9c5cfe2d77` |
| 19 | 18 | `src/arduino_phy_compat.c`, `src/arduino_runtime.cpp`, `src/c6_radio.c`, `src/c6_rx.c`, `src/c6_rx_pcm.c`, `src/c6_tx.c`, `src/c6_tx_radio.cpp`, `src/firmware.cpp`, `build_opt.h → -O2` | `8124089ac653201799d9ad8f273b189bfdd2008191ef7e833e047836028e505b` |
| 20 | 18 | `src/arduino_phy_compat.c`, `src/arduino_runtime.cpp`, `src/c6_radio.c`, `src/c6_rx.c`, `src/c6_rx_pcm.c`, `src/c6_tx.c`, `src/c6_tx_radio.cpp`, `src/firmware.cpp`, `build_opt.h → -O2` | `bfc57e3743e84659dc0a69e7497dcb05e6aae22e5796086546d9b1bd53079444` |
| 21 | 24 | `src/arduino_phy_compat.c`, `src/arduino_runtime.cpp`, `src/e32_adc_bench.c`, `src/e32_pcm_worker.cpp`, `src/e32_radio.c`, `src/e32_rx.c`, `src/e32_rx_pcm.c`, `src/e32_tick.S`, `src/e32_tx.c`, `src/e32_tx_radio.cpp`, `src/firmware.cpp`, `build_opt.h → -O2 -DE32_TX_MPX_MIN_CYCLES=56` | `dec42774e081da03f5f858c287dff7dce5794564f718359907fbfab1d316768f` |
| 22 | 20 | `src/arduino_phy_compat.c`, `src/arduino_runtime.cpp`, `src/c5_radio.c`, `src/c5_rx.c`, `src/c5_rx_pcm.c`, `src/c5_tx.c`, `src/c5_tx_radio.cpp`, `src/firmware.cpp`, `build_opt.h → -O2` | `54d23102650d87c43bce3b3580152c273835cba87ed6326b2deb20c83d71e1c8` |
| 23 | 20 | `src/arduino_phy_compat.c`, `src/arduino_runtime.cpp`, `src/c5_radio.c`, `src/c5_rx.c`, `src/c5_rx_pcm.c`, `src/c5_tx.c`, `src/c5_tx_radio.cpp`, `src/firmware.cpp`, `build_opt.h → -O2` | `8fa6d82b99092eb9f5be3ad3e5b32f957b3a438d69420e21ef50174f161bf5be` |
| 24 | 24 | `src/arduino_phy_compat.c`, `src/arduino_runtime.cpp`, `src/e32_adc_bench.c`, `src/e32_pcm_worker.cpp`, `src/e32_radio.c`, `src/e32_rx.c`, `src/e32_rx_pcm.c`, `src/e32_tick.S`, `src/e32_tx.c`, `src/e32_tx_radio.cpp`, `src/firmware.cpp`, `build_opt.h → -O2 -DE32_TX_MPX_MIN_CYCLES=80` | `1b86799e7b6d641fb9a14371d70b94e3c32a70bd77d91047beaa05676bb1ce3b` |
| 25 | 20 | `src/arduino_phy_compat.c`, `src/arduino_runtime.cpp`, `src/c5_radio.c`, `src/c5_rx.c`, `src/c5_rx_pcm.c`, `src/c5_tx.c`, `src/c5_tx_radio.cpp`, `src/firmware.cpp`, `build_opt.h → -O2` | `7e7df45d742215a99c1ec14e5f76de8d5dd68ef1b5841615ee829b15736a8e57` |
| 26 | 20 | `src/arduino_phy_compat.c`, `src/arduino_runtime.cpp`, `src/c5_radio.c`, `src/c5_rx.c`, `src/c5_rx_pcm.c`, `src/c5_tx.c`, `src/c5_tx_radio.cpp`, `src/firmware.cpp`, `build_opt.h → -O2` | `31548676548aad96fb82132c735281551292005f36b32f5e8b86a6cc13e50225` |
| 27 | 18 | `src/arduino_phy_compat.c`, `src/arduino_runtime.cpp`, `src/c6_radio.c`, `src/c6_rx.c`, `src/c6_rx_pcm.c`, `src/c6_tx.c`, `src/c6_tx_radio.cpp`, `src/firmware.cpp`, `build_opt.h → -O2` | `971aaeb7b141c72d0de25d08dd347b0a90160b4dab94dcfcfc65bcfe5c84e5da` |
| 28 | 24 | `src/arduino_phy_compat.c`, `src/arduino_runtime.cpp`, `src/e32_adc_bench.c`, `src/e32_pcm_worker.cpp`, `src/e32_radio.c`, `src/e32_rx.c`, `src/e32_rx_pcm.c`, `src/e32_tick.S`, `src/e32_tx.c`, `src/e32_tx_radio.cpp`, `src/firmware.cpp`, `build_opt.h → -O2 -DE32_TX_MPX_MIN_CYCLES=80` | `aa3d5208acf7627dd5e088957070c09a6e2b40bf3bb3dceafd404dae503878fc` |
| 29 | 11 | `config.h`, `fm_tx.h`, `hardware_esp8266.h`, `iq_rx.h`, `pcm_tx.h`, `radio_phy.h`, `rf.h`, `serial_commands.h`, `tx_control.h`, `uart.h` | `4c923072f70e917d895fbf29c4fe4680211aa85a287e2031b61e3b91b23cbfa3` |
| 30 | 11 | `config.h`, `fm_tx.h`, `hardware_esp8266.h`, `iq_rx.h`, `pcm_tx.h`, `radio_phy.h`, `rf.h`, `serial_commands.h`, `tx_control.h`, `uart.h` | `11301dbae6cccd2e7fabd75f29789719dde62ca6e15670f1cc61393adedb5812` |
| 31 | 1 | `.ino only` | `ac6378e55f6a56e219f49d956b68d773e0fbdf534f4de2bfa04655a326c3bc80` |
| 32 | 1 | `.ino only` | `ac6378e55f6a56e219f49d956b68d773e0fbdf534f4de2bfa04655a326c3bc80` |
| 33 | 1 | `.ino only` | `f1bc866f13a892a04bbb52297b22cdc7b6fd8a52cc841485a849f2ad99bcca46` |
| 34 | 8 | `src/arduino_phy_compat.c`, `src/arduino_runtime.cpp`, `src/c5_cb_radio.cpp`, `src/firmware.cpp`, `build_opt.h → -O2` | `1f6d0c37a04d4ca31029169fe90cec43cc1007d104188316c3bd92ec92be977a` |
| 35 | 8 | `src/arduino_phy_compat.c`, `src/arduino_runtime.cpp`, `src/c5_cb_radio.cpp`, `src/firmware.cpp`, `build_opt.h → -O2` | `a1718e36f91a36aad0d1f2efbc5c19ea4b8e78a4ac367b7bc528da6f45805d04` |
| 36 | 8 | `src/arduino_phy_compat.c`, `src/arduino_runtime.cpp`, `src/c6_cb_radio.cpp`, `src/firmware.cpp`, `build_opt.h → -O2` | `99316a91a893fcb019c0a5e4ae0f9e110584e994257a76771304d1f39566c66e` |
| 37 | 11 | `src/arduino_phy_compat.c`, `src/arduino_runtime.cpp`, `src/e32_cb_radio.cpp`, `src/e32_radio.c`, `src/firmware.cpp`, `src/peer_tick.S`, `build_opt.h → -O2` | `9d31ac300ed35078fccdc65d75573b34ac430629c19ea863efd577969c35f4b0` |
| 38 | 1 | `.ino only` | `349a64c63b5cee3d8a50142be70d087e58ea8b2a8daf18901ee2212ee9ba9a16` |
| 39 | 1 | `.ino only` | `92bd3c2ce73636a9352901281c0b395942ef837270f84ddbb30f6c4f9adf0495` |
| 40 | 1 | `.ino only` | `a4154ed63003d3a3e813ea732ea2c8594f21159a7a18bb58edbaf28272095a4a` |
| 41 | 1 | `.ino only` | `c7cc16898f3a2f40c84222718b5f00f8d719c2553904788c9b278b289b8a5201` |
| 42 | 1 | `.ino only` | `c7cc16898f3a2f40c84222718b5f00f8d719c2553904788c9b278b289b8a5201` |
| 43 | 1 | `.ino only` | `2da5c34dff74bfc92559dc325045bfab7996dc5a34ddbb11ae38030376ef7281` |
| 44 | 1 | `.ino only` | `df13ead469aab6081a86abf0101e33c4e4e886441e42a592a14c9dbab1f70f4e` |
| 45 | 1 | `.ino only` | `ac8f6be38d6cd8fb7aee895e6d28c2c7c9ffe4111e9d5f329c02194b0797f411` |
| 46 | 1 | `.ino only` | `9ebbd8b16fdb194b5704cf2a774b484d8d26f54f9be12f2cb00a891d4f3719e3` |
| 47 | 1 | `.ino only` | `e9690c0ccba338ca6521c8e6bed891861bf916c673ef46df15dd3c91c5bdc963` |
| 48 | 3 | `radio.cpp` | `00696ceee6f403f149ff9444babe392d2b6a751817a318990f542f6f0739bb09` |
| 49 | 1 | `.ino only` | `a796680da92708c43a745a52bef51ec959f70013024c922476ce60b9035cd5c7` |
| 50 | 1 | `.ino only` | `fa46a92430a1a6dd37f071c3f0f8262f3d10b34985d01c14e91e94909a00a615` |
| 51 | 1 | `.ino only` | `a3685872185a9f99c2e95f385c73bd54dbc75bf7ec820cf14fc4a44bfcbde688` |
| 52 | 1 | `YAML only` | `112b63cb0331c5406cea1336c6c8d9363cdb8b76fc5646f9131c023482e393d4` |

### Hardware-related identifiers by target and source file

Use this table to trace a linker error or timing issue to the relevant source file. Each identifier is listed with the line number of its first occurrence in the named file, including occurrences in comments. Private functions, data objects, macros, assembly instructions and functions defined by the repository are all starting points for reading the code. To find what actually supplies a definition at link time, follow the ELF/MAP procedure in Chapter 8.

| No. | Identifiers found in this target |
|---:|---|
| 01 | `AMReceiverHQ.ino` — `chip6_phy_init_ctrl (L42)`, `set_rf_freq_offset (L41)`, `IRAM_ATTR (L208)` |
| 02 | `rx_radio.cpp` — `phy_param (L18)`, `phy_set_rf_freq_offset (L20)`, `phy_force_rx_gain (L26)`, `phy_set_chanfreq (L19)` · `AMReceiverHQ_C5.ino` — `gptimer_new_timer (L131)`, `hal/uart_ll.h (L6)`, `IRAM_ATTR (L88)` |
| 03 | `B2_AM_LIVE_SERIAL_HIFI_OV2.ino` — `set_txclk (L124)`, `set_ana_scale (L125)`, `timer1_attachInterrupt (L383)`, `IRAM_ATTR (L130)`, `UART_FIFO (L99)`, `memw (L127)` |
| 04 | `radio.cpp` — `phy_param (L16)`, `RFChannelSel (L15)` · `AMSerialHQv1_C5.ino` — `gptimer_new_timer (L162)`, `hal/uart_ll.h (L6)`, `IRAM_ATTR (L105)` |
| 05 | `CB_FM_STEREOv5.ino` — `set_txclk (L91)`, `set_ana_scale (L92)`, `chip6_phy_init_ctrl (L36)`, `set_rf_freq_offset (L35)`, `timer1_attachInterrupt (L307)`, `IRAM_ATTR (L97)`, `UART_FIFO (L66)`, `memw (L94)` |
| 06 | `hardware_esp8266.h` — `set_txclk (L29)`, `set_ana_scale (L30)`, `UART_FIFO (L4)` · `CB_FM_STEREOv10_Arduino.ino` — `chip6_phy_init_ctrl (L36)`, `set_rf_freq_offset (L35)` · `fm_tx.h` — `timer1_attachInterrupt (L116)`, `IRAM_ATTR (L41)`, `memw (L45)` |
| 07 | `hardware_esp8266.h` — `set_txclk (L29)`, `set_ana_scale (L30)`, `UART_FIFO (L4)` · `CB_FM_STEREOv12_Arduino.ino` — `chip6_phy_init_ctrl (L36)`, `set_rf_freq_offset (L35)` · `fm_tx.h` — `timer1_attachInterrupt (L116)`, `IRAM_ATTR (L41)`, `memw (L45)` |
| 08 | `hardware_esp8266.h` — `set_txclk (L29)`, `set_ana_scale (L30)`, `UART_FIFO (L4)` · `CB_FM_STEREOv13_Arduino.ino` — `chip6_phy_init_ctrl (L37)`, `set_rf_freq_offset (L36)` · `fm_tx.h` — `timer1_attachInterrupt (L116)`, `IRAM_ATTR (L41)`, `memw (L45)` |
| 09 | `src/arduino_phy_compat.c` — `phy_param (L31)`, `RFChannelSel (L32)`, `esp_phy_wifi_tx_tone (L5)` · `src/c5_radio.c` — `phy_set_rf_freq_offset (L16)`, `phy_force_rx_gain (L23)`, `phy_set_chanfreq (L15)`, `IRAM_ATTR (L33)` · `src/firmware.cpp` — `hal/uart_ll.h (L11)` · `src/arduino_runtime.cpp` — `CONFIG_PM_ENABLE (L86)` |
| 10 | `src/arduino_phy_compat.c` — `phy_param (L31)`, `RFChannelSel (L32)`, `ram_start_tx_tone_step (L37)`, `esp_phy_wifi_tx_tone (L5)` · `src/c6_radio.c` — `rom_chip_i2c_readReg (L24)`, `chip_v7_set_chan (L15)`, `IRAM_ATTR (L46)` · `src/firmware.cpp` — `hal/uart_ll.h (L11)` · `src/arduino_runtime.cpp` — `CONFIG_PM_ENABLE (L86)` |
| 11 | `src/arduino_phy_compat.c` — `g_phyFuns (L4)` · `src/e32_adc_bench.c` — `SOC_RESERVE_MEMORY_REGION (L47)`, `esp_cpu_get_cycle_count (L53)`, `memw (L55)` · `src/e32_radio.c` — `rom_set_rf_freq_offset (L22)` · `src/arduino_runtime.cpp` — `esp_ipc_isr (L7)`, `CONFIG_FREERTOS_UNICORE (L13)` · `src/firmware.cpp` — `hal/uart_ll.h (L11)` |
| 12 | `src/arduino_phy_compat.c` — `phy_param (L31)`, `RFChannelSel (L32)`, `esp_phy_wifi_tx_tone (L5)` · `src/c5_radio.c` — `phy_set_rf_freq_offset (L16)`, `phy_force_rx_gain (L23)`, `phy_set_chanfreq (L15)`, `IRAM_ATTR (L49)` · `src/firmware.cpp` — `hal/uart_ll.h (L11)` · `src/arduino_runtime.cpp` — `CONFIG_PM_ENABLE (L86)` |
| 13 | `src/arduino_phy_compat.c` — `phy_param (L31)`, `RFChannelSel (L32)`, `esp_phy_wifi_tx_tone (L5)` · `src/c5_radio.c` — `phy_set_rf_freq_offset (L16)`, `phy_force_rx_gain (L23)`, `phy_set_chanfreq (L15)`, `IRAM_ATTR (L49)` · `src/firmware.cpp` — `hal/uart_ll.h (L11)` · `src/arduino_runtime.cpp` — `CONFIG_PM_ENABLE (L86)` |
| 14 | `src/arduino_phy_compat.c` — `phy_param (L31)`, `RFChannelSel (L32)`, `ram_start_tx_tone_step (L37)`, `esp_phy_wifi_tx_tone (L5)` · `src/c6_radio.c` — `rom_chip_i2c_readReg (L24)`, `chip_v7_set_chan (L15)`, `IRAM_ATTR (L46)` · `src/firmware.cpp` — `hal/uart_ll.h (L11)` · `src/arduino_runtime.cpp` — `CONFIG_PM_ENABLE (L86)` |
| 15 | `src/arduino_phy_compat.c` — `g_phyFuns (L4)` · `src/e32_adc_bench.c` — `SOC_RESERVE_MEMORY_REGION (L47)`, `esp_cpu_get_cycle_count (L53)`, `memw (L55)` · `src/e32_radio.c` — `rom_set_rf_freq_offset (L22)` · `src/arduino_runtime.cpp` — `esp_ipc_isr (L7)`, `CONFIG_FREERTOS_UNICORE (L13)` · `src/firmware.cpp` — `hal/uart_ll.h (L11)` |
| 16 | `hardware_esp8266.h` — `set_txclk (L29)`, `set_ana_scale (L30)`, `UART_FIFO (L4)` · `CB_FM_STEREOv13_ESP8266.ino` — `chip6_phy_init_ctrl (L37)`, `set_rf_freq_offset (L36)` · `fm_tx.h` — `timer1_attachInterrupt (L134)`, `IRAM_ATTR (L52)`, `memw (L56)` |
| 17 | `hardware_esp8266.h` — `set_txclk (L29)`, `set_ana_scale (L30)`, `UART_FIFO (L4)` · `CB_FM_STEREO_MPXRX_PCM12TX_ESP8266.ino` — `chip6_phy_init_ctrl (L37)`, `set_rf_freq_offset (L36)` · `fm_tx.h` — `timer1_attachInterrupt (L220)`, `IRAM_ATTR (L139)`, `memw (L88)` |
| 18 | `hardware_esp8266.h` — `set_txclk (L29)`, `set_ana_scale (L30)`, `UART_FIFO (L4)` · `CB_FM_STEREO_MPXRX_PCM12TX_ESP8266.ino` — `chip6_phy_init_ctrl (L37)`, `set_rf_freq_offset (L36)` · `fm_tx.h` — `timer1_attachInterrupt (L231)`, `IRAM_ATTR (L163)`, `memw (L107)` |
| 19 | `src/arduino_phy_compat.c` — `phy_param (L31)`, `RFChannelSel (L32)`, `ram_start_tx_tone_step (L37)`, `esp_phy_wifi_tx_tone (L5)` · `src/c6_radio.c` — `rom_chip_i2c_readReg (L24)`, `chip_v7_set_chan (L15)` · `src/firmware.cpp` — `hal/uart_ll.h (L11)` · `src/arduino_runtime.cpp` — `CONFIG_PM_ENABLE (L86)` · `src/c6_pcm_tx_dsp.h` — `IRAM_ATTR (L167)` |
| 20 | `src/arduino_phy_compat.c` — `phy_param (L31)`, `RFChannelSel (L32)`, `ram_start_tx_tone_step (L37)`, `esp_phy_wifi_tx_tone (L5)` · `src/c6_radio.c` — `rom_chip_i2c_readReg (L24)`, `chip_v7_set_chan (L15)` · `src/firmware.cpp` — `hal/uart_ll.h (L11)` · `src/arduino_runtime.cpp` — `CONFIG_PM_ENABLE (L86)` · `src/c6_pcm_tx_dsp.h` — `IRAM_ATTR (L167)` |
| 21 | `src/arduino_phy_compat.c` — `g_phyFuns (L4)` · `src/arduino_runtime.cpp` — `e32_rearm_peer_tick (L25)`, `esp_ipc_isr (L8)`, `esp_cpu_get_cycle_count (L61)`, `CONFIG_FREERTOS_UNICORE (L18)` · `src/e32_adc_bench.c` — `SOC_RESERVE_MEMORY_REGION (L47)`, `memw (L55)` · `src/e32_radio.c` — `rom_set_rf_freq_offset (L22)` · `src/firmware.cpp` — `hal/uart_ll.h (L11)` |
| 22 | `src/arduino_phy_compat.c` — `phy_param (L31)`, `RFChannelSel (L32)`, `esp_phy_wifi_tx_tone (L5)` · `src/c5_radio.c` — `phy_set_rf_freq_offset (L16)`, `phy_force_rx_gain (L23)`, `phy_set_chanfreq (L15)` · `src/firmware.cpp` — `hal/uart_ll.h (L11)` · `src/arduino_runtime.cpp` — `CONFIG_PM_ENABLE (L86)` · `src/c5_pcm_tx_dsp.h` — `IRAM_ATTR (L116)` |
| 23 | `src/arduino_phy_compat.c` — `phy_param (L31)`, `RFChannelSel (L32)`, `esp_phy_wifi_tx_tone (L5)` · `src/c5_radio.c` — `phy_set_rf_freq_offset (L16)`, `phy_force_rx_gain (L23)`, `phy_set_chanfreq (L15)` · `src/firmware.cpp` — `hal/uart_ll.h (L11)` · `src/arduino_runtime.cpp` — `CONFIG_PM_ENABLE (L86)` · `src/c5_pcm_tx_dsp.h` — `IRAM_ATTR (L116)` |
| 24 | `src/arduino_phy_compat.c` — `g_phyFuns (L4)` · `src/arduino_runtime.cpp` — `e32_rearm_peer_tick (L25)`, `esp_ipc_isr (L8)`, `esp_cpu_get_cycle_count (L61)`, `CONFIG_FREERTOS_UNICORE (L18)` · `src/e32_adc_bench.c` — `SOC_RESERVE_MEMORY_REGION (L47)`, `memw (L55)` · `src/e32_radio.c` — `rom_set_rf_freq_offset (L22)` · `src/firmware.cpp` — `hal/uart_ll.h (L11)` |
| 25 | `src/arduino_phy_compat.c` — `phy_param (L31)`, `RFChannelSel (L32)`, `esp_phy_wifi_tx_tone (L5)` · `src/c5_radio.c` — `phy_set_rf_freq_offset (L16)`, `phy_force_rx_gain (L23)`, `phy_set_chanfreq (L15)` · `src/firmware.cpp` — `hal/uart_ll.h (L11)` · `src/arduino_runtime.cpp` — `CONFIG_PM_ENABLE (L86)` · `src/c5_pcm_tx_dsp.h` — `IRAM_ATTR (L116)` |
| 26 | `src/arduino_phy_compat.c` — `phy_param (L31)`, `RFChannelSel (L32)`, `esp_phy_wifi_tx_tone (L5)` · `src/c5_radio.c` — `phy_set_rf_freq_offset (L16)`, `phy_force_rx_gain (L23)`, `phy_set_chanfreq (L15)` · `src/firmware.cpp` — `hal/uart_ll.h (L11)` · `src/arduino_runtime.cpp` — `CONFIG_PM_ENABLE (L86)` · `src/c5_pcm_tx_dsp.h` — `IRAM_ATTR (L58)` |
| 27 | `src/arduino_phy_compat.c` — `phy_param (L31)`, `RFChannelSel (L32)`, `ram_start_tx_tone_step (L37)`, `esp_phy_wifi_tx_tone (L5)` · `src/c6_radio.c` — `rom_chip_i2c_readReg (L24)`, `chip_v7_set_chan (L15)` · `src/firmware.cpp` — `hal/uart_ll.h (L11)` · `src/arduino_runtime.cpp` — `CONFIG_PM_ENABLE (L86)` · `src/c6_pcm_tx_dsp.h` — `IRAM_ATTR (L167)` |
| 28 | `src/arduino_phy_compat.c` — `g_phyFuns (L4)` · `src/arduino_runtime.cpp` — `e32_rearm_peer_tick (L25)`, `esp_ipc_isr (L8)`, `esp_cpu_get_cycle_count (L61)`, `CONFIG_FREERTOS_UNICORE (L18)` · `src/e32_adc_bench.c` — `SOC_RESERVE_MEMORY_REGION (L47)`, `memw (L55)` · `src/e32_radio.c` — `rom_set_rf_freq_offset (L22)` · `src/firmware.cpp` — `hal/uart_ll.h (L11)` |
| 29 | `hardware_esp8266.h` — `set_txclk (L29)`, `set_ana_scale (L30)`, `UART_FIFO (L4)` · `CB_FM_STEREO_MPXRX_PCM12TX_ESP8266.ino` — `chip6_phy_init_ctrl (L37)`, `set_rf_freq_offset (L36)` · `fm_tx.h` — `timer1_attachInterrupt (L241)`, `IRAM_ATTR (L173)`, `memw (L117)` |
| 30 | `hardware_esp8266.h` — `set_txclk (L29)`, `set_ana_scale (L30)`, `UART_FIFO (L4)` · `CB_FM_STEREO_MPXRX_PCM12TX_ESP8266.ino` — `chip6_phy_init_ctrl (L37)`, `set_rf_freq_offset (L36)` · `fm_tx.h` — `timer1_attachInterrupt (L253)`, `IRAM_ATTR (L147)`, `memw (L94)` |
| 31 | `CB_DuplexVoice_V4_Arduino.ino` — `chip6_phy_init_ctrl (L19)`, `set_rf_freq_offset (L18)`, `IRAM_ATTR (L121)`, `memw (L103)` |
| 32 | `CB_DuplexVoice_V4_Arduino.ino` — `chip6_phy_init_ctrl (L19)`, `set_rf_freq_offset (L18)`, `IRAM_ATTR (L121)`, `memw (L103)` |
| 33 | `CB_DuplexVoice_V6_Arduino.ino` — `chip6_phy_init_ctrl (L19)`, `set_rf_freq_offset (L18)`, `IRAM_ATTR (L123)`, `memw (L105)` |
| 34 | `src/arduino_phy_compat.c` — `phy_param (L31)`, `RFChannelSel (L32)`, `esp_phy_wifi_tx_tone (L5)` · `src/c5_cb_radio.cpp` — `phy_set_rf_freq_offset (L28)`, `phy_force_rx_gain (L40)`, `phy_set_chanfreq (L27)`, `IRAM_ATTR (L106)` · `src/firmware.cpp` — `hal/uart_ll.h (L18)` · `src/arduino_runtime.cpp` — `CONFIG_PM_ENABLE (L86)` |
| 35 | `src/arduino_phy_compat.c` — `phy_param (L31)`, `RFChannelSel (L32)`, `esp_phy_wifi_tx_tone (L5)` · `src/c5_cb_radio.cpp` — `phy_set_rf_freq_offset (L28)`, `phy_force_rx_gain (L41)`, `phy_set_chanfreq (L27)`, `IRAM_ATTR (L109)` · `src/firmware.cpp` — `hal/uart_ll.h (L18)` · `src/arduino_runtime.cpp` — `CONFIG_PM_ENABLE (L86)` |
| 36 | `src/arduino_phy_compat.c` — `phy_param (L31)`, `RFChannelSel (L32)`, `ram_start_tx_tone_step (L37)`, `esp_phy_wifi_tx_tone (L5)` · `src/c6_cb_radio.cpp` — `rom_chip_i2c_readReg (L36)`, `chip_v7_set_chan (L22)`, `IRAM_ATTR (L80)` · `src/firmware.cpp` — `hal/uart_ll.h (L17)` · `CB_DuplexVoice_V6_C6.ino` — `CONFIG_PM_ENABLE (L8)` |
| 37 | `src/arduino_phy_compat.c` — `g_phyFuns (L4)` · `src/arduino_runtime.cpp` — `e32_rearm_peer_tick (L21)`, `esp_ipc_isr (L7)`, `CONFIG_FREERTOS_UNICORE (L15)` · `src/e32_radio.c` — `rom_set_rf_freq_offset (L22)` · `src/e32_cb_radio.cpp` — `esp_cpu_get_cycle_count (L158)`, `memw (L74)` · `src/firmware.cpp` — `hal/uart_ll.h (L18)` |
| 38 | `CB_DuplexVoice_V6_ESP8266.ino` — `chip6_phy_init_ctrl (L21)`, `set_rf_freq_offset (L18)`, `IRAM_ATTR (L133)`, `memw (L115)` |
| 39 | `B2_FM_LIVE_SERIAL_48K_WFM_B2_K4_TUNE1450_UART_FLOW8K_2000000.ino` — `set_txclk (L124)`, `set_ana_scale (L125)`, `timer1_attachInterrupt (L422)`, `IRAM_ATTR (L130)`, `UART_FIFO (L99)`, `LittleFS (L5)`, `memw (L127)` |
| 40 | `B2_FM_LIVE_SERIAL_STEREO_120K_MPX8.ino` — `set_txclk (L125)`, `set_ana_scale (L126)`, `timer1_attachInterrupt (L425)`, `IRAM_ATTR (L131)`, `UART_FIFO (L100)`, `LittleFS (L5)`, `memw (L128)` |
| 41 | `B2_FM_LIVE_SERIAL_STEREO_120K_MPX8.ino` — `set_txclk (L128)`, `set_ana_scale (L129)`, `timer1_attachInterrupt (L430)`, `IRAM_ATTR (L134)`, `UART_FIFO (L103)`, `LittleFS (L5)`, `memw (L131)` |
| 42 | `B2_FM_LIVE_SERIAL_STEREO_120K_MPX8.ino` — `set_txclk (L128)`, `set_ana_scale (L129)`, `timer1_attachInterrupt (L430)`, `IRAM_ATTR (L134)`, `UART_FIFO (L103)`, `LittleFS (L5)`, `memw (L131)` |
| 43 | `FMStereoRX.ino` — `chip6_phy_init_ctrl (L37)`, `set_rf_freq_offset (L36)`, `IRAM_ATTR (L219)` |
| 44 | `FMFileModemRX.ino` — `chip6_phy_init_ctrl (L39)`, `set_rf_freq_offset (L38)`, `IRAM_ATTR (L221)` |
| 45 | `FMFileModemTX.ino` — `set_txclk (L130)`, `set_ana_scale (L131)`, `timer1_attachInterrupt (L432)`, `IRAM_ATTR (L136)`, `UART_FIFO (L105)`, `LittleFS (L7)`, `memw (L133)` |
| 46 | `SSBReceiverHQ.ino` — `chip6_phy_init_ctrl (L48)`, `set_rf_freq_offset (L47)`, `IRAM_ATTR (L209)` |
| 47 | `B2_SSB_LIVE_SERIAL_HIFI.ino` — `set_txclk (L134)`, `set_ana_scale (L135)`, `timer1_attachInterrupt (L410)`, `IRAM_ATTR (L140)`, `UART_FIFO (L109)`, `memw (L137)` |
| 48 | `radio.cpp` — `phy_param (L17)`, `RFChannelSel (L15)`, `phy_set_rf_freq_offset (L16)` · `B2_SSB_LIVE_SERIAL_HIFI_C5.ino` — `gptimer_new_timer (L205)`, `hal/uart_ll.h (L7)`, `IRAM_ATTR (L155)` |
| 49 | `B2_WFM_MONO_HIFI_48K_CONTROLE_MODULATION_APERCU.ino` — `set_txclk (L11315)`, `set_ana_scale (L11316)`, `chip6_phy_init_ctrl (L58)`, `set_rf_freq_offset (L59)`, `timer1_attachInterrupt (L14090)`, `IRAM_ATTR (L11498)`, `LittleFS (L18)`, `memw (L11374)`, `ESP8266WebServer (L50)` |
| 50 | `ESP8266_AM.ino` — `set_txclk (L11316)`, `set_ana_scale (L11317)`, `chip6_phy_init_ctrl (L59)`, `set_rf_freq_offset (L60)`, `timer1_attachInterrupt (L13830)`, `IRAM_ATTR (L11497)`, `LittleFS (L18)`, `memw (L11373)`, `ESP8266WebServer (L51)` |
| 51 | `ESP8266_SSB.ino` — `set_txclk (L11314)`, `set_ana_scale (L11315)`, `chip6_phy_init_ctrl (L57)`, `set_rf_freq_offset (L58)`, `timer1_attachInterrupt (L13767)`, `IRAM_ATTR (L11372)`, `LittleFS (L18)`, `memw (L11371)`, `ESP8266WebServer (L49)` |
| 52 | `CB_FM_STEREO.yaml` — `--wrap=setup (L65)`, `--wrap=loop (L66)`, `--wrap=millis (L71)`, `build_src_filter (L72)`, `waveform_stubs.cpp (L75)`, `lwip_glue_stubs.cpp (L76)`, `IRAM_ATTR (L200)`, `NONOSDK22x_190703 (L20)` |

## 6. Target 52 — why the YAML file needs its own toolchain

The legacy firmware in `CB-FMStereov1/CB_FM_STEREO.yaml` is C++ embedded in a multiline YAML value. The ESPHome generator turns it into compilable source. The first line of the included text, its indentation, the end marker, the `cbfm_native` namespace and the mangled C++ names all form part of the build recipe. Indenting the C++ so that it falls outside the string, or replacing the YAML file with the nearby v5 sketch, can produce a different program.

| Active build setting | Role in rebuilding this variant |
|---|---|
| ESPHome 2026.10.1 generator, PlatformIO backend | Generates `src/main.cpp` and `platformio.ini`, among other files. Keep both files from a successful build alongside the original YAML. They record the transformations that would otherwise be lost if a future generator changed how it includes the code. |
| `-Wl,--wrap=setup` and `-Wl,--wrap=loop` | Redirect the Arduino entry points to the aliases in the embedded code. The source names `_ZN11cbfm_native5setupEv` and `_ZN11cbfm_native4loopEv`. Each mangled name depends on the C++ signature and namespace. |
| `build_unflags` and `-Wl,--wrap=millis` | Remove the wrapper around `millis`. The firmware then gets its timing from the Arduino core. |
| `build_src_filter` | Limits the runtime sources compiled to `main.cpp`, `esphome/components/esp8266/waveform_stubs.cpp` and `esphome/components/esp8266/lwip_glue_stubs.cpp`. These are exact paths in the source tree generated by ESPHome. |
| `-DPIO_FRAMEWORK_ARDUINO_ESPRESSIF_SDK22x_190703` | Selects the NONOS SDK variant. Archive the PIO framework version and its libraries. PlatformIO needs its own copy even if Arduino is installed elsewhere on the PC. |
| Host Python and packages | This variant has a separate toolchain using **ESPHome 2026.10.1** and **PlatformIO Core 6.2.0**. **4.2.1** is the Espressif8266 platform version. Preserve the Python and PlatformIO packages from a working ESPHome installation if you also want to restore this variant. |

A comment in the YAML gives the hash `2cee9d862f07afe63d4515ed29175d15c0a3a199021cbafd4f7cc9ba6ef0fd68` for the embedded legacy sketch as compared by its author. The hash column in chapter 5 covers the **entire YAML file together with its path**. These checks apply to two different inputs.

## 7. What to archive

1. **The original sources and the working sources.** Keep the original ZIP, its SHA-256, the commit, the complete directories and a diff or separate commit for each change. Retain `LICENSE`, the notices in individual files, schematics, README files and references. Some README illustrations link to GitHub. Download the ones that document wiring or measurements, along with the context needed to interpret them. A saved copy of the ZIP remains available independently of the branch name.
2. **Arduino packages.** Copy the entire `Arduino-telechargements` folder and, after installation, `%LOCALAPPDATA%\Arduino15`. Keep the cores, cache, indexes, `boards.txt`, `platform.txt`, variants, libraries, scripts, partition tables, headers and linker scripts. The FQBN alone does not contain these files.
3. **Host tools and cross-compilation tools.** IDE 2.3.11 includes Arduino CLI **1.6.0**. The Arduino-ESP32 3.3.12 indexes list `esp-x32` and `esp-rv32` package revision `2601` (GCC 14.2.0, build 20260121), `esptool_py` 5.3.1 and the SDK packages for each SoC, among other dependencies. Check the exact names in your archived index. For ESP8266, keep the `xtensa-lx106-elf` toolchain and the utilities from core 3.1.2. Also keep the assembler, linker, `ar`, `nm`, `objdump`, `readelf`, `objcopy`, the CLI’s built-in tools and its preprocessors. Include the Windows executables, their DLLs and the system runtime dependencies. Two GCC distributions with the same major version can contain different patches.
4. **Optional ESPHome environment.** If you maintain the YAML, separately archive Python, the installable Python packages, their installed versions, PlatformIO, its tools, its framework and its downloaded dependencies. Keep a version list alongside the binary archives.
5. **Outputs from each build.** In **File → Preferences**, enable verbose output during compilation. After **Verify**, save the log and relevant files from the temporary directory it identifies before that directory is cleaned. Use **Sketch → Export Compiled Binary** to retain the images. Archive the ELF, linker map, resolved options and generated C++ when available. For ESPHome, keep `firmware.bin`, the ELF, the log and the generated project. Label each reference with its project, board and profile.
6. **Reference hardware and test environment.** Record the board model, module, revision, crystal, actual flash memory fitted, USB-to-UART bridge, driver versions and serial settings. If possible, keep one working unit from each family. Save the local HTML/JS/CSS pages, a browser with Web Serial and Web Audio support, test clips and previously captured UART frames. Build tools alone cannot reproduce the browser, the RDS encoder or the analog characteristics of the test bench.
7. **Board-specific configuration.** Document the partition layout actually installed, PHY initialization parameters and any device calibration data stored in flash or NVS. Record eFuse characteristics, per-module calibration, any keys the product uses and the silicon revision in the hardware record for that unit. Treat values from another unit as specific to that unit. If the product uses a signing chain, document the authorized procedure and store the keys separately under the applicable key-management procedure.
8. **Backup media.** Keep at least two readable copies on independent media, with one offline. Keep an exported image of the reference x64 machine and the tool required to restore it. Whenever you migrate to a new disk, regenerate the file manifest and run a real build from the copy. Checksums let you detect data loss. The second copy lets you recover the affected files.

### Create an integrity inventory with the software already installed

Close the BAT, then gather the files to preserve under a dedicated folder such as `C:\Radio`. Run the following commands in **Windows PowerShell 5.1**. Save the manifest alongside the folder you are indexing. The manifest lists files only and uses relative paths so you can move the folder to another drive.

```powershell
$racine = (Resolve-Path 'C:\Radio').Path.TrimEnd('\')
$manifest = 'C:\Radio-SHA256.csv'
Get-ChildItem -LiteralPath $racine -File -Recurse -Force |
    Sort-Object FullName |
    ForEach-Object {
        [pscustomobject]@{
            Chemin = $_.FullName.Substring($racine.Length + 1)
            Octets = $_.Length
            SHA256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
        }
    } | Export-Csv -LiteralPath $manifest -Encoding UTF8 -NoTypeInformation
```

After restoring the backup, update the root and manifest paths. Verify the files before opening the IDE or starting a new build.

```powershell
$racine = (Resolve-Path 'D:\Radio').Path
$manifest = 'D:\Radio-SHA256.csv'
$ecarts = @(foreach ($f in (Import-Csv -LiteralPath $manifest)) {
    $p = Join-Path $racine $f.Chemin
    if (-not (Test-Path -LiteralPath $p -PathType Leaf)) {
        "ABSENT  $($f.Chemin)"
    } elseif ((Get-Item -LiteralPath $p).Length -ne [long]$f.Octets) {
        "TAILLE  $($f.Chemin)"
    } elseif ((Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash -ne $f.SHA256) {
        "SHA256  $($f.Chemin)"
    }
})
$ecarts
if ($ecarts.Count) { throw "$($ecarts.Count) ecart(s) dans la copie." }
"Tous les fichiers inventoriés sont intacts."
```

This check covers every file in the manifest. To find files added to the storage medium since then, create a second inventory and compare the two path lists as well. Keep the original manifest on a different storage medium from the copy it verifies.

## 8. Reading the linker’s output ten years later

A linker map records what the linker resolved in one build. The ELF retains the sections and symbols needed for diagnosis. **Examples from the builds of this archive** show how to distinguish a symbol supplied by a library, data held in RAM and an address defined in ROM.

| Example observed in linker maps for this revision | Verified provider or placement | What to preserve or check |
|---|---|---|
| C5 and C6 PCM12, `phy_param` | `libphy.a(phy_init.o)` for the **corresponding SoC** | The symbol has the same name on both chips. Each archive has a different hash, and the linker places its data in the ELF for its own target. |
| C5 and C6 PCM12, `RFChannelSel` | `libphy.a(phy_api.o)` on both targets | A saved prototype cannot reconstruct the object code. |
| C5 PCM12, `phy_start_tx_tone_step` | `libphy.a(phy_reg.o)` | This dependency identifies a specific library linked into the image. |
| ESP32 Classic PCM12, `g_phyFuns` | Section `.bss.g_phyFuns` in `libphy.a(phy_chip_v7.o)` | This data object receives its value at runtime. Its address in the ELF cannot reproduce the code that initializes the table. |
| ESP32 Classic, `rom_set_rf_freq_offset` | Absolute definition `PROVIDE(...)` in the observed map | The ROM linker script must match the hardware revision. |
| ESP32-C6, `rom_chip_i2c_readReg` | Absolute value assigned by the linker script | Find the symbol in the archived SDK’s C6 linker script and check for the same definition in the map. |

**Trace a missing dependency.** In the failing target’s log, identify the first unresolved symbol. Open the reference linker map and search for that name. The *Archive member included to satisfy reference* section identifies the object that the linker selected. The memory map and cross-reference table show the section and the provider. The linker lists discarded sections separately. Compare the hash of the `.a` file, its archive members, the exact C/C++ symbol name, the configuration guards and the arguments passed to the linker.

Local example for the C5 PCM12 target after installing the two packages on Windows. These paths use the standard Boards Manager data directory.

```powershell
$kit = Join-Path $env:LOCALAPPDATA 'Arduino15'
$tool = Get-ChildItem "$kit\packages\esp32\tools" -Filter 'riscv32-esp-elf-nm.exe' -File -Recurse |
    Select-Object -First 1
$lib = Get-Item "$kit\packages\esp32\tools\esp32c5-libs\3.3.12\ld\libphy.a"
& $tool.FullName -A -g --defined-only $lib.FullName |
    Select-String 'RFChannelSel|phy_param|phy_start_tx_tone_step'
```

For Classic, use `xtensa-esp32-elf-nm.exe` and the `esp32-libs` package. For ESP8266, use `xtensa-lx106-elf-nm.exe` and the library from the exact NONOS subdirectory. In `nm` output, `U` marks a reference that the object under inspection leaves undefined. `A` indicates an absolute definition, `T` code and `B` uninitialized data. Inspect the actual sections with `readelf -h -S -s` and the relocations in an object file or archive with `readelf -r`. A symbol type letter, an address on its own and the spelling of a name each provide only part of the evidence.

Next, compare the reference and rebuilt ELF files with `objdump -h` and `objdump -d -C`, using the same target prefix. Start with routines marked for IRAM, calls made by interrupt service routines (ISRs), tables in DRAM, alignment, assembly routines and RAM boundaries. Check the flags passed to the compiler, including `-O2`, ABI and floating-point options, section garbage collection and any use of link-time optimization (LTO), **as recorded in that target’s log**. C and C++ must agree on symbol names and ABI at each `extern "C"` boundary.

### Rebuilding if Arduino CLI itself becomes unusable

First restore the archived executable and operating system. If only the compilers and SDKs still run, use the procedure below. Paths in the logs may point to temporary directories. Preserve the exact sketch name and the inputs to the reference build.

1. **Reconstruct the translation unit.** Take one `.ino`, its adjacent files and the `src/` tree from the correct directory. Arduino generated `<sketch>.ino.cpp` with its include directives, any required prototypes and line directives. This exported file gives you a verifiable starting point for compiling the same C++ translation unit without the Arduino preprocessor. If you subsequently change the sketch, regenerate the declarations or maintain an equivalent C++ source file.
2. **Recover the command for each language.** Read `compile_commands.json` and the verbose log. Remap only the old path prefixes to the restored directories. Use the C tool for `.c`, the C++ tool for `.cpp` and the assembly preprocessor for `.S`. Preserve the chip variant, macros, include settings, architecture options and optimization level of **each** translation unit. Earlier commands can use options that do not appear in the final command.
3. **Keep the response files that commands read through `@`.** For each of the three ESP32 chips, SDK package 3.3.12 has a `flags/` directory containing `S_flags`, `c_flags`, `cpp_flags`, `defines`, `includes`, `ld_flags`, `ld_libs` and `ld_scripts`. Also copy `include/`, `ld/`, `lib/`, `bin/`, the directories for each flash mode, `sdkconfig` and `versions.txt`. In the projects that use it, the build reads the local `build_opt.h` as a list of options. A file with a `.h` extension can therefore hold compiler options. The log shows exactly how the command used it.
4. **Rebuild the core object files and the selected Arduino libraries.** Read the recipes in `platform.txt`, including the pre-build hooks, compilation, archive creation, linking and image conversion. The project libraries are used in addition to the SDK’s precompiled archives. `compile_commands.json` records compilation commands. The verbose log also records linking and the steps that produce the binary image. Keep both.
5. **Link again with the original memory layout.** Reuse the link command, argument order, `--start-group/--end-group` groups and `-T` scripts from the original build. On ESP8266, keep generated scripts such as `local.eagle.flash.ld` and `local.eagle.app.v6.common.ld`. On C5/C6/Classic, `flags/ld_scripts` lists multiple ROM files, memory scripts and section scripts. A single `*.rom.ld` file describes only part of the address space.
6. **Reproduce image generation.** Recover the hooks, ELF-to-image converter, flash layout and boot components. Where the build produces them, also archive `flash_args`, `boot_app0.bin`, `partitions.csv`, `sdkconfig`, `build_opt.h` and `file_opts`. Take offsets, minimum silicon revision, flash mode and the placement of each segment from the target under examination. C5 and C6 can require different offsets from Classic.
7. **Verify what you recovered.** Compare the commands, sections, original libraries and critical symbols before comparing protocol captures or bench measurements. If the entire SDK is missing, this procedure identifies which component you need to recover and at which stage it becomes indispensable.

The following hashes of **symbol definition scripts** in package 3.3.12 were recomputed from the distributed files. Use them to identify an old backup. Keep the entire script directory and the order in which the linker loads the scripts.

| Chip | SHA-256 of `ld/<puce>.rom.ld` | SHA-256 of `ld/<puce>.rom.api.ld` |
|---|---|---|
| `esp32` | `0a5891603311f260fdf9732cea8e58b4aa6fb118b8b5732c44caf2068ad61ae8` | `e0438d826affdb8a0b62d0e8d448fa36c598647eff068b5f668e0ba9a2a055d4` |
| `esp32c5` | `53bcf1ce4a410e46af398d22f2059e2632fae6c708537815d6da819c284fb56f` | `1f1f8203425d23d508e0fae880d0b15fbf345e104b4278e34007af8d577c2b5f` |
| `esp32c6` | `e0a0d43e31deeeaca4b967a02fb40faaa2f099d9a2f8d322c4a02b5f6f36c7df` | `6c8bfc83dda3e441dc227bd81cd6fcf07d42371af4934b2061fd9994b825872b` |

### If the ecosystem has changed

| Situation in 2036 | Procedure | Success criterion |
|---|---|---|
| Websites or indexes are unavailable | Restore the IDE ZIP, download folder, sources and the installed copy of `Arduino15`. Select the target profile and click **Verify** offline. | Fresh binaries from local source and a saved build log. |
| The current version of Windows no longer runs an old tool | Restore the reference x64 machine image in a compatible hypervisor. Verify the kit against its SHA-256 hashes first. | The original toolchain produces the reference set of build artifacts again. |
| A future SDK no longer exports a symbol | First build with the archived package. Port the code in a separate copy. Identify the archive, object file and interface contract used by the original. | Document the equivalence of the provider and ABI. Repeat the hardware tests if the binary changes. |
| A private header or a structure type has changed | Compare the `sdkconfig.h` and HAL versions, the macros and the structure representations on the target. Check sizes, alignment, signatures, units and section attributes. | The interface the code was compiled against matches the implementation that provides it. |
| Only an old image survives | Archive that image and all available ELF files and maps. Use the versioned tools to identify its image type, chip model and partition layout. Look for the original archives in working copies and on backup media. | Document what you recovered. Rebuilding from source remains unresolved while essential dependencies are missing. |
| The same code compiles with a new SDK but behaves differently | Compare both sets of ELF files and maps, the effective configurations and the code generated for timing-sensitive loops. Use the saved input/output traces. | Compare protocol behavior, real-time deadlines and bench measurements separately. |

## 9. Troubleshooting while preserving the reference build

| Symptom | What to check first |
|---|---|
| `Missing FQBN`, `platform not installed`, or an attempt to reach a registry | Check the selected board, both URLs, the local indexes and the archives in `%LOCALAPPDATA%\Arduino15\staging\packages`. Confirm the installed versions in Boards Manager. For ESPHome, check its separate installation. |
| `multiple definition of setup/loop` | For `WIFIOnlyTest`, check that the build folder contains **exactly one** .ino file. For the YAML target, inspect the aliases, the `--wrap` options and the generated `build_src_filter`. |
| `undefined reference` to `c5_*`, `e32_*` or `cb_fm_stereo_run` | Check the local `src/` directory, the files next to the C5 AM/SSB sketches and whether the `.S` files were assembled. Identify the first expected object file missing from the linker command. |
| `undefined reference` to `phy_*`, `ram_*` or `rom_*` | Find the symbol’s provider in the reference linker map. Check the target chip of the library package, the checksum of `libphy.a`, the ROM linker scripts and the `CONFIG_*` conditions. |
| A `#error` requires a particular target, version, fixed clock or USB configuration | Read the exact condition, the resolved FQBN and the configuration file. The condition records a technical assumption. Removing it suppresses the diagnostic without establishing ABI compatibility. |
| `region ... overflowed`, an IRAM error or an application partition that is too small | Inspect the section sizes and the partition table. On the D1 mini, check `mmu=4816` and `eesz=4M1M` in particular. On the three standalone C5 targets, check the 8 MB profile used on the test bench. |
| The program starts, but the expected frames never appear on the serial port | Match the HTML folder to the firmware variant. Check UART0 versus CDC, baud rate, parity and stop bits. Compare the protocol identifier with the entry in chapter 5 before investigating the compiler. |
| The build succeeds, but the firmware drops samples or exhibits jitter | Check the actual clock rate, watchdog assumptions, interrupts, placement of routines and constants, assembly code and time spent servicing events. Compare against captures from hardware of the same revision. |
| Two binaries built from the same source have different SHA-256 hashes | First compare the build profile, exact tool versions, generated headers, paths, timestamps, sections and version metadata. A bit-for-bit match requires a reproducible build procedure. It is a stricter test than successful compilation or protocol compatibility. |
| The YAML build switches backend or can no longer find its stubs | Check the reference copies of `platformio.ini` and `main.cpp` and the version of the generator that produced them. Rebuild with the archived kit. Changes to indentation or generated names can change the program even when the embedded C++ looks identical. |

**Validate in stages.** (1) Verify the presence and identity of every build input. (2) Recompile and link. (3) Inspect the ELF, binary images and partitions. (4) Test the protocol using the corresponding data and browser pages. (5) Measure the hardware on a suitable bench under the same conditions as the reference test. Passing one stage does not explain a failure at another. When a project’s notes say “not tested on hardware,” retain that qualification in future validation records.

## 10. References to archive with the files

The links below identify the sources of the build rules and packages. For independent use in 2036, keep dated local copies of the documentation, datasheets, errata and licenses applicable to each board. The tables and reference values in this guide can be read offline.

- Original repository at the revision in the supplied archive. [ESP-AnalogRadio, commit 0cabfa13](https://github.com/kaboom748/ESP-AnalogRadio/tree/0cabfa13ae883506af4aad99d9d34442b978aca3)
- Rules for files alongside the sketch and recursive compilation of `src/`. [Arduino sketch specification](https://docs.arduino.cc/arduino-cli/sketch-specification/)
- IDE build engine and ESP8266 core. [Arduino IDE 2.3.11](https://github.com/arduino/arduino-ide/releases/tag/2.3.11) · [Arduino CLI 1.6.0](https://github.com/arduino/arduino-cli/releases/tag/v1.6.0) · [ESP8266 Arduino 3.1.2](https://github.com/esp8266/Arduino/releases/tag/3.1.2)
- ESP8266 core memory configuration and board options. [Cache/IRAM split](https://arduino-esp8266.readthedocs.io/en/3.1.2/mmu.html) · [Board and NONOS SDK options](https://arduino-esp8266.readthedocs.io/en/3.1.2/ideoptions.html)
- Arduino-ESP32 package, distributed libraries and board definitions. [Release 3.3.12](https://github.com/espressif/arduino-esp32/releases/tag/3.3.12) · [boards.txt at the same revision](https://github.com/espressif/arduino-esp32/blob/3.3.12/boards.txt)
- ESPHome and the backend used in this release. [ESPHome 2026.10.1](https://pypi.org/project/esphome/2026.10.1/) · [Generator source](https://github.com/esphome/esphome/tree/2026.10.1) · [PlatformIO Espressif8266 4.2.1](https://github.com/platformio/platform-espressif8266/tree/v4.2.1)
- Boot, calibration and the role of flash on the ESP32 Classic. [IDF 5.5 bootloader](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/api-guides/bootloader.html) · [IDF 5.5.5 RF calibration](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32/api-guides/RF_calibration.html)
- Reading the linker map and identifying sources of variation between binaries. [IDF 5.5.5 linker map](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32/api-guides/performance/size.html) · [IDF 5.5.5 reproducible builds](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32/api-guides/reproducible-builds.html)

## Appendix — Reference binaries for the 51 Arduino builds

These hashes identify the application images built on Linux with Arduino CLI 1.4.1 and the original sketch names. The numbers correspond to chapter 5. Build dates, paths and tool versions can change the binary produced by a valid build. Also retain hashes for the binaries produced on your own Windows PC.

| No. | Family | Application image, bytes | Linker map, bytes | SHA-256 of the Linux image |
|---:|---|---:|---:|---|
| 01 | ESP8266 | 276208 | 2312837 | `28851bf4372e6c0c5e37d967a16b135928434615ade2527325f42a7903defef5` |
| 02 | C5 | 833456 | 12879596 | `cbf5d9e4678bae0b8151ebf8eb495e8d139d99bea553c9d7aa403b130d5de358` |
| 03 | ESP8266 | 276192 | 2323030 | `29d5f42b73e57aadec8f81a5cae66e66cf3f553fa12e7a9b3af5695b35a189e1` |
| 04 | C5 | 814000 | 12845634 | `95efdb1f1da3aef827006d064018be0bc9cdbab08b3604159e6a7963b8c0169a` |
| 05 | ESP8266 | 279840 | 2337933 | `1631822856bde0cf4f26809dc8df32418c08bf390a24a7722a55df7e3e581d49` |
| 06 | ESP8266 | 279840 | 2338797 | `1631822856bde0cf4f26809dc8df32418c08bf390a24a7722a55df7e3e581d49` |
| 07 | ESP8266 | 279840 | 2338797 | `1631822856bde0cf4f26809dc8df32418c08bf390a24a7722a55df7e3e581d49` |
| 08 | ESP8266 | 279840 | 2338797 | `17ef4f2289055a19f805ce75aeea5103b645405f81600c351fe871a4c2800d42` |
| 09 | C5 | 832048 | 12872994 | `04b2a05e1c9d6798f85fb0bbefd168025a65e44be141074e88cbf2996f00410d` |
| 10 | C6 | 791536 | 12227753 | `0dd235cb17eb9dec5a2e9ad4500b5abc1b9d1fdf56a13f3d09b56b008dcd240e` |
| 11 | ESP32 | 728448 | 13278475 | `006e6972207bdb139e6b1fdb05de7c84b1882afdbfb3ce2a043fb007bb5e6882` |
| 12 | C5 | 854224 | 12865570 | `ab43454037936f169aad245ec2c91ebebd2159d691e25232b34524885d7e07e3` |
| 13 | C5 | 834848 | 12865509 | `89f1b1738f77197e6789a71bab82e790dc81c24dadd66c1887cc2ee661199f12` |
| 14 | C6 | 791680 | 12247538 | `b1c83e2707d04f9a150bc20802861235d022253656446e32f1fdc04e513f4d14` |
| 15 | ESP32 | 728608 | 13374804 | `7dd6271998358f46968722f08cc7a80031dd6884ed48d97843c04f7c6224518d` |
| 16 | ESP8266 | 279952 | 2339213 | `5d8fab7be9bbda65ab2b4c81c60ea4fe32973e2b277db2b3af7e4e94e78fffba` |
| 17 | ESP8266 | 285824 | 2244081 | `b629c38814304a5c62214f5e47d74b427e9b9577fdc7555b8de7fb765cfa1b8c` |
| 18 | ESP8266 | 286128 | 2349966 | `2ab99fdab125db617888ec81b9cca1672cdccb43ade654eed35903efb017fcad` |
| 19 | C6 | 836256 | 12324000 | `6da44424f6a1f2485c1ab83e251f8c5d704583270c7b7596e7636c54cddf89b8` |
| 20 | C6 | 836384 | 12324039 | `74e2b02fae5020a5c2ae10b93088919cbbf639f146360b7207aa2d5cb4207a74` |
| 21 | ESP32 | 770944 | 13463945 | `d9ec12ddc3d8d7d0f79699b38afed12a6ee2638b3ec868c4f9e9a56ee75bc0c2` |
| 22 | C5 | 876016 | 12939249 | `3109a4588cbd38a7af829716241754c61f1044a9de6d8a65113685f74e67ca2a` |
| 23 | C5 | 876112 | 12939249 | `5f218ebb78ed7ac566decedcbd6e762229d43847c5064dbe36820795c4f88b64` |
| 24 | ESP32 | 771200 | 13464231 | `6b24c2fa0a37289b52038d22756f83e0c73e97776d31343bc8ed6f387d3b17b8` |
| 25 | C5 | 876192 | 12939249 | `fff407389670aaab97d9f22179d6abe6f274825751ffe20382398760a9706e34` |
| 26 | C5 | 909968 | 12938157 | `d92a934a58c238df7f57feac86deb4511400ba4540fc9d08cd6653624643664a` |
| 27 | C6 | 836336 | 12324039 | `00876f92598ce853e08acf7e07e67bafbe2e00679963a458aac205638f72ea8e` |
| 28 | ESP32 | 771216 | 13464153 | `5703a2a5fc91003444522d2c614d962745c6d911470541f1e79508e78ebfcd33` |
| 29 | ESP8266 | 286160 | 2349988 | `9a32ff799d6ffa904e112605fb6ed6b9e6a1a22aaac96a9b549087b69dc5f30a` |
| 30 | ESP8266 | 284128 | 2346695 | `1b6e98bbe3c92d78ec46ddd0b8f9874a876512cf35e4e7b62f1bafb5e4b406eb` |
| 31 | ESP8266 | 280928 | 2332851 | `ac2b850d4e4572744a4bd6e0d29403ff899001b3693f56334770063238f9ebfb` |
| 32 | ESP8266 | 280928 | 2332851 | `ac2b850d4e4572744a4bd6e0d29403ff899001b3693f56334770063238f9ebfb` |
| 33 | ESP8266 | 280896 | 2332710 | `469b55415429112d94b6e62e8a0cab97bdf75e162ccd8838e7f65b385abb4f6b` |
| 34 | C5 | 822432 | 12851252 | `b09fe89e1f5b7dec79cea12d2aa827d4f84f84df4f556d85e78121f70906c73b` |
| 35 | C5 | 822720 | 12838168 | `fe351cf425907056ade7d78756dc9a15912f079f4eb26f1758af1334a5a382d3` |
| 36 | C6 | 781088 | 12230561 | `53676ff3a676ccc76366c5e55ee8fbf1d5cd2f2fc801f0a9e26cea1fd67f6592` |
| 37 | ESP32 | 719408 | 13361252 | `adc4fac775aee491057bfc94d079115b85f4e7fc81d0eb443c991cb1e4b0921f` |
| 38 | ESP8266 | 281296 | 2333170 | `7b1f8705589ee776130981e64ffb4211596a9e8e99cba262fb604127623cee31` |
| 39 | ESP8266 | 276432 | 2326739 | `12aa10cccc1bc0f1fcbdf3ea4c344a921d088a106ad749f4a5f8fee2886e6594` |
| 40 | ESP8266 | 276432 | 2323400 | `3c21a3d65a3c2d7d1a8aad770a7d5c9091ddf278d883621ab73d15953323022f` |
| 41 | ESP8266 | 276480 | 2323275 | `a1dd909342ce2c99297d135cf26172cf857cebffc2dd67402517fb771168497f` |
| 42 | ESP8266 | 276480 | 2323275 | `a1dd909342ce2c99297d135cf26172cf857cebffc2dd67402517fb771168497f` |
| 43 | ESP8266 | 276448 | 2321546 | `f6e2cce2e429d58819b0bc5601c5ae613ce032eea33fd2f62a5f1c0270cd678a` |
| 44 | ESP8266 | 276448 | 2321678 | `f6e2cce2e429d58819b0bc5601c5ae613ce032eea33fd2f62a5f1c0270cd678a` |
| 45 | ESP8266 | 276480 | 2213923 | `a1dd909342ce2c99297d135cf26172cf857cebffc2dd67402517fb771168497f` |
| 46 | ESP8266 | 276224 | 2312905 | `3c97964662a826ccdf0166bdfc2a5ce68c5fd5db063946242d9cec2e5e12b989` |
| 47 | ESP8266 | 276592 | 2325574 | `71a82cfc32f5c05ad912484954b94a343fc56d45731fd2d75d0a68ba4c0fc0a9` |
| 48 | C5 | 823664 | 12956477 | `d17e2959c27d555cce65f4fd4a7d54edfb7721ceea01fde9de5f6e4381ba2c71` |
| 49 | ESP8266 | 989296 | 3210204 | `f1dbacc7573b41c3acdb04640b22bcf77cb2e93b1486e0a62d1af93ec35567f1` |
| 50 | ESP8266 | 979264 | 3167915 | `5d5b055f5d1e45031e88b1f9a168461e0b960a8d75ed76354175c7a68af73acc` |
| 51 | ESP8266 | 976272 | 3167184 | `a81c358d67f7ea543f20607ef723967186413b123716c1790cf8bd8c6685a64d` |
