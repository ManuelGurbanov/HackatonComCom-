# Hardware notes, assumptions, and what you MUST verify

This project targets the repo's custom ESP32-S3 hackathon board. Pinout and
driver behaviour come from `.claude/skills/hackathon-board/SKILL.md`,
`example/`, and `truco/`. Nothing here was invented or guessed; where the board
is genuinely unknown it is marked **[verify]**.

## Pins used (from `board.h`, unchanged)

| Function | GPIO |
|---|---|
| Speaker I2S (MAX98357A) DIN/BCLK/LRCLK | 4 / 5 / 6 |
| PDM mic CLK / DOUT | 7 / 15 |
| OLED SSD1306 SPI SCL/SDA/RST/DC/CS | 36 / 37 / 40 / 39 / 38 |
| Joystick UP/DOWN/LEFT/RIGHT | 9 / 11 / 12 / 10 |
| KEY1..KEY4 | 14 / 21 / 47 / 48 |

The joystick is mounted rotated 45 degrees; we reuse `truco/`'s software fix
(`hal/buttons.c`). Flip it with menuconfig `HACK_JOYSTICK_ROT_CCW` if directions
feel swapped. Joystick CENTER (GPIO13) is **not wired** and is unused here.

## Display is MONOCHROME

The SSD1306 is 128x64, 1 bpp. The spec asked for a "state color"; that is not
possible on this panel. Instead the node shows state as **text** (`OK` /
`WARNING` / `ALERT`), a filled vs. hollow health bar, and an **inverted banner**
on ALERT. Color lives only on the gateway's web dashboard (a real screen).

## Microphone caveats — verify before trusting any number

The mic is a cheap PDM MEMS part. The following are **unknown / [verify] on real
hardware** and directly affect detection quality:

- **Frequency response.** PDM MEMS mics roll off and have resonances; the band
  energies are *relative to this response*. Because we compare live vs. a learned
  baseline captured through the same mic, a flat response is **not** required —
  but a tone the mic cannot hear cannot be detected. **Verify** the usable band
  by playing a sweep.
- **Gain / sensitivity.** Absolute RMS dB is arbitrary (we do not know the mic's
  dBFS calibration). The baseline learns its own level, so scoring is
  self-referential and fine; do **not** report absolute SPL.
- **Clipping near loud machines.** A loud pump/compressor may clip the ADC.
  Clipping flattens the spectrum and will corrupt both baseline and scoring.
  **Verify** headroom at the real install distance; if `rms_db` sits near 0 dBFS,
  move the node back or reduce gain. (A clip detector is a good next addition.)
- **Mono, single channel.** We average the two PDM channels; one may be silent
  depending on the mic wiring. **[verify]** which channel carries signal.
- **Mounting / wind / HVAC airflow** add low-frequency rumble; we ignore < 60 Hz,
  but strong airflow on the mic port still raises the floor. Use a foam cover.

## What is honestly good vs. not

- **Good at:** steady machines with a stable acoustic signature, catching
  *relative* change — a new rattle, a shifted hum, a raised broadband floor,
  belt squeal. This is the screening use case.
- **Weak / not claimed:** early bearing wear (often ultrasonic, and structure-
  borne — a contact accelerometer wins), noisy shared spaces where other
  machines dominate, and any **absolute** fault classification. We detect
  "different from normal", not "this is a spalled inner race".

## Claims that need real data (do not state these without measuring)

- Any detection-rate / false-alarm-rate percentage.
- Lead time before failure.
- Minimum detectable fault size.
- Max useful distance from the machine.

The included tests use **synthetic** signals and only prove the DSP responds in
the right direction. Collect real recordings (normal + seeded faults) per machine
class before quoting numbers.

## Networking reality

- ESP32-S3 has Wi-Fi (2.4 GHz) and BLE but **no 802.15.4 radio**, so Zigbee/
  Thread and LoRa are out (matches `preguntas.md` Q1). We use **ESP-NOW** node->
  gateway and Wi-Fi SoftAP for the dashboard; both share a fixed channel (1).
- ESP-NOW range is roughly room/line-of-sight scale (tens of metres indoors,
  **[verify]** in the real building). For a large plant you would add repeaters
  or an ESP-NOW mesh; not implemented here.
