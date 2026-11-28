# ESP-AnalogRadio — Guide de compilation pour Windows

**Archive de référence** · `0cabfa13ae883506af4aad99d9d34442b978aca3` · Windows 10/11 Intel ou AMD 64 bits · 51 sketches Arduino et 1 variante ESPHome.

**Pour compiler les 51 sketches dans Arduino IDE, il y a deux paquets à sélectionner dans le gestionnaire de cartes.** Installe **esp8266 3.1.2** pour le D1 mini et **esp32 3.3.12** pour les trois modèles ESP32. Les bibliothèques ESP-IDF 5.5.5 pour le Classic, le C5 et le C6 sont livrées par le paquet esp32. Le D1 mini utilise le SDK NONOS de son propre paquet. Le chapitre 1 explique l’installation et donne les réglages GUI de chaque projet.

## 1. Télécharger, installer, puis choisir les réglages dans Arduino IDE

### Les versions et leurs liens directs

| Élément | Version et rôle | Téléchargement officiel |
|---|---|---|
| Arduino IDE pour Windows x64 | **2.3.11**, interface choisie pour ce guide. Elle inclut Arduino CLI **1.6.0**. | [ZIP Windows x64](https://github.com/arduino/arduino-ide/releases/download/2.3.11/arduino-ide_2.3.11_Windows_64bit.zip) |
| Paquet de cartes **esp8266** | **3.1.2**, pour ESP8266. Il inclut notamment les bibliothèques `ESP8266WiFi`, `ESP8266WebServer` et `LittleFS`. | [Index du gestionnaire](https://arduino.esp8266.com/stable/package_esp8266com_index.json) · [Archive du cœur](https://github.com/esp8266/Arduino/releases/download/3.1.2/esp8266-3.1.2.zip) |
| Paquet de cartes **esp32** | **3.3.12**, basé sur **ESP-IDF 5.5.5**. Une installation couvre ESP32 Classic, ESP32-C5 et ESP32-C6. | [Index du gestionnaire](https://espressif.github.io/arduino-esp32/package_esp32_index.json) · [Archive du cœur](https://github.com/espressif/arduino-esp32/releases/download/3.3.12/esp32-core-3.3.12.zip) |
| Bibliothèques précompilées Espressif | Les trois archives utilisées par les projets ESP32 de ce dépôt. Le gestionnaire se charge de leur mise en place. | [Classic](https://github.com/espressif/arduino-esp32/releases/download/3.3.12/esp32-libs-3.3.12.zip) · [C5](https://github.com/espressif/arduino-esp32/releases/download/3.3.12/esp32c5-libs-3.3.12.zip) · [C6](https://github.com/espressif/arduino-esp32/releases/download/3.3.12/esp32c6-libs-3.3.12.zip) |

Les deux archives de cœurs, prises isolément, ne contiennent pas tous les compilateurs Windows. Leurs index listent aussi les outils, les bibliothèques par puce et leurs empreintes. Pour installer officiellement esp32 3.3.12, le gestionnaire réclame l’ensemble des dépendances déclarées du paquet, y compris certaines archives pour d’autres variantes ESP32.

Le téléchargement utilise Windows PowerShell 5.1 ainsi que `curl.exe` et `tar.exe` fournis avec les versions récentes de Windows 10/11. Compte environ **2,33 Go pour les archives** de l’IDE et des paquets Windows, plus les index, puis plusieurs gigaoctets supplémentaires pour les paquets décompressés. Après succès, `SHA256SUMS.txt` se trouve dans le dossier de téléchargement.

### Premier téléchargement et utilisation hors ligne

1. Place `ESP-AnalogRadio.bat` dans un répertoire accessible en écriture, par exemple `C:\Radio`, puis double-clique **avec Internet**. Le BAT télécharge les archives Windows et les index dans `Arduino-telechargements`, à côté de lui. Conserve ce dossier et le ZIP de ton dépôt. Tu peux lancer cette étape sur un autre PC que celui qui compilera.
2. Sur le PC de compilation, **ferme Arduino IDE**. Décompresse le ZIP de l’IDE dans un dossier court. Pour préparer une installation hors ligne sur Windows, ouvre `%LOCALAPPDATA%` dans l’Explorateur et copie le dossier `Arduino15` fourni sous `Arduino-telechargements` dans cet emplacement. Le résultat doit être `%LOCALAPPDATA%\Arduino15\staging\packages`. Si tu as déjà une installation, sauvegarde d’abord ton dossier `Arduino15`, puis fusionne son contenu. Les archives des paquets restent compressées dans `staging\packages`.
3. Lance **Arduino IDE**. Sous **Fichier → Préférences → URL de gestionnaire de cartes supplémentaires**, ajoute chacune de ces deux lignes. Ces noms servent aussi à l’IDE pour reconnaître les index locaux.

```text
https://arduino.esp8266.com/stable/package_esp8266com_index.json
https://espressif.github.io/arduino-esp32/package_esp32_index.json
```

4. Dans le **Gestionnaire de cartes** de la barre latérale, cherche `esp8266` et installe **esp8266 by ESP8266 Community → 3.1.2**. Cherche ensuite `esp32` et installe **esp32 by Espressif Systems → 3.3.12**. Choisis la version dans le sélecteur avant de cliquer sur **Installer**. Le gestionnaire décompresse les archives en cache dans `%LOCALAPPDATA%\Arduino15\packages`. Une actualisation automatique de l’index peut afficher une erreur réseau sur un poste déconnecté.
5. Si l’interface ne propose pas les versions sur un premier démarrage hors ligne, utilise le moteur inclus dans le ZIP de l’IDE. Depuis un terminal PowerShell **ouvert à la racine de l’IDE décompressé**, avec le cache déjà copié et l’IDE fermé, exécute les commandes ci-dessous. Elles réalisent l’installation à partir du cache sur le poste de compilation. Rouvre ensuite l’IDE.

```powershell
$cli = (Get-ChildItem -Path . -Filter arduino-cli.exe -File -Recurse |
    Select-Object -First 1).FullName
$urls = 'https://arduino.esp8266.com/stable/package_esp8266com_index.json,https://espressif.github.io/arduino-esp32/package_esp32_index.json'
& $cli core install 'esp8266:esp8266@3.1.2' --additional-urls $urls
if ($LASTEXITCODE -ne 0) { throw 'esp8266:esp8266@3.1.2' }
& $cli core install 'esp32:esp32@3.3.12' --additional-urls $urls
if ($LASTEXITCODE -ne 0) { throw 'esp32:esp32@3.3.12' }
```

6. Décompresse le dépôt. Pour chaque cible, place le **seul sketch principal** dans un dossier qui porte son nom sans `.ino`. Copie à côté les fichiers sources et en-têtes de **sa** variante, notamment les `.cpp` adjacents, `build_opt.h` et l’arbre `src/` lorsqu’ils existent. Dans `WIFIOnlyTest`, crée trois dossiers indépendants. Ouvre le sketch, choisis sa carte dans **Outils → Type de carte**, applique sa ligne du tableau puis clique sur **Vérifier** (✓). Cette compilation fonctionne sans carte connectée.
7. Pour la conservation à long terme, copie sur un second support le dossier téléchargé, le dépôt, ces guides et, après une compilation réussie hors ligne, le dossier **Arduino15 installé**. Une sauvegarde de machine virtuelle Windows x64 qui a reproduit les builds permet aussi de conserver l’environnement hôte.

### Réglages GUI par projet

Chaque ligne correspond à **un chemin précis du dépôt**. Ce sont les valeurs des profils utilisés pour compiler ces 51 sketches, vérifiées dans les fichiers `boards.txt` des paquets 3.1.2 et 3.3.12. Les libellés de choix reprennent le texte anglais du paquet. Dans le menu, `CPU Frequency` est la cadence du processeur. `Flash Frequency` est la cadence de la flash. `Upload Speed` règle le téléversement et n’intervient pas dans **Vérifier**.

- **ESP8266** · Dans `MMU`, sélectionne **16KB cache + 48KB IRAM (IRAM)**. Sous `Flash Size`, choisis **4MB (FS:1MB OTA:~1019KB)**. DIO et 40 MHz sont fixés par la définition D1 mini. Les menus d’USB CDC et de PSRAM ne s’appliquent pas.
- **ESP32 Classic, C5 et C6** · Ces définitions de carte n’exposent pas de menu MMU pour choisir la quantité d’IRAM. La colonne MMU affiche donc un tiret. Sur Classic et C5, mets `PSRAM` sur `Disabled`. Sur C5 et C6, mets `USB CDC On Boot` sur `Disabled`. Pour le Classic, garde `Arduino Runs On` et `Events Run On` sur `Core 1`. Sur toutes les cartes, garde les autres options aux valeurs par défaut de la version indiquée.
- **Trois C5 à 8 Mo** · Les projets 02, 04 et 48 ont été documentés sur une carte de 8 Mo. La table de partitions standard à 8 Mo ci-dessous est celle utilisée pour les compilations de référence. Vérifie la capacité physique du module avant tout téléversement. Les neuf autres projets C5 utilisent le profil 4 Mo.
- **Cible 52** · Le fichier YAML est une enveloppe ESPHome distincte. Sa ligne indique les valeurs actives du YAML. Sa chaîne de construction séparée est documentée au chapitre 6.

| Nº | Projet / sketch dans le dépôt | Outils → Type de carte | CPU Frequency | MMU / IRAM | Flash Size · Partition Scheme | Flash Mode · Flash Frequency |
|---:|---|---|---|---|---|---|
| 01 | `AM-RX-YEAHHHHHv1/AMReceiverHQ.ino` | `LOLIN(WEMOS) D1 R2 & mini` | `160 MHz` | `16KB cache + 48KB IRAM (IRAM)` | `4MB (FS:1MB OTA:~1019KB)` | `DIO` · `40 MHz` (fixés) |
| 02 | `AM-RX-YEAHHHHHv1/ESP32-C5v1/AMReceiverHQ_C5/AMReceiverHQ_C5.ino` | `ESP32C5 Dev Module` | `240MHz (WiFi)` | — | `8MB (64Mb)` · `8M with spiffs (3MB APP/1.5MB SPIFFS)` | `DIO` · `80MHz` |
| 03 | `AMSerialHQv1/B2_AM_LIVE_SERIAL_HIFI_OV2.ino` | `LOLIN(WEMOS) D1 R2 & mini` | `160 MHz` | `16KB cache + 48KB IRAM (IRAM)` | `4MB (FS:1MB OTA:~1019KB)` | `DIO` · `40 MHz` (fixés) |
| 04 | `AMSerialHQv1/ESP32-C5v1/AMSerialHQv1_C5/AMSerialHQv1_C5.ino` | `ESP32C5 Dev Module` | `240MHz (WiFi)` | — | `8MB (64Mb)` · `8M with spiffs (3MB APP/1.5MB SPIFFS)` | `DIO` · `80MHz` |
| 05 | `CB-FMStereov1/CB_FM_STEREOv5.ino` | `LOLIN(WEMOS) D1 R2 & mini` | `160 MHz` | `16KB cache + 48KB IRAM (IRAM)` | `4MB (FS:1MB OTA:~1019KB)` | `DIO` · `40 MHz` (fixés) |
| 06 | `CB-FMStereov1/CB_V10/CB_FM_STEREOv10_Arduino/CB_FM_STEREOv10_Arduino.ino` | `LOLIN(WEMOS) D1 R2 & mini` | `160 MHz` | `16KB cache + 48KB IRAM (IRAM)` | `4MB (FS:1MB OTA:~1019KB)` | `DIO` · `40 MHz` (fixés) |
| 07 | `CB-FMStereov1/CB_V12/CB_FM_STEREOv12_Arduino/CB_FM_STEREOv12_Arduino.ino` | `LOLIN(WEMOS) D1 R2 & mini` | `160 MHz` | `16KB cache + 48KB IRAM (IRAM)` | `4MB (FS:1MB OTA:~1019KB)` | `DIO` · `40 MHz` (fixés) |
| 08 | `CB-FMStereov1/CB_V13/CB_FM_STEREOv13_Arduino/CB_FM_STEREOv13_Arduino.ino` | `LOLIN(WEMOS) D1 R2 & mini` | `160 MHz` | `16KB cache + 48KB IRAM (IRAM)` | `4MB (FS:1MB OTA:~1019KB)` | `DIO` · `40 MHz` (fixés) |
| 09 | `CB-FMStereov1/CB_V13/CB_FM_STEREOv13_C5/CB_FM_STEREOv13_C5.ino` | `ESP32C5 Dev Module` | `240MHz (WiFi)` | — | `4MB (32Mb)` · `Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)` | `DIO` · `80MHz` |
| 10 | `CB-FMStereov1/CB_V13/CB_FM_STEREOv13_C6/CB_FM_STEREOv13_C6.ino` | `ESP32C6 Dev Module` | `160MHz (WiFi)` | — | `4MB (32Mb)` · `Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)` | `QIO` · `80MHz` |
| 11 | `CB-FMStereov1/CB_V13/CB_FM_STEREOv13_ESP32/CB_FM_STEREOv13_ESP32.ino` | `ESP32 Dev Module` | `240MHz (WiFi/BT)` | — | `4MB (32Mb)` · `Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)` | `DIO` · `40MHz` |
| 12 | `CB-FMStereov1/CB_V13_5GHZ_C5_BETA/CB_FM_STEREOv16_C5/CB_FM_STEREOv16_C5.ino` | `ESP32C5 Dev Module` | `240MHz (WiFi)` | — | `4MB (32Mb)` · `Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)` | `DIO` · `80MHz` |
| 13 | `CB-FMStereov1/CB_V14/CB_FM_STEREOv13_C5/CB_FM_STEREOv13_C5.ino` | `ESP32C5 Dev Module` | `240MHz (WiFi)` | — | `4MB (32Mb)` · `Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)` | `DIO` · `80MHz` |
| 14 | `CB-FMStereov1/CB_V14/CB_FM_STEREOv13_C6/CB_FM_STEREOv13_C6.ino` | `ESP32C6 Dev Module` | `160MHz (WiFi)` | — | `4MB (32Mb)` · `Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)` | `QIO` · `80MHz` |
| 15 | `CB-FMStereov1/CB_V14/CB_FM_STEREOv13_ESP32/CB_FM_STEREOv13_ESP32.ino` | `ESP32 Dev Module` | `240MHz (WiFi/BT)` | — | `4MB (32Mb)` · `Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)` | `DIO` · `40MHz` |
| 16 | `CB-FMStereov1/CB_V14/CB_FM_STEREOv13_ESP8266/CB_FM_STEREOv13_ESP8266.ino` | `LOLIN(WEMOS) D1 R2 & mini` | `160 MHz` | `16KB cache + 48KB IRAM (IRAM)` | `4MB (FS:1MB OTA:~1019KB)` | `DIO` · `40 MHz` (fixés) |
| 17 | `CB-FMStereov1/CB_V15_PCM12/CB_FM_STEREO_MPXRX_PCM12TX_ESP8266/CB_FM_STEREO_MPXRX_PCM12TX_ESP8266.ino` | `LOLIN(WEMOS) D1 R2 & mini` | `160 MHz` | `16KB cache + 48KB IRAM (IRAM)` | `4MB (FS:1MB OTA:~1019KB)` | `DIO` · `40 MHz` (fixés) |
| 18 | `CB-FMStereov1/CB_V15_PCM12/CB_FM_STEREO_MPXRX_PCM12TX_ESP8266v2/CB_FM_STEREO_MPXRX_PCM12TX_ESP8266.ino` | `LOLIN(WEMOS) D1 R2 & mini` | `160 MHz` | `16KB cache + 48KB IRAM (IRAM)` | `4MB (FS:1MB OTA:~1019KB)` | `DIO` · `40 MHz` (fixés) |
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
| 29 | `CB-FMStereov1/CB_V15_PCM12/TESTONLY-ESP8266-DUALCARRIER/CB_FM_STEREO_MPXRX_PCM12TX_ESP8266.ino` | `LOLIN(WEMOS) D1 R2 & mini` | `160 MHz` | `16KB cache + 48KB IRAM (IRAM)` | `4MB (FS:1MB OTA:~1019KB)` | `DIO` · `40 MHz` (fixés) |
| 30 | `CB-FMStereov1/CB_V15_PCM12/TESTONLY-ESP8266-DUALCARRIER/TEST_2_INDEP_CARRIER_MONO/CB_FM_STEREO_MPXRX_PCM12TX_ESP8266.ino` | `LOLIN(WEMOS) D1 R2 & mini` | `160 MHz` | `16KB cache + 48KB IRAM (IRAM)` | `4MB (FS:1MB OTA:~1019KB)` | `DIO` · `40 MHz` (fixés) |
| 31 | `CB_DuplexVoice_V3/CB_V4/CB_DuplexVoice_V4_Arduino/CB_DuplexVoice_V4_Arduino.ino` | `LOLIN(WEMOS) D1 R2 & mini` | `160 MHz` | `16KB cache + 48KB IRAM (IRAM)` | `4MB (FS:1MB OTA:~1019KB)` | `DIO` · `40 MHz` (fixés) |
| 32 | `CB_DuplexVoice_V3/CB_V5/CB_DuplexVoice_V4_Arduino/CB_DuplexVoice_V4_Arduino.ino` | `LOLIN(WEMOS) D1 R2 & mini` | `160 MHz` | `16KB cache + 48KB IRAM (IRAM)` | `4MB (FS:1MB OTA:~1019KB)` | `DIO` · `40 MHz` (fixés) |
| 33 | `CB_DuplexVoice_V3/CB_V6/CB_DuplexVoice_V6_Arduino/CB_DuplexVoice_V6_Arduino.ino` | `LOLIN(WEMOS) D1 R2 & mini` | `160 MHz` | `16KB cache + 48KB IRAM (IRAM)` | `4MB (FS:1MB OTA:~1019KB)` | `DIO` · `40 MHz` (fixés) |
| 34 | `CB_DuplexVoice_V3/CB_V6/CB_DuplexVoice_V6_C5/CB_DuplexVoice_V6_C5.ino` | `ESP32C5 Dev Module` | `240MHz (WiFi)` | — | `4MB (32Mb)` · `Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)` | `DIO` · `80MHz` |
| 35 | `CB_DuplexVoice_V3/CB_V7beta/CB_DuplexVoice_V6_C5/CB_DuplexVoice_V6_C5.ino` | `ESP32C5 Dev Module` | `240MHz (WiFi)` | — | `4MB (32Mb)` · `Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)` | `DIO` · `80MHz` |
| 36 | `CB_DuplexVoice_V3/CB_V7beta/CB_DuplexVoice_V6_C6/CB_DuplexVoice_V6_C6.ino` | `ESP32C6 Dev Module` | `160MHz (WiFi)` | — | `4MB (32Mb)` · `Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)` | `QIO` · `80MHz` |
| 37 | `CB_DuplexVoice_V3/CB_V7beta/CB_DuplexVoice_V6_ESP32/CB_DuplexVoice_V6_ESP32.ino` | `ESP32 Dev Module` | `240MHz (WiFi/BT)` | — | `4MB (32Mb)` · `Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)` | `DIO` · `40MHz` |
| 38 | `CB_DuplexVoice_V3/CB_V7beta/CB_DuplexVoice_V6_ESP8266/CB_DuplexVoice_V6_ESP8266.ino` | `LOLIN(WEMOS) D1 R2 & mini` | `160 MHz` | `16KB cache + 48KB IRAM (IRAM)` | `4MB (FS:1MB OTA:~1019KB)` | `DIO` · `40 MHz` (fixés) |
| 39 | `FMSerialHQv1/B2_FM_LIVE_SERIAL_48K_WFM_B2_K4_TUNE1450_UART_FLOW8K_2000000.ino` | `LOLIN(WEMOS) D1 R2 & mini` | `160 MHz` | `16KB cache + 48KB IRAM (IRAM)` | `4MB (FS:1MB OTA:~1019KB)` | `DIO` · `40 MHz` (fixés) |
| 40 | `FMStereoHQv1/B2_FM_LIVE_SERIAL_STEREO_120K_MPX8.ino` | `LOLIN(WEMOS) D1 R2 & mini` | `160 MHz` | `16KB cache + 48KB IRAM (IRAM)` | `4MB (FS:1MB OTA:~1019KB)` | `DIO` · `40 MHz` (fixés) |
| 41 | `FMStereoHQv2/B2_FM_LIVE_SERIAL_STEREO_120K_MPX8.ino` | `LOLIN(WEMOS) D1 R2 & mini` | `160 MHz` | `16KB cache + 48KB IRAM (IRAM)` | `4MB (FS:1MB OTA:~1019KB)` | `DIO` · `40 MHz` (fixés) |
| 42 | `FMStereoHQv3/B2_FM_LIVE_SERIAL_STEREO_120K_MPX8.ino` | `LOLIN(WEMOS) D1 R2 & mini` | `160 MHz` | `16KB cache + 48KB IRAM (IRAM)` | `4MB (FS:1MB OTA:~1019KB)` | `DIO` · `40 MHz` (fixés) |
| 43 | `FMStereoRX-YEAHHHHHv1/FMStereoRX.ino` | `LOLIN(WEMOS) D1 R2 & mini` | `160 MHz` | `16KB cache + 48KB IRAM (IRAM)` | `4MB (FS:1MB OTA:~1019KB)` | `DIO` · `40 MHz` (fixés) |
| 44 | `MODEM-SimplexV1/FMFileModemRX/FMFileModemRX.ino` | `LOLIN(WEMOS) D1 R2 & mini` | `160 MHz` | `16KB cache + 48KB IRAM (IRAM)` | `4MB (FS:1MB OTA:~1019KB)` | `DIO` · `40 MHz` (fixés) |
| 45 | `MODEM-SimplexV1/FMFileModemTX/FMFileModemTX.ino` | `LOLIN(WEMOS) D1 R2 & mini` | `160 MHz` | `16KB cache + 48KB IRAM (IRAM)` | `4MB (FS:1MB OTA:~1019KB)` | `DIO` · `40 MHz` (fixés) |
| 46 | `SSB-RX-YEAHHHHHv1/SSBReceiverHQ.ino` | `LOLIN(WEMOS) D1 R2 & mini` | `160 MHz` | `16KB cache + 48KB IRAM (IRAM)` | `4MB (FS:1MB OTA:~1019KB)` | `DIO` · `40 MHz` (fixés) |
| 47 | `SSBTestv1/B2_SSB_LIVE_SERIAL_HIFI.ino` | `LOLIN(WEMOS) D1 R2 & mini` | `160 MHz` | `16KB cache + 48KB IRAM (IRAM)` | `4MB (FS:1MB OTA:~1019KB)` | `DIO` · `40 MHz` (fixés) |
| 48 | `SSBTestv1/ESP32-C5v1/B2_SSB_LIVE_SERIAL_HIFI_C5/B2_SSB_LIVE_SERIAL_HIFI_C5.ino` | `ESP32C5 Dev Module` | `240MHz (WiFi)` | — | `8MB (64Mb)` · `8M with spiffs (3MB APP/1.5MB SPIFFS)` | `DIO` · `80MHz` |
| 49 | `WIFIOnlyTest/B2_WFM_MONO_HIFI_48K_CONTROLE_MODULATION_APERCU.ino` | `LOLIN(WEMOS) D1 R2 & mini` | `160 MHz` | `16KB cache + 48KB IRAM (IRAM)` | `4MB (FS:1MB OTA:~1019KB)` | `DIO` · `40 MHz` (fixés) |
| 50 | `WIFIOnlyTest/ESP8266_AM.ino` | `LOLIN(WEMOS) D1 R2 & mini` | `160 MHz` | `16KB cache + 48KB IRAM (IRAM)` | `4MB (FS:1MB OTA:~1019KB)` | `DIO` · `40 MHz` (fixés) |
| 51 | `WIFIOnlyTest/ESP8266_SSB.ino` | `LOLIN(WEMOS) D1 R2 & mini` | `160 MHz` | `16KB cache + 48KB IRAM (IRAM)` | `4MB (FS:1MB OTA:~1019KB)` | `DIO` · `40 MHz` (fixés) |
| 52 | `CB-FMStereov1/CB_FM_STEREO.yaml` | ESPHome · `d1_mini` | `160000000L` | — | Configuration YAML / PlatformIO | `dout` (YAML) |

**Dossiers à préparer dans cette archive.** Les 26 entrées suivantes sont fournies dans un dossier dont le nom diffère de celui du sketch principal. Applique l’étape 6 aux cibles **01, 03, 05, 18–30, 39–43, 46, 47 et 49–51**. Les options locales de `build_opt.h` des projets concernés sont déjà écrites dans ce fichier. Sur Classic, les cibles 21, 24 et 28 l’utilisent pour fixer `E32_TX_MPX_MIN_CYCLES`. Conserve le fichier à côté du sketch.

**Résultats de référence.** Les 51 sketches Arduino du ZIP ont compilé et produit un binaire et une carte de lien sous Linux, avec les profils du tableau. Les 29 archives Windows de cœurs et d’outils ont été contrôlées et les 38 entrées du manifeste vérifiées. Le cache complet mesurait environ 2,40 Go. Une deuxième exécution du téléchargeur avec les téléchargements bloqués a donné le même manifeste. Ces vérifications ont été effectuées sous PowerShell sur Linux avec les fichiers Windows. Une compilation supplémentaire par famille a utilisé Arduino CLI 1.6.0, la version embarquée dans l’IDE choisi. Le YAML ESPHome a compilé séparément, puis a produit les mêmes 261 040 octets lors d’une recompilation hors ligne. Ces contrôles portent sur les firmwares. La première utilisation de l’interface graphique sur Windows demeure à vérifier sur le poste de destination.

SHA-256 du ZIP source · `347a654890dd90950524cc14dda18bf0b8666f1805e454c4cf16246315d3f9fc`. SHA-256 du binaire ESPHome de référence · `eeafc4e4b871fc6f4f5b0c4ecc39cfd865ef6863724b80d3d46032dfa132d503`.

## 2. Les quatre couches dont dépend le code

| Couche | Exemple dans ce dépôt | Preuve à emporter |
|---|---|---|
| Silicium et ROM de masque | Sur l’ESP8266, des pointeurs de fonction sont initialisés avec des adresses littérales. Sur Classic, le chemin effectif dépend aussi de la révision du silicium. | Référence de module et de puce, révision/ECO, cristal, taille/type de flash, errata datés, carte mémoire et fichiers de symboles ROM correspondant à la cible. Une adresse a une signification dans ce contexte précis. |
| Logique matérielle et registres mappés en mémoire (MMIO) | FIFO UART, compteurs, logique d’acquisition I/Q, sélection d’horloge et registres d’interruption. | Noms de registres et masques du source, types et largeur des accès, code assembleur, barrières mémoire, ordre des accès, en-têtes SoC et TRM du même silicium. Un registre lu à une adresse est un périphérique, pas une fonction ROM. |
| Binaire du SDK et état en RAM | `libphy.a`, `phy_param`, `g_phyFuns`, fonctions importées, tables de calibration et correctifs d’initialisation. | Archive `.a` complète par cible, empreinte SHA-256, en-têtes et scripts `.ld` associés, ordre de liaison et `.map` du firmware qui fonctionne. Une déclaration `extern` suffit au compilateur pour vérifier un appel, puis l’éditeur de liens doit trouver sa définition. |
| Code et outils reconstruisibles | Sketch, fichiers `.c/.cpp/.S`, Arduino Core, générateur ESPHome, scripts Python, assembleur, compilateur, linker et outil de production du `.bin`. | Sources exactes, archives des paquets et copies installées, version de chaque exécutable, options du générateur, commande complète et environnement hôte. |

**Scénario « il ne reste que la ROM ».** Un binaire historique encore conservé permet d’étudier les produits du lien. Avec ses fichiers ELF et MAP, on peut aussi identifier quels symboles venaient des bibliothèques et lesquels étaient absolus. Aucun de ces fichiers ne restitue à lui seul la bibliothèque réutilisable, les scripts de démarrage ou le compilateur. Si toutes les copies légitimes d’une routine indispensable hors ROM ont disparu, la reconstruction équivalente reste indéterminée. Il faut retrouver l’archive d’origine ou porter et qualifier une implémentation de remplacement. Une mise à jour du SDK peut changer l’ABI même quand le nom de la fonction et son prototype public demeurent identiques.

## 3. Versions, cartes et options à geler

| Cible | Profil de référence et options FQBN | Dépendance temporelle ou matérielle |
|---|---|---|
| ESP8266 / D1 mini | `esp8266:esp8266@3.1.2` · `esp8266:esp8266:d1_mini:xtal=160,eesz=4M1M,mmu=4816` | Xtensa LX106 à 160 MHz. Plan flash de 4 Mo avec 1 Mo pour le système de fichiers. Option MMU 48 Kio d’IRAM et 16 Kio de cache. Le choix de menu `xtal=160` règle ici la fréquence CPU. Conserver le cristal physique séparément. |
| ESP32 Classic | `esp32:esp32@3.3.12` · `esp32:esp32:esp32:CPUFreq=240,FlashMode=dio,FlashFreq=40` | Deux cœurs Xtensa LX6. Référence Classic à 240 MHz, flash DIO à 40 MHz. Plusieurs variantes exigent FreeRTOS bicœur, IPC ISR et le tick Xtensa. |
| ESP32-C5 | Même cœur Arduino-ESP32. `esp32:esp32:esp32c5:CPUFreq=240,FlashMode=dio,FlashFreq=80,FlashSize=4M,PartitionScheme=default,PSRAM=disabled,CDCOnBoot=default` | Cœur RISC-V à 240 MHz pour les boucles minutées. Référence C.B. en DIO 80 MHz, 4 Mo, partitions par défaut, PSRAM désactivée et UART0. Pour les cibles 02, 04 et 48, sélectionner `FlashSize=8M,PartitionScheme=default_8MB`. La carte de 8 Mo est attestée dans leurs README. Le choix de cette table standard est celui des builds vérifiés. |
| ESP32-C6 | Même cœur Arduino-ESP32. `esp32:esp32:esp32c6` | Cœur RISC-V à 160 MHz. Les six builds C6 utilisent les valeurs par défaut du paquet 3.3.12, dont QIO et 80 MHz. Pour les reproduire dans l’IDE, suivre la ligne correspondante du chapitre 1. |
| YAML sur ESP8266 | ESPHome `2026.10.1`, plate-forme PlatformIO Espressif8266 `4.2.1`, framework Arduino `3.1.2`, source PlatformIO `3.30102.0` | Les **clés actives** fixent `board: d1_mini`, `board_flash_mode: dout`, `board_build.f_cpu: 160000000L`, NONOS `22x_190703`. Le commentaire historique parle de `esp01_1m`. Le générateur lit les clés actives. |

Arduino-ESP32 **3.3.12** est construit sur ESP-IDF **5.5.5**. Des sources mentionnent un arbre IDF **6.2** utilisé par l’auteur pour comparer une bibliothèque. La chaîne décrite ici compile avec le paquet Arduino 3.3.12. Sur ESP8266, garder le NONOS SDK livré dans le cœur 3.1.2, y compris sa sélection par défaut 2.2.1+100 (190703), ses fichiers de démarrage et ses bibliothèques. Les dépendances `ESP8266WiFi`, `LittleFS` et `ESP8266WebServer` proviennent de ce paquet. Le SDK ESP32 contient ses propres composants FreeRTOS, HAL, pilotes et bibliothèques par puce.

Le couple *carte + options* indique comment construire l’image de référence. Une autre capacité de flash ou une autre révision de carte demande une vérification de compatibilité distincte. Une option de fréquence flash ne donne pas la cadence du processeur. Une broche USB série native n’offre pas forcément le comportement de l’UART0 qui alimente directement ces FIFO.

## 4. Les dépendances non standard par silicium

### ESP8266 — pointeurs littéraux, SDK NONOS et budget IRAM

- Dans le dossier PCM12 ESP8266, ouvrir `hardware_esp8266.h`. Les types `SetTxClkFn` et `SetAnaScaleFn` sont associés dans le source aux valeurs `0x4000650C` et `0x4000678C`. C’est une convention d’appel imposée par ce firmware. Un éditeur de liens peut produire une image alors que cette convention serait fausse sur un autre silicium. Conserver les prototypes, les valeurs et la révision de référence.
- `user_interface.h` dessert les API NONOS. Des projets déclarent séparément `set_rf_freq_offset` ou `chip6_phy_init_ctrl`. La présence d’un nom dans un en-tête, sa présence dans une archive, son adresse de lien et sa valeur à l’exécution sont quatre faits distincts. Conserver l’en-tête **et** le fournisseur relevé dans la MAP.
- Les boucles lisent les registres UART et les compteurs de cycles directement. L’assembleur Xtensa (`rsr`, `memw` selon le fichier), le type `volatile`, les macros d’interruption et les fonctions placées en IRAM relèvent de l’architecture. Dans les sources à Timer1, le choix du diviseur, la fréquence CPU et la durée de l’ISR contribuent ensemble à la cadence audio.
- Vérifier les sections d’instructions et les constantes lues par les ISR. Un appel depuis une routine en IRAM vers un helper placé en flash peut compiler et échouer lorsque le cache est indisponible. Garder l’option MMU et rechercher les débordements `.iram*` dans le journal. Pour les gros sketches Wi-Fi, conserver le partage flash/application/LittleFS et les données embarquées en flash.
- Les tests de réception standalone ont des conventions de transport différentes des émetteurs. Par exemple, le récepteur SSB produit des paires I/Q, tandis que le récepteur AM produit du PCM mono. Une carte qui se connecte au navigateur mais fournit du bruit peut simplement être appairée à la mauvaise interface.

### ESP32 Classic — ABI Xtensa, correctifs du SDK et deux cœurs

- Les fichiers `src/e32_radio.c` et `src/arduino_phy_compat.c` emploient des symboles `rom_*`, `ram_*` et la table `g_phyFuns`. Le commentaire du code décrit une table de dispatch corrigée par le SDK, notamment sur ECO1. Il faut préserver l’initialisation de cette table, l’ABI et le binaire qui la fournit. Son contenu en RAM peut différer des entrées d’une table ROM.
- `soc/dport_access.h`, les accès DPORT, le HAL UART et les structures TIMERG sont spécifiques à cette cible. Dans `e32_radio.c`, un décalage de fréquence est exprimé en unités de **1/1024 de MHz**. Le code C6 utilise ailleurs le kHz entier. Un remplacement compilable qui garde l’entier mais change son unité change le comportement.
- Le chemin PCM12 Classic utilise `e32_pcm_worker.cpp`, `e32_dram_cursor.h` et `e32_tick.S`. Les curseurs partagés sont des mots de 32 bits alignés en DRAM interne, avec ordre mémoire explicite autour des accès Xtensa. La routine assembleur du tick est en `.iram1`. Son contrat indique un appel **CALLX0**, l’argument dans `a2` et un ensemble de registres utilisables restreint. Préserver l’extension `.S`, le prétraitement, les symboles globaux et la convention d’appel.
- Dans le code qui les vérifie, `CONFIG_FREERTOS_UNICORE` doit être désactivé, `CONFIG_ESP_IPC_ISR_ENABLE` activé, le tick doit utiliser CCOUNT et l’index du timer attendu. `e32_adc_bench.c` référence aussi des bornes fournies par le linker. La branche de capture de banc est conditionnée par `CB13_BENCH_IQ`. Elle réserve de la DIRAM et vérifie ses alias IRAM ainsi que les bornes du tas. Garder les scripts, les drapeaux et la MAP. Une bibliothèque qui ajoute un verrou ou un appel auxiliaire dans le chemin critique peut modifier le minutage sans erreur de compilation.
- Lire **le `build_opt.h` de la variante**. Le PCM12 Classic d’origine demande `-O2 -DE32_TX_MPX_MIN_CYCLES=56`. Le PCM12 Classic v2 et le test à deux porteuses demandent `-O2 -DE32_TX_MPX_MIN_CYCLES=80`. Les réglages locaux restent associés au chemin complet du projet.

### ESP32-C5 — bibliothèque PHY propre au C5 et différences entre projets

- Les projets C.B. qui utilisent `src/arduino_phy_compat.c` vérifient la cible et la version d’IDF. Le chemin conditionnel dépend de `CONFIG_ESP_PHY_ENABLE_CERT_TEST`. Le commentaire signale l’absence de `librftest.a` dans le paquet Arduino standard. Le code du dépôt fournit déjà l’adaptation prévue pour les primitives exportées par **ce** `libphy.a`. Archiver l’adaptateur avec la bibliothèque de la même version.
- Ces sources utilisent des données privées comme `phy_param`, des fonctions au préfixe `phy_` et des conventions de calibration. Un prototype identique n’implique ni la même disposition de table, ni les mêmes unités, ni les mêmes effets d’initialisation. `c5_radio.c`, `c5_tx_radio.cpp` et, dans les variantes concernées, `c5_bands.h` matérialisent des hypothèses différentes de celles du C6.
- Les gardes PCM12 exigent la fréquence CPU attendue et excluent la gestion dynamique de puissance dans les chemins minutés. Le runtime s’appuie sur la HAL des watchdogs, FreeRTOS, la configuration des groupes de timers et le routage UART0. Les références matérielles C5 C.B. utilisent GPIO11/TX et GPIO12/RX. Conserver la configuration des ports, les hypothèses sur les interruptions et les tampons locaux.
- Les trois projets C5 de réception AM, émission AM et émission SSB ont de vrais `.cpp/.h` **voisins du sketch**. Ils passent par `driver/gptimer.h` et `hal/uart_ll.h`. Ils ne sont pas organisés comme les projets C.B. avec `src/` et `build_opt.h`. Pour la réception, inclure `rx_radio.cpp`. Pour les deux émetteurs, garder la version de `radio.cpp` du dossier concerné.

### ESP32-C6 — mêmes outils RISC-V, autre ABI radio

- Le code sélectionne `CONFIG_IDF_TARGET_ESP32C6` et le chemin PCM12 exige 160 MHz. Le partage de la chaîne RISC-V avec le C5 concerne le compilateur. Le jeu d’en-têtes SoC, le linker script ROM, les adresses MMIO, les bibliothèques et certains prototypes sont spécifiques au C6.
- Dans `src/c6_radio.c`, on trouve notamment `chip_v7_set_chan`, `phy_set_freq`, `ram_stop_tx_tone`, `rom_chip_i2c_readReg` et `rom_chip_i2c_writeReg`. Dans `arduino_phy_compat.c`, les noms et contrats de plusieurs fonctions diffèrent des équivalents C5. Une fonction préfixée `rom_` doit encore être rattachée au bon fournisseur et à la révision à partir de l’ELF et de la MAP.
- `arduino_runtime.cpp` vérifie la configuration USB et la fréquence effective. Le matériel de référence C.B. utilise UART0 GPIO16/TX et GPIO17/RX via un pont série. L’acquisition, la gestion des interruptions et les accès au FIFO passent par le matériel C6. Une page sur le mauvais débit ou une console de logs sur le même UART peut fausser le diagnostic.

### Empreintes des bibliothèques effectivement retrouvées

Les empreintes ont été recalculées sur les paquets de cartes officiellement distribués et installés pour les essais. Les trois valeurs ESP32 concordent avec celles inscrites dans les commentaires des adaptateurs par leurs auteurs. Elles identifient le contenu de chaque archive statique **extraite**. La comparaison historique du dépôt cite `esp-phy-lib` au commit `e294ff039e26b3486d6c9e5853d24d98ee3300b2`. Comparer le fichier extrait du kit effectif avant de le classer comme exemplaire qualifié.

| Fichier extrait du paquet de cartes | SHA-256 vérifié |
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

## 5. Catalogue statique des 52 cibles

Les numéros correspondent au tableau des réglages GUI du chapitre 1. Chaque ligne relie le chemin exact d’une variante à son comportement, à ses fichiers de construction ou à ses dépendances matérielles.

| Nº | Cible et entrée dans le dépôt | Particularité à conserver |
|---:|---|---|
| 01 | **ESP8266** · `AM-RX-YEAHHHHHv1/AMReceiverHQ.ino` | Récepteur AM standalone. Estimation I/Q, détection d’enveloppe et sortie AM01 PCM16 48 kéch/s. UART 1,5 Mbaud 8N2. Garder le gain reçu et la convention d’échantillon manquant. |
| 02 | **C5** · `AM-RX-YEAHHHHHv1/ESP32-C5v1/AMReceiverHQ_C5/AMReceiverHQ_C5.ino` | Port C5 du récepteur AM. Le backend rx_radio.cpp prépare l’acquisition I/Q. GPTimer et UART bas niveau. AM01 à 2 Mbaud 8N1. Banc 8 Mo. |
| 03 | **ESP8266** · `AMSerialHQv1/B2_AM_LIVE_SERIAL_HIFI_OV2.ino` | Émetteur AM série ESP8266. Échantillons de commande d’amplitude, table locale et flux avec contrôle de remplissage. Conserver la page d’encodage AM 2 Mbaud. |
| 04 | **C5** · `AMSerialHQv1/ESP32-C5v1/AMSerialHQv1_C5/AMSerialHQv1_C5.ino` | Émetteur AM C5. radio.cpp adjacent, GPTimer et HAL UART. la conversion d’amplitude dépend de la bande choisie. Préserver la page C5 et le profil 8 Mo. |
| 05 | **ESP8266** · `CB-FMStereov1/CB_FM_STEREOv5.ino` | Ancienne C.B. stéréo v5 monolithique. Protocole 4 à 2 Mbaud. Distinguer du firmware historique protocole 3 encapsulé dans le YAML. |
| 06 | **ESP8266** · `CB-FMStereov1/CB_V10/CB_FM_STEREOv10_Arduino/CB_FM_STEREOv10_Arduino.ino` | C.B. V10. Le sketch inclut ses modules .h à la racine. MPX et RDS. conserver le décodage RadioText et la page de la V10. 2 Mbaud, protocole 4. |
| 07 | **ESP8266** · `CB-FMStereov1/CB_V12/CB_FM_STEREOv12_Arduino/CB_FM_STEREOv12_Arduino.ino` | C.B. V12. Architecture de V10 avec gestion de préaccentuation/désaccentuation 50/75 µs dans la chaîne associée. 2 Mbaud, protocole 4. |
| 08 | **ESP8266** · `CB-FMStereov1/CB_V13/CB_FM_STEREOv13_Arduino/CB_FM_STEREOv13_Arduino.ino` | C.B. V13 ESP8266. Passage au transport 1,5 Mbaud 8N1. MPX et protocole 4. Conserver les .h de ce dossier et le navigateur V13. |
| 09 | **C5** · `CB-FMStereov1/CB_V13/CB_FM_STEREOv13_C5/CB_FM_STEREOv13_C5.ino` | Port C5 de V13. Frontend V13, uniquement la bande 2,4 GHz dans cette édition. Runtime C5, adaptateur PHY et src/ propres à cette branche. |
| 10 | **C6** · `CB-FMStereov1/CB_V13/CB_FM_STEREOv13_C6/CB_FM_STEREOv13_C6.ino` | Port C6 de V13. Protocole 4, I/Q matériel, horloge 160 MHz. l’UART0 et le libphy sont ceux du C6. |
| 11 | **ESP32** · `CB-FMStereov1/CB_V13/CB_FM_STEREOv13_ESP32/CB_FM_STEREOv13_ESP32.ino` | Port Classic de V13. Conserver les fichiers e32_*, l’adaptateur de la cible et le runtime Classic. MPX en émission et réception. |
| 12 | **C5** · `CB-FMStereov1/CB_V13_5GHZ_C5_BETA/CB_FM_STEREOv16_C5/CB_FM_STEREOv16_C5.ino` | Branche bêta 5 GHz C5, sketch nommé v16. Protocole 7 à 2 Mbaud et page dédiée. Garder son code de sélection de bande ainsi que ses propres limites et diagnostics. |
| 13 | **C5** · `CB-FMStereov1/CB_V14/CB_FM_STEREOv13_C5/CB_FM_STEREOv13_C5.ino` | C5 V14 avec nom de sketch v13. MPX, protocole 4 à 1,5 Mbaud. cette branche gère aussi 5 GHz. Son homonyme du dossier V13 fonctionne en 2,4 GHz. |
| 14 | **C6** · `CB-FMStereov1/CB_V14/CB_FM_STEREOv13_C6/CB_FM_STEREOv13_C6.ino` | C6 V14 avec nom v13. MPX dans les deux directions. Conserver la fermeture complète de ce répertoire pour comparer V13 et V14. |
| 15 | **ESP32** · `CB-FMStereov1/CB_V14/CB_FM_STEREOv13_ESP32/CB_FM_STEREOv13_ESP32.ino` | Classic V14 avec nom v13. Chemin d’acquisition et runtime Classic. conserver la page V14 et les versions locales des modules e32_*. |
| 16 | **ESP8266** · `CB-FMStereov1/CB_V14/CB_FM_STEREOv13_ESP8266/CB_FM_STEREOv13_ESP8266.ino` | ESP8266 V14 avec nom v13. Cible V14 en MPX, protocole 4. Le code et les en-têtes sont sélectionnés par le dossier parent. |
| 17 | **ESP8266** · `CB-FMStereov1/CB_V15_PCM12/CB_FM_STEREO_MPXRX_PCM12TX_ESP8266/CB_FM_STEREO_MPXRX_PCM12TX_ESP8266.ino` | V15 PCM12 hybride ESP8266. Le navigateur envoie du PCM12 stéréo. le retour demeure MPX3. Multiplex construit sur le microcontrôleur, IRAM et Timer1 à vérifier. |
| 18 | **ESP8266** · `CB-FMStereov1/CB_V15_PCM12/CB_FM_STEREO_MPXRX_PCM12TX_ESP8266v2/CB_FM_STEREO_MPXRX_PCM12TX_ESP8266.ino` | Variante v2 ESP8266. Deux sorties de modulation pour le même programme. la documentation ESP8266 garde une déviation de référence de ±50 kHz. Même nom de sketch que la première édition. |
| 19 | **C6** · `CB-FMStereov1/CB_V15_PCM12/PCM12_C6/CB_FM_STEREO_PCM12_C6.ino` | C6 V15 PCM12. Retour PCM12 et RDS, génération du multiplex sur puce. Modules séparés pour échantillonnage, émission et décodage PCM. |
| 20 | **C6** · `CB-FMStereov1/CB_V15_PCM12/PCM12_C6v2/CB_FM_STEREO_PCM12_C6.ino` | C6 V15 v2. Deux porteuses superposées pour le programme stéréo et consigne de déviation modifiée. Copier le c6_tx* de v2. |
| 21 | **ESP32** · `CB-FMStereov1/CB_V15_PCM12/PCM12_ESP32/CB_FM_STEREO_PCM12_ESP32.ino` | Classic V15 PCM12. DSP bicœur, curseurs partagés en DRAM et tick assembleur. Le minimum E32_TX_MPX_MIN_CYCLES est 56. |
| 22 | **C5** · `CB-FMStereov1/CB_V15_PCM12/PCM12_ESP32C5/CB_FM_STEREO_PCM12_C5.ino` | C5 V15 PCM12. Moteur de transmission et acquisition propres au C5 et aux deux bandes. Le paquet V15 de référence utilise la flash de 4 Mo. |
| 23 | **C5** · `CB-FMStereov1/CB_V15_PCM12/PCM12_ESP32C5v2/CB_FM_STEREO_PCM12_C5.ino` | C5 V15 v2. Même programme sur deux oscillateurs et consigne de déviation modifiée. Les tables locales du dossier et la cadence des mises à jour distinguent cette variante. |
| 24 | **ESP32** · `CB-FMStereov1/CB_V15_PCM12/PCM12_ESP32v2/CB_FM_STEREO_PCM12_ESP32.ino` | Classic V15 v2. Les fichiers de synchronisation inter-cœurs et le réglage minimal de 80 cycles font partie du profil de cette version. |
| 25 | **C5** · `CB-FMStereov1/CB_V15_PCM12/TESTONLY-C5-DUALCARRIER/CB_FM_STEREO_PCM12_C5.ino` | Essai C5 à deux porteuses séparées portant le même multiplex. le fichier de politique de bande et le backend de transmission changent. Garder distinct de v2. |
| 26 | **C5** · `CB-FMStereov1/CB_V15_PCM12/TESTONLY-C5-DUALCARRIER/TEST_2_INDEP_CARRIER_MONO/CB_FM_STEREO_PCM12_C5.ino` | Essai C5 imbriqué à deux programmes mono indépendants. Routage gauche/droite et interpolation propres à ce répertoire. la hiérarchie contient un deuxième sketch homonyme. |
| 27 | **C6** · `CB-FMStereov1/CB_V15_PCM12/TESTONLY-C6-DUALCARRIER/CB_FM_STEREO_PCM12_C6.ino` | Essai C6 à deux porteuses séparées et un multiplex partagé. Les calculs et tables spécifiques C6 changent, même si le .ino de premier niveau est très court. |
| 28 | **ESP32** · `CB-FMStereov1/CB_V15_PCM12/TESTONLY-ESP32-DUALCARRIER/CB_FM_STEREO_PCM12_ESP32.ino` | Essai Classic à deux porteuses séparées. Cadence minimale 80 cycles et synchronisation dual-core. Convention d’appel de l’assembleur identique à la famille Xtensa Classic. |
| 29 | **ESP8266** · `CB-FMStereov1/CB_V15_PCM12/TESTONLY-ESP8266-DUALCARRIER/CB_FM_STEREO_MPXRX_PCM12TX_ESP8266.ino` | Essai ESP8266 à deux porteuses séparées et multiplex partagé. Le protocole reste hybride PCM12 TX / MPX RX. Le répertoire d’essai enfant est une autre cible. |
| 30 | **ESP8266** · `CB-FMStereov1/CB_V15_PCM12/TESTONLY-ESP8266-DUALCARRIER/TEST_2_INDEP_CARRIER_MONO/CB_FM_STEREO_MPXRX_PCM12TX_ESP8266.ino` | Essai ESP8266 imbriqué à deux programmes mono. Les canaux audio indépendants changent le DSP en émission. Isoler le sketch de celui de son parent. |
| 31 | **ESP8266** · `CB_DuplexVoice_V3/CB_V4/CB_DuplexVoice_V4_Arduino/CB_DuplexVoice_V4_Arduino.ino` | Duplex Voice V4. Alternance temporelle TDD, PCM/compandage et trames DPX1 avec la page V4 avancée. Liaison série 460 800 bauds 8N2. |
| 32 | **ESP8266** · `CB_DuplexVoice_V3/CB_V5/CB_DuplexVoice_V4_Arduino/CB_DuplexVoice_V4_Arduino.ino` | Duplex Voice V5. Le sketch conserve son nom V4. l’interface EASY distingue l’édition. Même débit de 460 800 bauds 8N2. |
| 33 | **ESP8266** · `CB_DuplexVoice_V3/CB_V6/CB_DuplexVoice_V6_Arduino/CB_DuplexVoice_V6_Arduino.ino` | Duplex Voice V6 sur ESP8266. Capture des créneaux en mode passif, DPX1 à 921 600 bauds 8N2. La page V5 utilise un autre débit. |
| 34 | **C5** · `CB_DuplexVoice_V3/CB_V6/CB_DuplexVoice_V6_C5/CB_DuplexVoice_V6_C5.ino` | Duplex Voice V6 sur C5. 921 600 bauds 8N2. le backend C5 remplace les dépendances au matériel du 8266. Conserver le runtime, la cadence et le fichier c5_cb_radio.cpp de V6. |
| 35 | **C5** · `CB_DuplexVoice_V3/CB_V7beta/CB_DuplexVoice_V6_C5/CB_DuplexVoice_V6_C5.ino` | Duplex Voice V7 bêta C5. Nommée V6 dans l’arbre. ajout de télémétrie DPT1 et d’identification côté interface. Préserver la négociation des créneaux et le backend C5. |
| 36 | **C6** · `CB_DuplexVoice_V3/CB_V7beta/CB_DuplexVoice_V6_C6/CB_DuplexVoice_V6_C6.ino` | Duplex Voice V7 bêta C6. DPT1/DPX1 à 921 600 bauds 8N2, runtime et PHY C6. La carte de référence utilise l’UART0 matériel. |
| 37 | **ESP32** · `CB_DuplexVoice_V3/CB_V7beta/CB_DuplexVoice_V6_ESP32/CB_DuplexVoice_V6_ESP32.ino` | Duplex Voice V7 bêta Classic. Le fichier peer_tick.S réarme le tick si CONFIG_FREERTOS_SYSTICK_USES_CCOUNT. Respecter CALLX0, l’argument dans a2 et la section IRAM. |
| 38 | **ESP8266** · `CB_DuplexVoice_V3/CB_V7beta/CB_DuplexVoice_V6_ESP8266/CB_DuplexVoice_V6_ESP8266.ino` | Duplex Voice V7 bêta ESP8266. Version distincte de la V6 mono-fichier. garder le décodage des trames de télémétrie et la page V7. |
| 39 | **ESP8266** · `FMSerialHQv1/B2_FM_LIVE_SERIAL_48K_WFM_B2_K4_TUNE1450_UART_FLOW8K_2000000.ino` | Émetteur FM mono série. Flux UART 2 Mbaud 8N1 et conversion d’échantillons en déviation. Conserver la table et les commandes de contrôle de flux de ce long nom de sketch. |
| 40 | **ESP8266** · `FMStereoHQv1/B2_FM_LIVE_SERIAL_STEREO_120K_MPX8.ino` | Émetteur FM stéréo V1. Multiplex MPX8 signé à 120 kéch/s généré dans le navigateur. canal fixe. La récupération stéréo dépend du pilote encodé par la page. |
| 41 | **ESP8266** · `FMStereoHQv2/B2_FM_LIVE_SERIAL_STEREO_120K_MPX8.ino` | Émetteur FM stéréo V2. Ajoute le choix du canal. même interface série MPX8. Conserver la page qui configure le canal. |
| 42 | **ESP8266** · `FMStereoHQv3/B2_FM_LIVE_SERIAL_STEREO_120K_MPX8.ino` | Émetteur FM stéréo V3. Le firmware correspond à celui de V2. la nouveauté RDS 57 kHz est dans l’encodeur HTML. Geler aussi ce fichier pour pouvoir reproduire la fonction V3. |
| 43 | **ESP8266** · `FMStereoRX-YEAHHHHHv1/FMStereoRX.ino` | Récepteur stéréo standalone. Produit du MPX8. le navigateur reconstruit le son, le pilote et le RDS. 1,5 Mbaud 8N2, choix de profils d’acquisition. |
| 44 | **ESP8266** · `MODEM-SimplexV1/FMFileModemRX/FMFileModemRX.ino` | Modem simplex, station RX. Le micrologiciel d’acquisition ESP8266 et la page FM_FILE_MODEM_RX.html constituent le couple témoin. Flux série 1,5 Mbaud 8N2. |
| 45 | **ESP8266** · `MODEM-SimplexV1/FMFileModemTX/FMFileModemTX.ino` | Modem simplex, station TX. Firmware de modulation et page FM_FILE_MODEM_TX.html à 2 Mbaud 8N1. Le framing fichier, la redondance et la vérification finale sont traités avec les pages. |
| 46 | **ESP8266** · `SSB-RX-YEAHHHHHv1/SSBReceiverHQ.ino` | Récepteur SSB. Trames IQ10, paires complexes de 10 bits groupées sur cinq octets pour deux paires, 48 kéch/s complexes. Démodulation USB/LSB dans le navigateur. |
| 47 | **ESP8266** · `SSBTestv1/B2_SSB_LIVE_SERIAL_HIFI.ino` | Émetteur SSB série ESP8266. Encodage côté navigateur et minutage des mises à jour côté puce. Conserver le profil d’échantillonnage et l’encodeur qui produit les données. |
| 48 | **C5** · `SSBTestv1/ESP32-C5v1/B2_SSB_LIVE_SERIAL_HIFI_C5/B2_SSB_LIVE_SERIAL_HIFI_C5.ino` | Émetteur SSB C5. radio.cpp et son .h à côté du sketch, adaptation de la fréquence et GPTimer. Carte de banc 8 Mo, entrée série propre au port C5. |
| 49 | **ESP8266** · `WIFIOnlyTest/B2_WFM_MONO_HIFI_48K_CONTROLE_MODULATION_APERCU.ino` | Wi-Fi seul, émetteur mono WFM. Serveur HTTP et page embarquée dans le sketch, LittleFS pour le clip WFM2. diagnostic série distinct du flux audio. Compiler ce seul .ino. |
| 50 | **ESP8266** · `WIFIOnlyTest/ESP8266_AM.ino` | Wi-Fi seul, émetteur AM. Clip AMF1 vérifié puis stocké dans LittleFS. Serveur, page et données embarquées appartiennent à ce .ino indépendant. |
| 51 | **ESP8266** · `WIFIOnlyTest/ESP8266_SSB.ino` | Wi-Fi seul, émetteur SSB. Stockage LittleFS au format SBF1, encodage USB/LSB. Le découpage de la flash affecte la capacité à stocker un clip après compilation. |
| 52 | **ESPHome** · `CB-FMStereov1/CB_FM_STEREO.yaml` | YAML ESPHome autonome. Code C++ en ligne, filtrage PlatformIO et aliases du linker : figer aussi le code effectivement généré. Protocole historique 3, 2 Mbaud. |

### Fichiers et empreintes des unités à compiler

Pour chaque cible, les chemins auxiliaires sont relatifs au dossier du sketch. Le SHA-256 porte sur **toutes les entrées de sa compilation présentes dans le dépôt**, dans leur version de l’archive fournie, y compris les en-têtes. Calcul exact : trier les chemins relatifs en ordre ordinal, puis, pour chacun, ajouter au hachage son chemin UTF-8 avec séparateur `/`, un octet nul et les 32 octets du SHA-256 de son contenu brut. Le fichier principal conserve son nom d’origine pour cette empreinte. Les outils du SDK ont leurs propres empreintes. Deux noms identiques dans le tableau précédent peuvent donc conduire à des ensembles très différents.

| Nº | Entrées | Fichiers d’implémentation adjacents ou sous src/ ; options locales | SHA-256 de l’ensemble |
|---:|---:|---|---|
| 01 | 1 | `.ino seul` | `6b65d32bc1c331255614fcc1d200a980d7941ce6a895c7ed0572c40a16d12aed` |
| 02 | 3 | `rx_radio.cpp` | `635ef253587c2383415b8c6fc8fc7fda5b43126aabfa12f76738bb15485f436c` |
| 03 | 1 | `.ino seul` | `073e07678cfb41348d4e7cd31a92b84eaeb7b1216c32537501116e8bcae55ad6` |
| 04 | 3 | `radio.cpp` | `41667880f2dfe9d4578c4ea21bb4f157e2aa2536e898ad23a79e101e0a72b3b9` |
| 05 | 1 | `.ino seul` | `c65f42da0c4dbe83a7b8e3435822033a67aa2c7549f8cbb331b8d5dfebe0fc32` |
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
| 31 | 1 | `.ino seul` | `ac6378e55f6a56e219f49d956b68d773e0fbdf534f4de2bfa04655a326c3bc80` |
| 32 | 1 | `.ino seul` | `ac6378e55f6a56e219f49d956b68d773e0fbdf534f4de2bfa04655a326c3bc80` |
| 33 | 1 | `.ino seul` | `f1bc866f13a892a04bbb52297b22cdc7b6fd8a52cc841485a849f2ad99bcca46` |
| 34 | 8 | `src/arduino_phy_compat.c`, `src/arduino_runtime.cpp`, `src/c5_cb_radio.cpp`, `src/firmware.cpp`, `build_opt.h → -O2` | `1f6d0c37a04d4ca31029169fe90cec43cc1007d104188316c3bd92ec92be977a` |
| 35 | 8 | `src/arduino_phy_compat.c`, `src/arduino_runtime.cpp`, `src/c5_cb_radio.cpp`, `src/firmware.cpp`, `build_opt.h → -O2` | `a1718e36f91a36aad0d1f2efbc5c19ea4b8e78a4ac367b7bc528da6f45805d04` |
| 36 | 8 | `src/arduino_phy_compat.c`, `src/arduino_runtime.cpp`, `src/c6_cb_radio.cpp`, `src/firmware.cpp`, `build_opt.h → -O2` | `99316a91a893fcb019c0a5e4ae0f9e110584e994257a76771304d1f39566c66e` |
| 37 | 11 | `src/arduino_phy_compat.c`, `src/arduino_runtime.cpp`, `src/e32_cb_radio.cpp`, `src/e32_radio.c`, `src/firmware.cpp`, `src/peer_tick.S`, `build_opt.h → -O2` | `9d31ac300ed35078fccdc65d75573b34ac430629c19ea863efd577969c35f4b0` |
| 38 | 1 | `.ino seul` | `349a64c63b5cee3d8a50142be70d087e58ea8b2a8daf18901ee2212ee9ba9a16` |
| 39 | 1 | `.ino seul` | `92bd3c2ce73636a9352901281c0b395942ef837270f84ddbb30f6c4f9adf0495` |
| 40 | 1 | `.ino seul` | `a4154ed63003d3a3e813ea732ea2c8594f21159a7a18bb58edbaf28272095a4a` |
| 41 | 1 | `.ino seul` | `c7cc16898f3a2f40c84222718b5f00f8d719c2553904788c9b278b289b8a5201` |
| 42 | 1 | `.ino seul` | `c7cc16898f3a2f40c84222718b5f00f8d719c2553904788c9b278b289b8a5201` |
| 43 | 1 | `.ino seul` | `2da5c34dff74bfc92559dc325045bfab7996dc5a34ddbb11ae38030376ef7281` |
| 44 | 1 | `.ino seul` | `df13ead469aab6081a86abf0101e33c4e4e886441e42a592a14c9dbab1f70f4e` |
| 45 | 1 | `.ino seul` | `ac8f6be38d6cd8fb7aee895e6d28c2c7c9ffe4111e9d5f329c02194b0797f411` |
| 46 | 1 | `.ino seul` | `9ebbd8b16fdb194b5704cf2a774b484d8d26f54f9be12f2cb00a891d4f3719e3` |
| 47 | 1 | `.ino seul` | `e9690c0ccba338ca6521c8e6bed891861bf916c673ef46df15dd3c91c5bdc963` |
| 48 | 3 | `radio.cpp` | `00696ceee6f403f149ff9444babe392d2b6a751817a318990f542f6f0739bb09` |
| 49 | 1 | `.ino seul` | `a796680da92708c43a745a52bef51ec959f70013024c922476ce60b9035cd5c7` |
| 50 | 1 | `.ino seul` | `fa46a92430a1a6dd37f071c3f0f8262f3d10b34985d01c14e91e94909a00a615` |
| 51 | 1 | `.ino seul` | `a3685872185a9f99c2e95f385c73bd54dbc75bf7ec820cf14fc4a44bfcbde688` |
| 52 | 1 | `YAML seul` | `112b63cb0331c5406cea1336c6c8d9363cdb8b76fc5646f9131c023482e393d4` |

### Repères matériels rattachés aux fichiers de chaque cible

Pour aller d’une erreur de lien ou d’un problème de cadence au bon fichier, ce tableau relève des identifiants effectivement présents dans les sources de chaque cible. Chaque repère porte la ligne de sa première occurrence dans le fichier indiqué, commentaires compris. Les fonctions privées, les objets de données, les macros, les mots d’assembleur et les fonctions définies par le dépôt servent ici de points d’entrée pour la lecture. Pour savoir qui fournit une définition au lien, utiliser la procédure ELF/MAP du chapitre 8.

| Nº | Repères présents dans cette cible |
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

## 6. Cible 52 — pourquoi le YAML demande sa propre chaîne

Le firmware historique inclus dans `CB-FMStereov1/CB_FM_STEREO.yaml` est du C++ inséré comme une valeur YAML multiligne. Le générateur ESPHome le transforme en source compilable. La première ligne de l’inclusion, son indentation, le marqueur final, l’espace de noms `cbfm_native` et les noms C++ manglés forment une partie de la recette. Réindenter le C++ en dehors de sa chaîne ou remplacer le YAML par le sketch v5 voisin peut produire un autre programme.

| Élément actif | Rôle pour reconstruire la variante |
|---|---|
| Générateur ESPHome 2026.10.1, backend PlatformIO | Génère notamment `src/main.cpp` et `platformio.ini`. Garder les deux fichiers issus d’un passage réussi en plus du YAML brut. Ils documentent les transformations qui seraient perdues si un futur générateur changeait l’inclusion. |
| `-Wl,--wrap=setup` et `-Wl,--wrap=loop` | Dirigent les points d’entrée Arduino vers les aliases du code inclus. Le source nomme `_ZN11cbfm_native5setupEv` et `_ZN11cbfm_native4loopEv`. Le nom manglé est une conséquence de la signature et du namespace C++. |
| `build_unflags` et `-Wl,--wrap=millis` | Retirent l’enveloppement de `millis`. Le temps attendu par le firmware provient alors du cœur Arduino. |
| `build_src_filter` | Restreint le runtime compilé à `main.cpp`, `esphome/components/esp8266/waveform_stubs.cpp` et `esphome/components/esp8266/lwip_glue_stubs.cpp`. Ces chemins appartiennent exactement à l’arbre généré par ESPHome. |
| `-DPIO_FRAMEWORK_ARDUINO_ESPRESSIF_SDK22x_190703` | Sélectionne la variante du SDK NONOS. Archiver la version du framework PIO et ses bibliothèques ; une autre installation Arduino sur le PC ne suffit pas à satisfaire PlatformIO. |
| Hôte Python et paquets | Pour cette seule variante, la chaîne séparée utilise **ESPHome 2026.10.1** et **PlatformIO Core 6.2.0**. **4.2.1** désigne la plateforme Espressif8266. Conserver les paquets Python et PlatformIO d’une installation ESPHome fonctionnelle si tu veux aussi restaurer cette variante. |

Le commentaire du YAML cite l’empreinte `2cee9d862f07afe63d4515ed29175d15c0a3a199021cbafd4f7cc9ba6ef0fd68` pour le sketch historique embarqué tel que comparé par son auteur. La colonne des empreintes au chapitre 5 hache, elle, le **fichier YAML entier avec son chemin**. Ce sont deux contrôles portant sur des objets différents.

## 7. Ce qu’il faut effectivement mettre à l’abri

1. **Les sources immuables et les sources de travail.** Garder le ZIP original, son SHA-256, le commit, les répertoires complets et, pour toute modification, un diff ou un commit supplémentaire. Conserver `LICENSE`, les notices de fichiers, les schémas, les README et les références. Certaines illustrations de README pointent sur GitHub. Télécharger séparément celles qui servent de preuve de câblage ou de mesure et conserver leur contexte. Une copie du ZIP reste indépendante de son nom de branche.
2. **Les paquets Arduino.** Copier `Arduino-telechargements` entier et, après installation, `%LOCALAPPDATA%\Arduino15`. Conserver les cœurs, le cache, les index, `boards.txt`, `platform.txt`, les variants, bibliothèques, scripts, tables de partitions, en-têtes et scripts de lien. Le FQBN seul ne contient pas ces fichiers.
3. **Les outils hôtes et les outils croisés.** L’IDE 2.3.11 inclut Arduino CLI **1.6.0**. Les index Arduino-ESP32 3.3.12 référencent notamment `esp-x32` et `esp-rv32` révision de paquet `2601` (GCC 14.2.0, construction 20260121), `esptool_py` 5.3.1 et les paquets SDK par SoC. Vérifier les noms exacts dans l’index archivé. Pour ESP8266, garder la chaîne `xtensa-lx106-elf` et les utilitaires du cœur 3.1.2. Conserver aussi assembleur, linker, `ar`, `nm`, `objdump`, `readelf`, `objcopy`, les outils intégrés de la CLI et ses préprocesseurs. Inclure les exécutables Windows, leurs DLL et les dépendances runtime du système. Deux distributions GCC affichant un numéro majeur identique peuvent avoir des patches différents.
4. **ESPHome en option.** Si tu entretiens le YAML, sauvegarde séparément Python, les paquets Python installables, les versions installées, PlatformIO, ses outils, son framework et les dépendances téléchargées. Une liste de versions doit accompagner les archives binaires.
5. **Produits de chaque compilation.** Dans **Fichier → Préférences**, active la sortie détaillée de compilation. Après **Vérifier**, garde le journal et les fichiers pertinents du dossier temporaire que la sortie indique, avant son nettoyage. **Croquis → Exporter les binaires compilés** permet de conserver les images. Archive aussi l’ELF, la MAP, les options résolues et le C++ généré lorsqu’ils existent. Pour ESPHome, conserver `firmware.bin`, l’ELF, le journal et le projet généré. Étiqueter chaque témoin avec son projet, sa carte et son profil.
6. **Le témoin matériel et l’environnement de test.** Relever modèle de carte, module, révision, cristal, flash réelle, pont USB-UART, version des pilotes et paramètres série. Garder un exemplaire fonctionnel par famille si possible. Sauvegarder les pages HTML/JS/CSS locales, un navigateur compatible Web Serial/Web Audio, les clips de test et des trames UART déjà capturées. Des outils de compilation seuls ne reproduisent pas un navigateur, un encodeur RDS ou les caractéristiques analogiques du banc.
7. **Configuration propre à une carte.** Documenter le partitionnement réellement installé, les paramètres d’initialisation PHY et l’existence de données de calibration de l’appareil en flash/NVS le cas échéant. Les caractéristiques eFuse, la calibration de chaque module, les clés éventuellement utilisées par un produit et la révision de silicium relèvent de sa fiche matérielle. Les valeurs d’un autre exemplaire ne constituent pas un profil générique. Si le produit utilise une chaîne de signature, documenter la procédure autorisée et conserver les clés séparément suivant son mode de gestion.
8. **Support de secours.** Garder au moins deux exemplaires lisibles sur des supports indépendants, dont l’un hors ligne. Conserver une image exportée de la machine x64 de référence et l’outil nécessaire pour la restaurer. À chaque migration de disque, recalculer le manifeste des fichiers, puis exécuter un vrai build sur la copie. Une somme de contrôle détecte une perte ; c’est le deuxième exemplaire qui permet la réparation.

### Faire un inventaire d’intégrité sans nouveau logiciel

Fermer le BAT, puis rassembler les fichiers à conserver sous un dossier dédié, par exemple `C:\Radio`. Les commandes suivantes s’exécutent dans **Windows PowerShell 5.1**. Placer le manifeste à côté de la racine inventoriée. La liste porte uniquement sur des fichiers et utilise des chemins relatifs, afin de pouvoir déplacer le dossier sur un autre lecteur.

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

Après la restauration, adapte le chemin de la racine et du manifeste. Vérifie les fichiers avant d’ouvrir l’IDE ou de lancer une nouvelle compilation.

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

Ce contrôle couvre tous les fichiers du manifeste. Pour auditer des fichiers ajoutés sur le support entre-temps, dresser un deuxième inventaire et comparer aussi les listes de chemins. Garder le premier manifeste sur un support distinct de la copie dont il protège l’intégrité.

## 8. Relire un lien dans dix ans

Une MAP issue d’une compilation est une photographie de ce que le linker a résolu. L’ELF garde les sections et les symboles utiles au diagnostic. **Un exemple issu des essais sur cette archive** illustre la différence entre la bibliothèque, la RAM et la ROM.

| Exemple observé dans des MAP de cette révision | Fournisseur ou placement constaté | Conséquence pour l’archive |
|---|---|---|
| C5 et C6 PCM12, `phy_param` | `libphy.a(phy_init.o)` du **SoC correspondant** | Les deux noms de symboles sont identiques. Chaque archive a une empreinte différente et ses données prennent leur place dans son propre ELF. |
| C5 et C6 PCM12, `RFChannelSel` | `libphy.a(phy_api.o)` sur les deux cibles | Un prototype sauvegardé ne restitue pas le code objet. |
| C5 PCM12, `phy_start_tx_tone_step` | `libphy.a(phy_reg.o)` | Dépendance concrète à la bibliothèque qui a été liée dans l’image. |
| ESP32 Classic PCM12, `g_phyFuns` | Section `.bss.g_phyFuns` de `libphy.a(phy_chip_v7.o)` | C’est un objet de données dont la valeur est établie à l’exécution. Une adresse d’ELF ne remplace pas l’initialisation de la table. |
| ESP32 Classic, `rom_set_rf_freq_offset` | Définition absolue `PROVIDE(...)` dans la MAP observée | Le script ROM et la révision matérielle doivent correspondre. |
| ESP32-C6, `rom_chip_i2c_readReg` | Valeur absolue affectée par le script du lien | Vérifier le symbole dans le script C6 du SDK archivé et la même définition dans la MAP. |

**Retrouver une dépendance manquante.** Prendre le journal de la cible en échec, noter le premier symbole non résolu, ouvrir la MAP de référence et chercher ce nom. La zone *Archive member included to satisfy reference* identifie la sélection d’un objet. La carte de placement et la table de références indiquent la section et le fournisseur retenus. Les sections éliminées sont signalées séparément. Comparer ensuite le fichier `.a` par son empreinte, la liste des membres, le nom C/C++ exact, les gardes de configuration et les arguments de la commande de lien.

Exemple local pour le C5 PCM12 après installation des deux paquets sur Windows. Les chemins ci-dessous utilisent le dossier standard du gestionnaire de cartes.

```powershell
$kit = Join-Path $env:LOCALAPPDATA 'Arduino15'
$tool = Get-ChildItem "$kit\packages\esp32\tools" -Filter 'riscv32-esp-elf-nm.exe' -File -Recurse |
    Select-Object -First 1
$lib = Get-Item "$kit\packages\esp32\tools\esp32c5-libs\3.3.12\ld\libphy.a"
& $tool.FullName -A -g --defined-only $lib.FullName |
    Select-String 'RFChannelSel|phy_param|phy_start_tx_tone_step'
```

Pour Classic, prendre `xtensa-esp32-elf-nm.exe` et le paquet `esp32-libs`. Pour ESP8266, prendre `xtensa-lx106-elf-nm.exe` et la bibliothèque du sous-dossier NONOS exact. Dans `nm`, `U` marque une référence indéfinie dans l’objet inspecté. `A` désigne une définition absolue, `T` du code, `B` des données non initialisées. Lire les sections effectives avec `readelf -h -S -s`, et les relocalisations d’un objet ou d’une archive avec `readelf -r`. La lettre d’un symbole, une adresse seule et l’apparence d’un nom sont chacune une preuve partielle.

Comparer ensuite les ELF témoin et reconstruit avec `objdump -h` et `objdump -d -C` du même préfixe de cible. Contrôler en priorité les routines marquées IRAM, les appels des ISR, les tables en DRAM, les alignements, les routines assembleur et les limites de RAM. Suivre aussi les flags passés au compilateur, notamment `-O2`, les options d’ABI et de flottants, le ramasse-miettes des sections et tout usage de LTO **tel qu’indiqué par le journal de cette cible**. Le C et le C++ doivent retrouver les mêmes noms et le même ABI aux frontières `extern "C"`.

### Reconstruire si le programme Arduino CLI lui-même devient inutilisable

Restaurer d’abord la version conservée de l’exécutable et de son système hôte. Si seuls les compilateurs et les SDK restent exécutables, reprendre la recette ci-dessous. Les chemins dans les journaux peuvent pointer vers des dossiers temporaires. Conserver le nom exact du sketch et les entrées de la compilation de référence.

1. **Reconstituer l’unité de compilation.** Prendre un seul `.ino`, les fichiers adjacents et l’arbre `src/` du bon dossier. Arduino a généré `<sketch>.ino.cpp` avec les includes, les éventuels prototypes et les directives de lignes. Ce fichier exporté est un point de départ vérifiable pour faire compiler la même traduction C++ sans le préprocesseur Arduino. Pour une modification ultérieure du sketch, il faudra régénérer ces déclarations ou entretenir une traduction source C++ équivalente.
2. **Récupérer la commande de chaque langage.** Lire `compile_commands.json` et le journal verbeux. Remapper uniquement les anciennes racines de chemin vers la copie restaurée. Garder l’outil C pour les `.c`, l’outil C++ pour les `.cpp`, et le préprocesseur assembleur pour les `.S`. Conserver la variante de puce, les macros, les includes, les options d’architecture et le niveau d’optimisation de **chaque** unité. Les options de la dernière commande ne décrivent pas toutes les précédentes.
3. **Conserver les fichiers-réponses que les commandes ouvrent avec `@`.** Pour chacun des trois ESP32, le paquet SDK 3.3.12 contient un répertoire `flags/` avec `S_flags`, `c_flags`, `cpp_flags`, `defines`, `includes`, `ld_flags`, `ld_libs` et `ld_scripts`. Copier aussi `include/`, `ld/`, `lib/`, `bin/`, les sous-répertoires de mode de flash, `sdkconfig` et `versions.txt`. Le fichier local `build_opt.h` est consommé comme liste d’options dans les projets concernés. L’extension `.h` peut donc désigner des options de compilateur. Le journal indique son usage précis.
4. **Reconstruire les objets du cœur et les bibliothèques Arduino sélectionnées.** Lire les recettes du `platform.txt`, notamment les hooks précédant le build, la compilation, la création des archives, le lien et la conversion en image. Les bibliothèques du projet s’ajoutent aux archives déjà précompilées du SDK. `compile_commands.json` décrit des commandes de compilation. Le journal verbeux contient aussi le lien et les étapes de formation du binaire. Il faut disposer des deux.
5. **Refaire le lien avec le plan mémoire d’origine.** Reprendre la commande de lien, l’ordre des arguments, les groupes `--start-group/--end-group` et les scripts `-T` réellement présents. Sur ESP8266, garder les scripts générés du type `local.eagle.flash.ld` et `local.eagle.app.v6.common.ld`. Sur C5/C6/Classic, le fichier `flags/ld_scripts` cite plusieurs fichiers ROM, des scripts de mémoire et de sections. Un fichier `*.rom.ld` seul ne définit pas tout l’espace d’adresses.
6. **Reproduire la construction de l’image.** Récupérer les hooks, le convertisseur ELF → image, le schéma de flash et les composants du boot. Archiver aussi, lorsqu’ils sont produits, `flash_args`, `boot_app0.bin`, `partitions.csv`, `sdkconfig`, `build_opt.h` et `file_opts`. Les offsets, la version minimale de silicium, le mode de flash et la place de chaque segment doivent venir de la cible observée. Un offset valable pour le Classic n’est pas une règle générale pour C5 et C6.
7. **Prouver ce que l’on a retrouvé.** Comparer les commandes, les sections, les bibliothèques d’origine et les symboles critiques avant de comparer les captures de protocole ou les mesures de banc. Si le SDK manque entièrement, cette recette indique exactement quelle pièce retrouver et à quelle étape elle devient indispensable.

Quelques empreintes des **scripts de symboles** du paquet 3.3.12, recalculées sur les fichiers distribués. Ces repères facilitent l’identification d’une ancienne sauvegarde. Conserver le répertoire complet de scripts et l’ordre dans lequel le linker les charge.

| Puce | SHA-256 de `ld/<puce>.rom.ld` | SHA-256 de `ld/<puce>.rom.api.ld` |
|---|---|---|
| `esp32` | `0a5891603311f260fdf9732cea8e58b4aa6fb118b8b5732c44caf2068ad61ae8` | `e0438d826affdb8a0b62d0e8d448fa36c598647eff068b5f668e0ba9a2a055d4` |
| `esp32c5` | `53bcf1ce4a410e46af398d22f2059e2632fae6c708537815d6da819c284fb56f` | `1f1f8203425d23d508e0fae880d0b15fbf345e104b4278e34007af8d577c2b5f` |
| `esp32c6` | `e0a0d43e31deeeaca4b967a02fb40faaa2f099d9a2f8d322c4a02b5f6f36c7df` | `6c8bfc83dda3e441dc227bd81cd6fcf07d42371af4934b2061fd9994b825872b` |

### Si l’écosystème a changé

| Situation en 2036 | Marche à suivre | Critère de réussite |
|---|---|---|
| Sites ou index indisponibles | Restaurer le ZIP de l’IDE, le dossier téléchargé, les sources et la copie installée de `Arduino15`. Choisir le profil de la cible et cliquer sur **Vérifier** hors ligne. | Nouveaux binaires issus des sources locales et journal de compilation conservé. |
| Windows actuel ne lance plus un outil ancien | Restaurer l’image x64 de référence dans un hyperviseur compatible. Comparer d’abord le kit par SHA-256. | La chaîne historique produit de nouveau le jeu d’artefacts témoin. |
| Un futur SDK ne publie plus un symbole | Réaliser d’abord le build avec l’ancien paquet conservé. Faire le portage dans une copie séparée. Identifier quelle archive, quel objet et quel contrat étaient utilisés. | Équivalence documentée du fournisseur et de l’ABI, puis reprise des tests matériels si le binaire change. |
| Un en-tête privé ou le type d’une structure a changé | Comparer les versions de `sdkconfig.h`, de HAL, les macros et la représentation des structures sur la cible. Vérifier tailles, alignements, signatures, unités et attributs de section. | Le contrat compilé coïncide avec le fournisseur de l’implémentation. |
| Seule une ancienne image existe | Archiver cette image et tout ELF/MAP encore disponible. Identifier son type, son modèle de puce et son partitionnement avec les outils versionnés. Rechercher les archives d’origine dans les copies de travail et les supports. | Documenter ce qui a été récupéré ; la reconstruction à partir des sources reste ouverte tant que les dépendances essentielles manquent. |
| Le même code compile avec un SDK nouveau mais le comportement diffère | Comparer les deux jeux ELF/MAP, les configurations effectives et le code généré dans les boucles dépendant du temps. Utiliser les traces d’entrée/sortie sauvegardées. | Comparaison séparée du protocole, des échéances temps réel et des mesures du banc. |

## 9. Lire les symptômes sans détruire le témoin

| Symptôme | Premier contrôle concret |
|---|---|
| `Missing FQBN`, `platform not installed`, tentative de joindre un registre | Vérifier la sélection de la carte, les deux URL, les index locaux et les archives de `%LOCALAPPDATA%\Arduino15\staging\packages`. Confirmer les versions installées dans le gestionnaire de cartes. Pour ESPHome, vérifier sa propre installation. |
| `multiple definition of setup/loop` | Dans `WIFIOnlyTest`, compter **un seul** .ino dans le dossier de compilation. Pour le YAML, regarder les aliases, les options `--wrap` et le `build_src_filter` généré. |
| `undefined reference` vers `c5_*`, `e32_*`, `cb_fm_stereo_run` | Vérifier le répertoire local `src/`, les fichiers adjacents pour AM/SSB C5 et la compilation des `.S`. Contrôler le premier fichier objet absent de la ligne de liaison. |
| `undefined reference` vers `phy_*`, `ram_*` ou `rom_*` | Retrouver le fournisseur via la MAP de référence. Vérifier la cible du paquet de bibliothèques, la somme de `libphy.a`, les scripts ROM et les conditions `CONFIG_*`. |
| Un `#error` exige la cible, la version, une horloge fixe ou une configuration USB | Relire le test exact, le FQBN résolu et le fichier de configuration. Le test documente une hypothèse technique. Le supprimer masque le diagnostic sans valider l’ABI. |
| `region ... overflowed`, erreur IRAM ou partition application trop petite | Inspecter les tailles de sections et la table de partitions. Sur D1 mini, vérifier notamment `mmu=4816` et `eesz=4M1M`. Sur les trois C5 autonomes, vérifier le profil 8 Mo du banc. |
| Le programme démarre, mais n’émet pas les trames attendues sur le port série | Associer le bon dossier HTML à la variante, vérifier UART0 vs CDC, le débit, la parité et les bits de stop. Comparer l’identité protocolaire au chapitre 5 avant d’incriminer le compilateur. |
| Un build réussit mais présente des pertes d’échantillons ou de la gigue | Vérifier l’horloge effective, les hypothèses du watchdog, les interruptions, l’emplacement des routines et constantes, le code assembleur et les durées de service. Comparer avec des captures d’un matériel de même révision. |
| Deux binaires issus des mêmes sources ont des SHA-256 différents | Comparer d’abord le profil, la version exacte des outils, les headers générés, les chemins, les horodatages, les sections et les informations de version. L’identité bit à bit demande une recette dédiée ; elle est plus stricte que le succès de compilation ou la compatibilité protocolaire. |
| Le YAML change de backend ou ne retrouve plus ses stubs | Relire `platformio.ini`, `main.cpp` et la version du générateur témoins. Reconstruire dans le kit historique. Une modification de l’indentation ou des noms générés peut changer le programme même si le texte C++ semble intact. |

**Validation en plusieurs étages.** (1) Existence et identité de toutes les entrées. (2) Recompilation et édition de liens. (3) Inspection de l’ELF, des images et des partitions. (4) Test de protocole avec les données et pages correspondantes. (5) Mesure du matériel sur un banc adapté, dans les mêmes conditions que le témoin. Un échec à l’un de ces étages ne se diagnostique pas par le succès d’un autre. Pour les projets dont les notes indiquent « pas testé sur matériel », préserver cette mention lors d’une future qualification.

## 10. Références à conserver avec les fichiers

Les adresses ci-dessous indiquent la provenance des règles et des paquets. Pour une consultation autonome en 2036, conserver localement des versions datées de la documentation, des fiches techniques, des errata et des licences applicables à chaque carte. Les tableaux ci-dessus et les valeurs de référence se lisent hors ligne.

- Dépôt d’origine à la révision de l’archive. [ESP-AnalogRadio, commit 0cabfa13](https://github.com/kaboom748/ESP-AnalogRadio/tree/0cabfa13ae883506af4aad99d9d34442b978aca3)
- Règles des fichiers adjacents et de la compilation récursive de `src/`. [Arduino, spécification des sketches](https://docs.arduino.cc/arduino-cli/sketch-specification/)
- Moteur de compilation de l’IDE et cœur ESP8266. [Arduino IDE 2.3.11](https://github.com/arduino/arduino-ide/releases/tag/2.3.11) · [Arduino CLI 1.6.0](https://github.com/arduino/arduino-cli/releases/tag/v1.6.0) · [ESP8266 Arduino 3.1.2](https://github.com/esp8266/Arduino/releases/tag/3.1.2)
- Mémoire et options du cœur ESP8266. [Répartition cache/IRAM](https://arduino-esp8266.readthedocs.io/en/3.1.2/mmu.html) · [Options de carte et du NONOS SDK](https://arduino-esp8266.readthedocs.io/en/3.1.2/ideoptions.html)
- Paquet Arduino-ESP32, bibliothèques distribuées et définition des cartes. [Version 3.3.12](https://github.com/espressif/arduino-esp32/releases/tag/3.3.12) · [boards.txt de la même révision](https://github.com/espressif/arduino-esp32/blob/3.3.12/boards.txt)
- ESPHome et backend de cette édition. [ESPHome 2026.10.1](https://pypi.org/project/esphome/2026.10.1/) · [Sources du générateur](https://github.com/esphome/esphome/tree/2026.10.1) · [PlatformIO Espressif8266 4.2.1](https://github.com/platformio/platform-espressif8266/tree/v4.2.1)
- Démarrage, calibration et rôle de la flash sur Classic. [Bootloader IDF 5.5](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/api-guides/bootloader.html) · [Calibration RF IDF 5.5.5](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32/api-guides/RF_calibration.html)
- Interprétation de la MAP et sources de variation entre deux binaires. [MAP du linker IDF 5.5.5](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32/api-guides/performance/size.html) · [Reproductibilité IDF 5.5.5](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32/api-guides/reproducible-builds.html)


## Annexe — binaires de référence des 51 compilations Arduino

Ces empreintes proviennent des images d’application compilées sous Linux avec Arduino CLI 1.4.1 et les noms de sketches d’origine. Les numéros correspondent au chapitre 5. Date, chemins et version de l’outil peuvent modifier le binaire d’un build valide. Conserver également les empreintes des binaires produits par ton poste Windows.

| Nº | Famille | Image d’application, octets | MAP, octets | SHA-256 de l’image Linux |
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
