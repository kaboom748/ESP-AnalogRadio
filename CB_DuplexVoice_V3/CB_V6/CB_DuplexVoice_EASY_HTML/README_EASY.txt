CB DUPLEX VOICE V6 — EASY + ADVANCED
===================================

CHANGEMENT V6 : 921600 baud 8N2 et écoute passive à pleine cadence pour
CHAQUE interlocuteur. Avec TDD 20/8 : 30000 échantillons/s par voix en écoute
passive, mélangées en une sortie à 30000 échantillons/s ; 30000 en écoute active.

INSTALLATION
------------
1. Flasher CB_DuplexVoice_V6_Arduino.ino sur TOUS les ESP8266 (160 MHz).
2. Ouvrir index.html dans Chrome/Edge bureau ; ou utiliser
   CB_DuplexVoice_V6_STANDALONE.html ; si besoin lancer un serveur localhost.
3. Régler la même fréquence, les timings TDD et l'encodage mu-law sur toutes
   les stations. Réglages 20/8 recommandés pour débuter.
4. Cliquer PORT (connecter), puis ANSWER (MASTER) sur une station.
5. Cliquer CALL sur une deuxième station pour la joindre en Slave.
6. Les autres stations peuvent écouter les deux voix en mode passif.
7. HANG UP revient en passif ; PORT permet de déconnecter.

PARAMÈTRES PAR DÉFAUT
---------------------
RF 2442312 kHz (CH 07) ; presets CH 01 2412312, CH 11 2462312 kHz.
APWR 63 (0 plus fort, 191 plus faible) ; TDD 20 data / 8 guard.
Music/Raw ; encodage Medium mu-law 63 ; filtre d'harmoniques TDD activé.
Gain RX RF 0, VGA 0, baseband 16 ; volume HP 100%, micro 0 dB.

COMMANDES EASY
--------------
CALL : rejoindre comme Slave (Master requis).
ANSWER : lancer une nouvelle session Master.
HANG UP : terminer la session active et repasser en écoute passive.
PORT : ouvrir ou fermer le port série ESP8266.
CHANNEL : changer de canal (01, 07, 11) avant connexion.
VOLUME : clic +5%, molette +/-5% ; MIC GAIN : -18 à +18 dB.
FREQUENCY : réglage fin kHz ; RX MODE : Off / Medium / Strong.
TX POWER : puissance APWR à ajuster port déconnecté.
AUDIO : sélectionner microphone et haut-parleurs.
DTMF : 12 touches ; ton local et RF pendant les appels ; 90 ms minimum,
       50 ms de séparation. Le signal DTMF remplace temporairement le micro.

MODE ADVANCED ET LIMITES
------------------------
Le tableau de bord, le laboratoire RF/audio, l'export CSV/WAV, la façade et
les filtres V5 sont préservés. L'indicateur LCD S estime la synchronisation
mais n'est pas un RSSI RF calibré. Les commandes POWER/frequence/mu-law
nécessitent une déconnexion avant modification.
Le taux d'échantillonnage égal ne garantit pas la qualité audio identique :
le mixage des deux voix, les filtres 5 kHz et les pertes radio restent présents.
921600 bauds sur USB-UART et la stabilité du récepteur RF ne sont pas validés
sur du matériel dans ce paquet : voir V6_TEST_RESULTS.txt.
