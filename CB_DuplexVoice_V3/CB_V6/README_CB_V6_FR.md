# CB_V6 — projet complet

Cette archive contient le firmware Arduino ESP8266 et l'application PC **Easy + Advanced** de CB_V5, modifiés pour utiliser **921 600 bauds 8N2** et décoder les **deux créneaux RF à pleine cadence** lors d'une écoute passive.

**Démarrage :** flasher `CB_DuplexVoice_V6_Arduino/CB_DuplexVoice_V6_Arduino.ino` sur chaque ESP8266, régler le CPU à **160 MHz**, puis ouvrir `CB_DuplexVoice_EASY_HTML/index.html` (ou `CB_DuplexVoice_V6_STANDALONE.html` dans le même dossier) avec Chrome/Edge de bureau. Choisir le même canal/TDD/encodage sur les postes. L'ancien firmware à 460800 bauds ne peut pas se connecter au logiciel V6.

**TDD 20/8 :** émission/réception active 30 000 échantillons/s ; réception passive 30 000 **par interlocuteur** en entrée et 30 000 sur le flux **mixé** en sortie. Deux flux passifs représentent 60 000 échantillons/s sur l'UART. La modulation radio et la bande audio filtrée Music (~5 kHz) restent inchangées.

**Validation :** voir `CB_DuplexVoice_EASY_HTML/V6_TEST_RESULTS.txt` et `README.md` pour les détails et limites. Les tests électroniques sur vraies cartes ESP8266 et la compilation avec SDK ESP8266 ne sont pas effectués dans l'environnement de création : valider le débit et les pertes en pratique avant usage prolongé.
