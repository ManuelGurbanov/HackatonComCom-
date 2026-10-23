#pragma once

#include "mhn_config.h"
#include "features.h"

/*
    Tonal tracking. Mechanical faults very often add a new tone or shift an
    existing one (imbalance moves 1x running speed, a loose part rattles at a
    new frequency, belt squeal adds a high partial). We find the strongest
    tonal peaks each frame and compare them to the peaks present when the
    baseline was learned.
*/

typedef struct {
    float hz[MHN_NUM_TONES];
    float mag[MHN_NUM_TONES];
    int   count;
} tones_t;

/* Pick up to MHN_NUM_TONES dominant peaks from a magnitude spectrum. */
void tonal_find(const float *mag, tones_t *out);

/* Penalty (in z-score units, >= 0) for tones that are new or shifted
   relative to the baseline tone list. 0 means "tonal content unchanged".
   `tolerance_hz` is how far a peak may drift before it counts as moved. */
float tonal_penalty(const float *baseline_hz, int baseline_count,
                    const tones_t *live, float tolerance_hz);
