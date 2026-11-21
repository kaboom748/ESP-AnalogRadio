CB DUPLEX VOICE V6 — EASY + ADVANCED
===================================

CB V6 prend en charge les firmwares ESP8266, ESP32, ESP32-C5 et ESP32-C6.
Le pont USB–UART externe fonctionne à 921600 bauds en 8N2. Le profil TDD 44/32
donne 30000 échantillons/s par voix et une sortie mono mixée à 30000 en écoute.

INSTALLATION
------------
1. Flasher le firmware du modèle de chaque ESP. Utiliser 160 MHz sur ESP8266.
2. Ouvrir index.html ou CB_DuplexVoice_V6_STANDALONE.html dans Chrome/Edge.
   Utiliser un serveur localhost si le navigateur bloque les worklets.
3. Régler la même fréquence, les timings TDD et l'encodage mu-law sur toutes
   les stations. Commencer avec le profil 44/32.
4. Cliquer PORT (connecter), puis ANSWER (MASTER) sur une station.
5. Cliquer CALL sur une deuxième station pour la joindre en Slave.
6. Les autres stations peuvent écouter les deux voix en mode passif.
7. HANG UP revient en passif ; PORT permet de déconnecter.

PARAMÈTRES PAR DÉFAUT
---------------------
La fréquence par défaut est de 2442312 kHz (CH 07). CH 01 correspond à
2412312 kHz et CH 11 à 2462312 kHz. APWR vaut 63 et le timing vaut 44/32.
Music/Raw, mu-law 63 et filtre d’harmoniques TDD sont activés.
Gain RX automatique, VGA 0, baseband 16, volume 100%, micro 0 dB et IF 78,125 kHz.
L’accord automatique est activé sur les quatre modèles. ANSWER fixe la
fréquence du Master. CALL cherche la porteuse et adopte le décalage retenu.
Après une perte durable, le Slave cherche à reprendre automatiquement.
Dans Current interface, « Initial frequency correction » déplace le point
de départ et « Automatic carrier tuning » permet de désactiver le balayage
avant de connecter le port. La console affiche le décalage appliqué.

PROFIL SIGNAL LINÉAIRE
----------------------
Choisir Signal linear dans Advanced sur les deux stations avant de les
connecter. Il neutralise le compandage, les coupe-bande et l’émission DTMF.
Le flux conserve la place des échantillons effacés et les fait entendre
comme du silence. Le tableau distingue les pertes RF et les trous série.

COMMANDES EASY
--------------
CALL : rejoindre comme Slave (Master requis).
ANSWER : lancer une nouvelle session Master.
HANG UP : terminer la session active et repasser en écoute passive.
PORT : ouvrir ou fermer le port série du récepteur.
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
Le README de la racine du projet décrit les essais matériels du portage.
V6_TEST_RESULTS.txt contient les résultats historiques de la version source.
