#pragma once

/*
    Shared, hardware-independent configuration for the "machine health
    listening node" DSP. Everything here is plain C with no ESP-IDF
    dependency so the same numbers drive the on-device build and the PC
    unit tests (see health-node/test/).

    Sample rate choice: 16 kHz.
      - The PDM mic driver on this board already runs at 16 kHz.
      - Nyquist is 8 kHz, which covers the mechanical phenomena we care
        about: line hum and its harmonics (50/60, 100/120 Hz ...), motor
        and fan blade-pass tones, belt squeal, bearing rattle sidebands.
        Most diagnostic airborne energy for this class of machines lives
        below a few kHz; ultrasonic bearing signatures are NOT reliably
        captured by a cheap PDM mic anyway (see docs/HARDWARE.md).
      - 16 kHz halves RAM and CPU versus 48 kHz, which matters in the
        no-dropped-frames audio loop.
*/

#define MHN_SAMPLE_RATE_HZ   16000

/* FFT size. 512 @ 16 kHz -> 32 ms frames, 31.25 Hz bin width. Power of two. */
#define MHN_FFT_SIZE         512
#define MHN_FFT_BINS         (MHN_FFT_SIZE / 2 + 1)   /* usable real bins, incl. DC & Nyquist */

/* Log-spaced analysis bands used as the compact feature vector. */
#define MHN_NUM_BANDS        24
#define MHN_BAND_LO_HZ       60.0f     /* ignore DC / wind rumble below this   */
#define MHN_BAND_HI_HZ       7800.0f   /* stay just under Nyquist              */

/* Number of tonal peaks tracked for the "new/shifted tone" detector. */
#define MHN_NUM_TONES        4

/* Default smoothing / hysteresis (overridable at runtime via sensitivity). */
#define MHN_SCORE_EMA_ALPHA  0.15f     /* health-score smoothing (0..1)        */
#define MHN_WARN_SCORE       70        /* <= -> WARNING                        */
#define MHN_ALERT_SCORE      45        /* <= -> ALERT                          */
#define MHN_HYSTERESIS       6         /* score points needed to recover a state */
