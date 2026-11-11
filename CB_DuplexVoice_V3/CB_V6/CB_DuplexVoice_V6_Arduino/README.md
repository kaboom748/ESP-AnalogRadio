# CB Duplex Voice V6 — firmware ESP8266

Ouvrir **`CB_DuplexVoice_V6_Arduino.ino`** dans l'Arduino IDE, configurer **ESP8266 / CPU 160 MHz** puis flasher **tous** les modules utilisés avec la page Web V6.

Le port série est maintenant **921600 bauds, 8 bits, aucune parité, 2 bits de stop (8N2)**. L'application Web V6 sélectionne automatiquement ces paramètres et attend la réponse `DUPLEX 1 921600 1`.

## Différences avec V5

- `START L` émet les **échantillons de tous les créneaux**, sans décimation 1 sur 2 ; `START A/B` restent inchangés côté RF et PCM logique.
- Les créneaux manquants sont complétés jusqu'à `cfg.data` échantillons pour conserver un index audio multiplexé cohérent.
- Validation du débit série tenant compte de deux canaux en mode `L`, du format DPX1 et de 4% de marge.
- La file des paquets de réception passe à **16 trames** (au lieu de 8) pour une durée de mise en tampon similaire malgré le débit doublé.

Le format `DPX1` (146 octets/trame et 128 octets de charge utile), les marqueurs RF, le délai de garde, l'APWR (0..191) et le cadencement TDD sont conservés.

**Important :** la page Web et le firmware V6 doivent être utilisés ensemble. Un ancien firmware qui utilise 460800 bauds ne répondra pas à l'application V6. Les cartes ESP8266 sont requises pour tester la stabilité réelle à 921600 bauds, notamment les statistiques UART et les pertes de trames.
