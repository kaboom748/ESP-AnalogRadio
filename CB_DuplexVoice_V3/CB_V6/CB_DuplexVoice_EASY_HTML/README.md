# CB Duplex Voice V6 — 921 600 bauds + réception passive pleine cadence

Version complète de CB_V5, avec la façade **EASY** et le mode **Advanced**, DTMF, DSP, mesures et trames DPX1 préservés.

## Modification V6

- L'UART entre le navigateur et **chaque** ESP8266 passe de **460 800** à **921 600 bauds**, en **8N2**, sans contrôle de flux.
- `START L` transmet désormais tous les échantillons RF des deux créneaux : **30 000 échantillons/s par interlocuteur** avec le preset TDD `20/8`, contre 15 000 auparavant. La sortie mixée reste **un flux à 30 000 échantillons/s**, pas 60 000.
- Les rôles actifs `START A` / `START B` restent à 30 000 échantillons/s par sens pour `20/8` ; le cadencement radio, les marqueurs, la modulation et le format DPX1 restent inchangés.
- Le DSP utilise des blocs de `dataSamples` (20 par défaut) pour séparer les deux interlocuteurs, au lieu de `dataSamples/2` ; les coefficients RX et le filtre TDD utilisent maintenant la cadence complète.
- La protection de capacité UART tient compte de **deux** flux complets en passif, des en-têtes DPX1, du pacing à **12 bits/octet** et d'une réserve minimale de **4 %**. Le tampon de paquets du firmware passe de 8 à 16 trames.

## Installation

1. Décompresser **CB_V6.zip** ; conserver la structure des dossiers.
2. Dans l'Arduino IDE, ouvrir `CB_DuplexVoice_V6_Arduino/CB_DuplexVoice_V6_Arduino.ino` et programmer **chaque** ESP8266, y compris ceux utilisés uniquement en écoute passive. Configurer le processeur à **160 MHz**. **Le firmware V5 n'est pas compatible avec le débit V6**, et un simple changement du navigateur ne suffit pas.
3. Ouvrir `CB_DuplexVoice_EASY_HTML/index.html` dans Chrome ou Edge de bureau avec Web Serial. Alternative : le fichier autonome `CB_DuplexVoice_V6_STANDALONE.html` (inclut CSS, JavaScript et image de façade), ou héberger le dossier sur `http://localhost` si le navigateur refuse les worklets depuis `file://`.
4. Régler la même fréquence RF, le même timing TDD et la même compression d'amplitude (off/medium/strong) sur toutes les stations. Le preset conseillé reste **20/8**, avec Music/Raw et Medium.
5. Connecter les stations via **PORT**. Faire **ANSWER** sur la station maître, puis **CALL** sur la station esclave ; un troisième récepteur peut demeurer en mode `L` pour écouter les deux à cadence complète.

## Attention / limites

- **921 600 bauds n'augmente pas automatiquement la bande passante acoustique**, puisque les filtres Music/Raw limitent encore le son autour de 5 kHz. Cela retire la perte d'un échantillon sur deux en écoute passive et rend possible une évolution ultérieure des filtres.
- L'écoute passive mélange les deux voies : le signal est à la même **fréquence d'échantillonnage** que chaque réception active, mais la qualité acoustique perçue n'est pas garantie identique (rapport signal/bruit RF, filtres, mélange, pertes et matériel comptent).
- USB-UART, câble USB, ESP8266 et pilote doivent pouvoir soutenir **921 600 bauds 8N2** sans perte. Un débit accepté par le navigateur ne garantit pas le fonctionnement sans erreurs.
- En l'absence de matériel dans cet environnement, les essais sont des **tests logiciels**, pas une certification RF, un essai Arduino compilé avec le vrai SDK, ni un test sur deux cartes ESP8266. Vérifier notamment `UART ERRORS`, `RX DROPS`, `IQ LATE`, pertes DPX1, qualité des aigus et absence de craquements en mode L.
- Le protocole RF lui-même, la puissance, les gardes et l'arbitrage radio ne changent pas. Prototype expérimental : respecter les exigences locales liées aux émissions à 2,4 GHz.

## Fichiers

- `index.html`, `style.css`, `facade.css`, `facade-cb.png`, `facade-ui.js` — interfaces Easy et Advanced.
- `CB_DuplexVoice_V6_STANDALONE.html` — version monofichier complète de la même application (pas l'ancienne version V4 avancée seulement).
- `app.js` — liaison série 921600, calcul de capacité, rôles et interface.
- `voice-dsp.js` — démultiplexage de deux créneaux audio complets et mixage à la cadence nominale.
- `audio-engine.js`, `signal-shaping.js`, `diagnostics.js`, `diagnostics-ui.js` — fonctions audio, mu-law et banc de test conservés.
- `V4_TEST_RESULTS.txt` — archive historique des tests de la V4, qui ne valide pas la V6.
- `V6_TEST_RESULTS.txt` — nouveaux tests de non-régression V6.
