#include "features.h"
#include "fft.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static float s_window[MHN_FFT_SIZE];
static int   s_band_lo[MHN_NUM_BANDS];   /* first bin of each band (inclusive) */
static int   s_band_hi[MHN_NUM_BANDS];   /* last  bin of each band (inclusive) */
static int   s_ready = 0;

float features_bin_hz(int bin)
{
    return (float)bin * (float)MHN_SAMPLE_RATE_HZ / (float)MHN_FFT_SIZE;
}

int features_hz_bin(float hz)
{
    int b = (int)(hz * (float)MHN_FFT_SIZE / (float)MHN_SAMPLE_RATE_HZ + 0.5f);
    if (b < 0) b = 0;
    if (b > MHN_FFT_BINS - 1) b = MHN_FFT_BINS - 1;
    return b;
}

void features_init(void)
{
    for (int i = 0; i < MHN_FFT_SIZE; i++) {
        s_window[i] = 0.5f * (1.0f - cosf(2.0f * (float)M_PI * i / (MHN_FFT_SIZE - 1)));
    }

    /* Log-spaced band edges between MHN_BAND_LO_HZ and MHN_BAND_HI_HZ. */
    float lo = logf(MHN_BAND_LO_HZ);
    float hi = logf(MHN_BAND_HI_HZ);
    for (int b = 0; b < MHN_NUM_BANDS; b++) {
        float f0 = expf(lo + (hi - lo) * b / MHN_NUM_BANDS);
        float f1 = expf(lo + (hi - lo) * (b + 1) / MHN_NUM_BANDS);
        s_band_lo[b] = features_hz_bin(f0);
        s_band_hi[b] = features_hz_bin(f1);
        if (s_band_hi[b] < s_band_lo[b]) s_band_hi[b] = s_band_lo[b];
    }
    s_ready = 1;
}

void features_extract(const float *frame, features_t *out)
{
    if (!s_ready) features_init();

    /* Windowed FFT (local fixed-size buffers; no heap, no VLA). */
    static float re[MHN_FFT_SIZE];   /* single DSP task only -> static is fine */
    static float im[MHN_FFT_SIZE];
    double sum_sq = 0.0;
    for (int i = 0; i < MHN_FFT_SIZE; i++) {
        sum_sq += (double)frame[i] * frame[i];
        re[i] = frame[i] * s_window[i];
        im[i] = 0.0f;
    }
    fft_radix2(re, im, MHN_FFT_SIZE);

    for (int k = 0; k < MHN_FFT_BINS; k++) {
        out->mag[k] = sqrtf(re[k] * re[k] + im[k] * im[k]);
    }

    /* Per-band log energy. */
    for (int b = 0; b < MHN_NUM_BANDS; b++) {
        double p = 0.0;
        for (int k = s_band_lo[b]; k <= s_band_hi[b]; k++) {
            p += (double)out->mag[k] * out->mag[k];
        }
        out->bands[b] = 10.0f * log10f((float)p + 1e-9f);
    }

    /* Broadband RMS level from the raw (un-windowed) frame. */
    float rms = sqrtf((float)(sum_sq / MHN_FFT_SIZE));
    out->rms_db = 20.0f * log10f(rms + 1e-9f);

    /* Spectral centroid over the usable band range. */
    double num = 0.0, den = 0.0;
    for (int k = 1; k < MHN_FFT_BINS; k++) {
        double m = out->mag[k];
        num += features_bin_hz(k) * m;
        den += m;
    }
    out->centroid_hz = den > 0.0 ? (float)(num / den) : 0.0f;
}
