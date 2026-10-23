#pragma once

/* Dead-simple mono 16-bit PCM WAV reader for the host test bench. */

typedef struct {
    int   sample_rate;
    int   num_samples;
    float *samples;   /* malloc'd, normalized to [-1, 1]; free with wav_free */
} wav_t;

int  wav_read(const char *path, wav_t *out);   /* 0 on success */
void wav_free(wav_t *w);
