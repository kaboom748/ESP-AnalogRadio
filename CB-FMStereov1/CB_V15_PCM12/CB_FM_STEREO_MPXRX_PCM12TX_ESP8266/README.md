# ESP8266 — réception MPX et émission PCM12

Ce firmware s’utilise avec le HTML universel. Il annonce ses formats à la connexion. En réception, l’ESP8266 envoie le multiplex au navigateur à 120 kéch/s en HQ, 96 kéch/s en ROBUST et 48 kéch/s en MONO. Le navigateur décode la stéréo et, en HQ, le RDS. En émission, le navigateur envoie du PCM stéréo 12 bits à 36 kéch/s par canal. L’ESP8266 génère le multiplex FM à 120 kéch/s. Le port série utilise 1,5 Mbaud en 8N1.

Les essais croisés avec le C5 ont confirmé les deux sens à 2 442,3125 MHz et 2 472,3125 MHz. Les deux transmissions de 900 trames PCM12 sur l’ESP8266 se sont terminées sans sous-alimentation active et ont été suivies d’un retour automatique en réception MPX. Le décodeur du HTML a restitué les canaux gauche et droit à partir des captures radio MPX de l’ESP8266.

Ouvrir `CB_FM_STEREO_MPXRX_PCM12TX_ESP8266.ino` avec Arduino ESP8266 3.1.2. Choisir LOLIN(WEMOS) D1 mini, CPU 160 MHz, flash DIO 4 Mo et 48 Kio d’IRAM avec 16 Kio de cache. L’image compilée dans `bin/` se flashe à `0x0`.

```text
arduino-cli compile --fqbn esp8266:esp8266:d1_mini:xtal=160,eesz=4M1M,mmu=4816 CB_FM_STEREO_MPXRX_PCM12TX_ESP8266
```
