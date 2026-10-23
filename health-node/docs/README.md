# Machine Health Listening Node

A low-cost "listening node" for predictive maintenance: an ESP32-S3 sits near a
machine (pump, motor, HVAC fan, compressor, elevator, conveyor), learns what it
sounds like when healthy, and flags drift or new/abnormal sound. Four nodes
report over ESP-NOW to a gateway board that serves a live web dashboard.

> **Honest framing:** an airborne microphone is a *screening* layer, not a
> replacement for a contact vibration sensor. See
> [the limitations in the pitch](PITCH.md#honest-limitations) and
> [HARDWARE.md](HARDWARE.md) for what must be verified on real hardware and
> which claims need real data before you can state them.

## Framework choice: ESP-IDF (not PlatformIO)

The rest of this repo (`example/`, `truco/`) is ESP-IDF v6.1, and the proven
board drivers (`display`, `buttons`, `audio`) are ESP-IDF. Reusing them verbatim
is the fastest path to a working demo, so this project is ESP-IDF too. The
portable DSP has **no** ESP dependency, so it builds and is unit-tested on a PC
with plain `cc` + `make` regardless.

## Repository layout

```
health-node/
  main/
    dsp/    portable C: FFT, features, baseline, scoring, tonal   (PC-testable)
    hal/    board-specific: display, buttons, audio_capture, audio_out, storage
    ui/     OLED screens (score / spectrum / info)
    net/    ESP-NOW transport + gateway Wi-Fi dashboard
    app/    node + gateway application loops
    main.c  entry, boot-time role select
  tools/    gen_test_wavs.py  (synthetic test signals)
  test/     host build + DSP unit test (no hardware needed)
  docs/     this file, HARDWARE.md, PITCH.md
```

## Signal pipeline

1. **Capture** (`hal/audio_capture.c`): PDM mic, 16 kHz mono, DMA. A fixed pool
   of 3 frame buffers (no heap in the loop) delivers 512-sample frames with 50%
   overlap (62.5 frames/s). If the DSP falls behind, the oldest frame is dropped
   and counted rather than stalling the mic.
2. **Features** (`dsp/features.c`): Hann window -> 512-pt FFT -> 24 log-spaced
   bands (60 Hz..7.8 kHz) of log energy, broadband RMS (dB), spectral centroid.
3. **Baseline** (`dsp/baseline.c`): "learn normal" accumulates per-band mean and
   variance (Welford) over ~40 s, plus the dominant tonal peaks, then persists to
   NVS (`hal/storage.c`).
4. **Scoring** (`dsp/scoring.c`): per-band z-score aggregated as a diagonal
   Mahalanobis (RMS z) distance -> 0..100 health, EMA-smoothed, with hysteresis
   between OK / WARNING / ALERT.
5. **Tonal** (`dsp/tonal.c`): tracks the strongest peaks and adds a penalty when
   a tone is new or has shifted beyond tolerance vs. the baseline.
6. **Report** (`net/`): heartbeat + health + compact features over ESP-NOW to the
   gateway, which serves the dashboard.

## Sample rate: why 16 kHz

The PDM mic driver already runs at 16 kHz; Nyquist 8 kHz covers line hum and
harmonics, motor/fan blade-pass tones, belt squeal, and rattle. 16 kHz halves
RAM/CPU vs 48 kHz, which matters for a no-drop audio loop. True ultrasonic
bearing signatures are above this and are not reliably captured by a cheap PDM
mic anyway (see HARDWARE.md).

## Memory / CPU budget (estimate, verify on hardware)

- **Static RAM:** frame pool 3 x 512 x 4 B = 6 KB; FFT scratch (static in
  `features.c`) 2 x 512 x 4 B = 4 KB; `features_t` incl. 257-bin spectrum ~1.2 KB;
  baseline/accumulator < 1 KB. Total working set a few tens of KB, comfortably in
  internal SRAM; no PSRAM required.
- **CPU:** one 512-pt radix-2 FFT + band/feature math every 16 ms. The plain-C
  FFT is the hot spot; it is well within budget at 62.5 frames/s on the S3 at 240
  MHz. If you ever need headroom, swap `fft_radix2` for ESP-DSP `dsps_fft2r_fc32`
  (localized to `features.c`).
- **Heap:** none allocated in the audio/DSP loop (pools + static buffers), so no
  fragmentation there. Wi-Fi/HTTP on the gateway use heap as usual.

## Build & flash (ESP-IDF v6.1)

```bash
. ~/esp-idf-v6.1/export.sh
cd health-node
idf.py set-target esp32s3
idf.py menuconfig        # set "Machine Health Node" -> Node ID (1..4), joystick/display rotation
idf.py -p /dev/ttyACM0 flash monitor
```

Each board can boot into one of three roles:

- **Node** (default): listens to a machine and transmits reports over ESP-NOW.
- **Host** (hold **KEY1** during boot): listens to a machine *and* runs the
  gateway + dashboard locally. Its own machine appears on its own dashboard.
- **Gateway** (menuconfig *Build this board as the gateway by default*):
  dashboard only, no local listening — for setups with 3+ boards.

Connect a phone/laptop to Wi-Fi `MHN-GATEWAY` (pass `machinehealth`) and open
`http://192.168.4.1/`.

### Two-board demo (recommended with only 2 boards)

1. Flash both boards; set a different **Node ID** on each (1 and 2).
2. Boot board 1 normally -> it's a **node** watching machine A.
3. Boot board 2 holding **KEY1** -> it's the **host** watching machine B and
   serving the dashboard.
4. Join `MHN-GATEWAY` Wi-Fi, open `http://192.168.4.1/`, and you'll see **both**
   machines. Learn a baseline on each (KEY1 after boot), then introduce a fault
   on one and watch only that card drop.

With 4 boards, make one a dashboard-only **gateway** and the other three nodes.

## Controls (node)

| Input | Action |
|---|---|
| KEY1 | Learn baseline (records ~40 s of "normal") |
| KEY2 | Mark serviced (reset score/state) |
| KEY3 | Mark false alarm (raises threshold a notch) |
| KEY4 | Cycle screens (main / spectrum / info) |
| Joystick Up / Down | Sensitivity + / - |
| Joystick Left | Toggle demo drift (presentation aid) |
| Joystick Right | Ramp demo drift up while in demo |

## PC unit tests (no hardware)

```bash
cd health-node/test
make           # generates WAVs, builds, runs
```

Learns a baseline from `normal.wav` and asserts the health score stays high for
normal sound and drops (with WARNING/ALERT frames) for `broadband`, `rattle`,
and `shifted` faults. These WAVs are **synthetic** (see `tools/gen_test_wavs.py`)
and only validate the DSP math, not real-world accuracy.

## Demo mode

The synthetic faults above can be played out a speaker near the mic, or use the
on-device **demo drift** (joystick Left to arm, Right to ramp) to drive the score
down on cue. Scripted live demo: run a desk fan, learn the baseline, then tape a
coin to a blade or block the intake and watch the score fall and the alert fire.
Demo drift is clearly labelled a presentation aid, not a measurement.
