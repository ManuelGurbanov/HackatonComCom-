#pragma once

#include "mhn_config.h"

/*
    Feature extraction: one MHN_FFT_SIZE frame of mono audio -> a compact,
    hardware-independent feature vector. No dynamic allocation; the caller
    owns the frame buffer. Safe to call from a single DSP task.
*/

typedef struct {
    float bands[MHN_NUM_BANDS];   /* log band energy, dB scale (10*log10 power) */
    float rms_db;                 /* broadband level, dBFS-ish (20*log10 rms)   */
    float centroid_hz;            /* spectral centroid                           */
    float mag[MHN_FFT_BINS];      /* magnitude spectrum, kept for UI + tonal     */
} features_t;

/* One-time setup of the Hann window and log-spaced band edges. */
void features_init(void);

/* `frame` holds MHN_FFT_SIZE samples normalized to roughly [-1, 1].
   Fills every field of `out`. */
void features_extract(const float *frame, features_t *out);

/* Bin <-> frequency helpers (handy for UI and tonal tracking). */
float features_bin_hz(int bin);
int   features_hz_bin(float hz);
