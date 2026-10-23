#pragma once

#include <stdint.h>
#include "mhn_config.h"
#include "features.h"

/*
    "Learn normal": accumulate per-band mean and variance (Welford) over a
    30-60 s window of the machine running normally, then freeze it as a
    baseline. The baseline struct is plain-old-data so it can be persisted
    verbatim to NVS/flash (see hal/storage.*).
*/

#define MHN_BASELINE_MAGIC 0x4D484E31u   /* 'MHN1' */

typedef struct {
    uint32_t magic;
    uint32_t frames;                      /* frames averaged (0 => invalid)      */
    float band_mean[MHN_NUM_BANDS];
    float band_var[MHN_NUM_BANDS];        /* variance, floored to avoid div-by-0 */
    float rms_mean, rms_var;
    float centroid_mean, centroid_var;
    float tone_hz[MHN_NUM_TONES];         /* dominant tonal peaks when learned   */
} baseline_t;

/* Running accumulator (not persisted). */
typedef struct {
    uint32_t n;
    float band_mean[MHN_NUM_BANDS], band_m2[MHN_NUM_BANDS];
    float rms_mean, rms_m2;
    float centroid_mean, centroid_m2;
    float tone_hz_sum[MHN_NUM_TONES];
    uint32_t tone_n;
} baseline_acc_t;

void baseline_begin(baseline_acc_t *acc);
void baseline_add(baseline_acc_t *acc, const features_t *f);
/* Fold the last-seen tonal peaks in (call once per frame with tonal result). */
void baseline_add_tones(baseline_acc_t *acc, const float *tone_hz, int n);
void baseline_finish(const baseline_acc_t *acc, baseline_t *out);

int  baseline_valid(const baseline_t *b);
