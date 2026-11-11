# CB Duplex Voice V3 — ESP8266 sketch

**The radio firmware logic is identical to CB Duplex Voice V2.** Only the
sketch filename/folder name is changed for the V3 package.

The RF & Audio Diagnostics suite is implemented in the browser and uses the
existing signed PCM8 TDD transport with the DPX1 framing. If you already have
the tested V2 firmware flashed, you may keep it and use the new V3 HTML.

If reflashing: compile for ESP8266 at **160 MHz**, respecting your original
board and USB serial configuration. Both stations must use the same frequency,
slot data and guard samples. Do not exceed APWR 191. Read the HTML README for
step-by-step test instructions, interpretation and limitations.
