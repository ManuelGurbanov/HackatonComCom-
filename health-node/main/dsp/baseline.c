#include "baseline.h"

#include <string.h>

void baseline_begin(baseline_acc_t *acc)
{
    memset(acc, 0, sizeof(*acc));
}

/* Welford online update for one scalar. */
static void welford(float x, uint32_t n, float *mean, float *m2)
{
    float d = x - *mean;
    *mean += d / (float)n;
    *m2 += d * (x - *mean);
}

void baseline_add(baseline_acc_t *acc, const features_t *f)
{
    acc->n++;
    for (int b = 0; b < MHN_NUM_BANDS; b++) {
        welford(f->bands[b], acc->n, &acc->band_mean[b], &acc->band_m2[b]);
    }
    welford(f->rms_db, acc->n, &acc->rms_mean, &acc->rms_m2);
    welford(f->centroid_hz, acc->n, &acc->centroid_mean, &acc->centroid_m2);
}

void baseline_add_tones(baseline_acc_t *acc, const float *tone_hz, int n)
{
    int m = n < MHN_NUM_TONES ? n : MHN_NUM_TONES;
    for (int i = 0; i < m; i++) {
        acc->tone_hz_sum[i] += tone_hz[i];
    }
    acc->tone_n++;
}

void baseline_finish(const baseline_acc_t *acc, baseline_t *out)
{
    memset(out, 0, sizeof(*out));
    out->magic = MHN_BASELINE_MAGIC;
    out->frames = acc->n;
    if (acc->n < 2) {
        return;   /* leave frames small; baseline_valid() will reject it */
    }

    for (int b = 0; b < MHN_NUM_BANDS; b++) {
        out->band_mean[b] = acc->band_mean[b];
        float var = acc->band_m2[b] / (float)(acc->n - 1);
        /* Floor the variance: a perfectly steady band would otherwise make
           any tiny deviation an infinite z-score. 1.0 dB^2 ~= 1 dB sigma. */
        out->band_var[b] = var < 1.0f ? 1.0f : var;
    }
    out->rms_mean = acc->rms_mean;
    out->rms_var = acc->rms_m2 / (float)(acc->n - 1);
    if (out->rms_var < 1.0f) out->rms_var = 1.0f;
    out->centroid_mean = acc->centroid_mean;
    out->centroid_var = acc->centroid_m2 / (float)(acc->n - 1);

    if (acc->tone_n > 0) {
        for (int i = 0; i < MHN_NUM_TONES; i++) {
            out->tone_hz[i] = acc->tone_hz_sum[i] / (float)acc->tone_n;
        }
    }
}

int baseline_valid(const baseline_t *b)
{
    return b && b->magic == MHN_BASELINE_MAGIC && b->frames >= 2;
}
