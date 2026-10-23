/*
    Host-side unit test for the portable DSP pipeline. No ESP-IDF, no
    hardware: it learns a baseline from the "normal" WAV and checks that the
    health score is high for normal sound and drops for each injected fault.

    Build & run:  make -C health-node/test            (see test/Makefile)
*/
#include <stdio.h>
#include <string.h>

#include "mhn_config.h"
#include "features.h"
#include "baseline.h"
#include "scoring.h"
#include "tonal.h"
#include "wav.h"

#define HOP (MHN_FFT_SIZE / 2)          /* 50% overlap */
#define TONE_TOL_HZ 25.0f

typedef struct {
    float avg_score;
    int   frames;
    int   ok, warn, alert;
} eval_t;

static void learn(const wav_t *w, int start, int end, baseline_t *out)
{
    baseline_acc_t acc;
    baseline_begin(&acc);
    features_t f;
    tones_t t;
    for (int i = start; i + MHN_FFT_SIZE <= end; i += HOP) {
        features_extract(&w->samples[i], &f);
        tonal_find(f.mag, &t);
        baseline_add(&acc, &f);
        baseline_add_tones(&acc, t.hz, t.count);
    }
    baseline_finish(&acc, out);
}

static eval_t evaluate(const wav_t *w, int start, int end, const baseline_t *base)
{
    scoring_t sc;
    scoring_init(&sc);
    features_t f;
    tones_t t;
    eval_t e = {0};
    double sum = 0.0;
    for (int i = start; i + MHN_FFT_SIZE <= end; i += HOP) {
        features_extract(&w->samples[i], &f);
        tonal_find(f.mag, &t);
        float pen = tonal_penalty(base->tone_hz, MHN_NUM_TONES, &t, TONE_TOL_HZ);
        int s = scoring_update(&sc, base, &f, pen);
        sum += s;
        e.frames++;
        if (sc.state == HEALTH_OK) e.ok++;
        else if (sc.state == HEALTH_WARNING) e.warn++;
        else e.alert++;
    }
    e.avg_score = e.frames ? (float)(sum / e.frames) : 0.0f;
    return e;
}

static int load(const char *dir, const char *name, wav_t *w)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.wav", dir, name);
    if (wav_read(path, w) != 0) return -1;
    if (w->sample_rate != MHN_SAMPLE_RATE_HZ) {
        fprintf(stderr, "WARN %s: %d Hz, expected %d\n", name, w->sample_rate, MHN_SAMPLE_RATE_HZ);
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "wav";
    features_init();

    wav_t normal;
    if (load(dir, "normal", &normal) != 0) {
        fprintf(stderr, "run tools/gen_test_wavs.py first (into %s/)\n", dir);
        return 2;
    }

    /* Learn on the first half of the normal clip, evaluate on the second. */
    int half = normal.num_samples / 2;
    baseline_t base;
    learn(&normal, 0, half, &base);
    printf("baseline: %u frames, %d bands, tones @",
           base.frames, MHN_NUM_BANDS);
    for (int i = 0; i < MHN_NUM_TONES; i++) printf(" %.0f", base.tone_hz[i]);
    printf(" Hz\n\n");

    eval_t e_norm = evaluate(&normal, half, normal.num_samples, &base);

    const char *faults[] = {"broadband", "rattle", "shifted"};
    eval_t e_fault[3];
    wav_t fw[3];
    int have[3] = {0};
    for (int i = 0; i < 3; i++) {
        if (load(dir, faults[i], &fw[i]) == 0) {
            have[i] = 1;
            e_fault[i] = evaluate(&fw[i], 0, fw[i].num_samples, &base);
        }
    }

    printf("%-12s %7s   %5s %5s %5s\n", "scenario", "health", "OK", "WARN", "ALRT");
    printf("%-12s %7.1f   %5d %5d %5d\n", "normal", e_norm.avg_score,
           e_norm.ok, e_norm.warn, e_norm.alert);
    for (int i = 0; i < 3; i++) {
        if (!have[i]) continue;
        printf("%-12s %7.1f   %5d %5d %5d\n", faults[i], e_fault[i].avg_score,
               e_fault[i].ok, e_fault[i].warn, e_fault[i].alert);
    }
    printf("\n");

    /* ---- assertions ---- */
    int fail = 0;
    if (e_norm.avg_score < 80.0f) {
        printf("FAIL: normal health %.1f < 80 (baseline should track itself)\n",
               e_norm.avg_score);
        fail = 1;
    }
    for (int i = 0; i < 3; i++) {
        if (!have[i]) { printf("SKIP: %s.wav missing\n", faults[i]); continue; }
        if (e_fault[i].avg_score >= e_norm.avg_score - 5.0f) {
            printf("FAIL: %s health %.1f not clearly below normal %.1f\n",
                   faults[i], e_fault[i].avg_score, e_norm.avg_score);
            fail = 1;
        }
        if (e_fault[i].warn + e_fault[i].alert == 0) {
            printf("FAIL: %s never left OK state\n", faults[i]);
            fail = 1;
        }
    }

    wav_free(&normal);
    for (int i = 0; i < 3; i++) if (have[i]) wav_free(&fw[i]);

    if (fail) { printf("\nTESTS FAILED\n"); return 1; }
    printf("All DSP tests passed.\n");
    return 0;
}
