#include "tonal.h"

#include <math.h>
#include <string.h>

/* Median magnitude via a coarse copy + partial selection. Used as a noise
   floor so we only keep peaks that truly stick out. */
static float spectral_floor(const float *mag)
{
    /* Mean of magnitudes is a cheap, robust-enough floor for a 257-bin
       spectrum; true median would need a sort we want to avoid per frame. */
    double sum = 0.0;
    for (int k = 1; k < MHN_FFT_BINS; k++) sum += mag[k];
    return (float)(sum / (MHN_FFT_BINS - 1));
}

void tonal_find(const float *mag, tones_t *out)
{
    memset(out, 0, sizeof(*out));
    float floor_mag = spectral_floor(mag);
    float thresh = floor_mag * 4.0f;   /* 12 dB above the average bin         */

    for (int k = 2; k < MHN_FFT_BINS - 2; k++) {
        float m = mag[k];
        if (m < thresh) continue;
        if (!(m > mag[k - 1] && m >= mag[k + 1])) continue;  /* local max      */

        /* Parabolic interpolation for a sub-bin frequency estimate. */
        float a = mag[k - 1], b = m, c = mag[k + 1];
        float denom = (a - 2.0f * b + c);
        float delta = denom != 0.0f ? 0.5f * (a - c) / denom : 0.0f;
        float hz = features_bin_hz(k) + delta * features_bin_hz(1);

        /* Insert into the top-N list sorted by magnitude. */
        for (int i = 0; i < MHN_NUM_TONES; i++) {
            if (m > out->mag[i]) {
                for (int j = MHN_NUM_TONES - 1; j > i; j--) {
                    out->mag[j] = out->mag[j - 1];
                    out->hz[j]  = out->hz[j - 1];
                }
                out->mag[i] = m;
                out->hz[i]  = hz;
                break;
            }
        }
    }
    for (int i = 0; i < MHN_NUM_TONES; i++) {
        if (out->mag[i] > 0.0f) out->count++;
    }
}

float tonal_penalty(const float *baseline_hz, int baseline_count,
                    const tones_t *live, float tolerance_hz)
{
    if (baseline_count <= 0) return 0.0f;

    float penalty = 0.0f;
    for (int i = 0; i < live->count; i++) {
        float f = live->hz[i];
        if (f <= 0.0f) continue;
        float nearest = 1e9f;
        for (int j = 0; j < baseline_count; j++) {
            if (baseline_hz[j] <= 0.0f) continue;
            float d = fabsf(f - baseline_hz[j]);
            if (d < nearest) nearest = d;
        }
        if (nearest > tolerance_hz) {
            /* New or shifted tone. Weight stronger, lower-order tones more. */
            float w = 1.0f - (float)i / (float)MHN_NUM_TONES;   /* 1.0 .. 0.25 */
            penalty += 1.5f * w;
        }
    }
    if (penalty > 4.0f) penalty = 4.0f;   /* keep it bounded in z units */
    return penalty;
}
