# CB Duplex Voice V4

Legacy 2.4 GHz ESP8266 release with the advanced browser interface, reversible amplitude companding and a local TDD harmonic filter.

1. Open the [Arduino sketch](CB_DuplexVoice_V4_Arduino/) for an ESP8266 at **160 MHz**. The serial link uses **460,800 baud, 8N2**. An already working V3 radio sketch can also run this interface.
2. Open the [V4 browser app](CB_DuplexVoice_V4_HTML/) in desktop Chrome or Edge. Its `index.html` and standalone page both provide the advanced interface.
3. Match the frequency, TDD timing and amplitude encoding on all stations. The V4 app starts with **20/8** and **Strong** companding.
4. Choose **Connect & Listen**. Start one station with **PTT** so it becomes Master, then use **PTT** on the other station to join as Slave.

The compander runs on both ends and affects the on-air PCM values. The TDD harmonic filter acts locally on playback. See [V5](../CB_V5/) for the EASY front panel and the [version guide](../) for newer boards.
