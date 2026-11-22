# CB Duplex Voice V5 firmware for ESP8266

Open [CB_DuplexVoice_V4_Arduino.ino](CB_DuplexVoice_V4_Arduino.ino) with the ESP8266 board package and set the CPU to **160 MHz**. Open the [V5 EASY browser app](../CB_DuplexVoice_EASY_HTML/) and connect over **460,800 baud, 8N2**.

The V5 download keeps the V4 filename because the sketches in V4 and V5 are identical. An ESP8266 already running that firmware can switch to the V5 web interface without reflashing. The browser handles companding and the playback notch. The radio works on 2.4 GHz. See the [V5 manual](../) for the station settings and troubleshooting.
