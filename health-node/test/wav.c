#include "wav.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t rd32(const unsigned char *p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint16_t rd16(const unsigned char *p) {
    return p[0] | (p[1] << 8);
}

int wav_read(const char *path, wav_t *out)
{
    memset(out, 0, sizeof(*out));
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "wav: cannot open %s\n", path); return -1; }

    unsigned char hdr[12];
    if (fread(hdr, 1, 12, f) != 12 || memcmp(hdr, "RIFF", 4) || memcmp(hdr + 8, "WAVE", 4)) {
        fprintf(stderr, "wav: not a RIFF/WAVE file\n"); fclose(f); return -1;
    }

    int channels = 1, bits = 16, rate = 16000;
    unsigned char ck[8];
    while (fread(ck, 1, 8, f) == 8) {
        uint32_t sz = rd32(ck + 4);
        if (!memcmp(ck, "fmt ", 4)) {
            unsigned char fmt[16];
            if (fread(fmt, 1, 16, f) != 16) { fclose(f); return -1; }
            channels = rd16(fmt + 2);
            rate = rd32(fmt + 4);
            bits = rd16(fmt + 14);
            if (sz > 16) fseek(f, sz - 16, SEEK_CUR);
        } else if (!memcmp(ck, "data", 4)) {
            int bytes_per = bits / 8;
            int total = sz / bytes_per;
            int frames = total / channels;
            float *s = malloc(sizeof(float) * frames);
            if (!s) { fclose(f); return -1; }
            for (int i = 0; i < frames; i++) {
                int32_t acc = 0;
                for (int c = 0; c < channels; c++) {
                    unsigned char smp[4] = {0};
                    if (fread(smp, 1, bytes_per, f) != (size_t)bytes_per) { frames = i; break; }
                    if (bits == 16) acc += (int16_t)rd16(smp);
                    else if (bits == 8) acc += (int)smp[0] - 128;
                    else acc += (int32_t)rd32(smp);
                }
                s[i] = (float)acc / channels / 32768.0f;
            }
            out->samples = s;
            out->num_samples = frames;
            out->sample_rate = rate;
            fclose(f);
            return 0;
        } else {
            fseek(f, sz, SEEK_CUR);
        }
    }
    fclose(f);
    fprintf(stderr, "wav: no data chunk\n");
    return -1;
}

void wav_free(wav_t *w)
{
    free(w->samples);
    w->samples = NULL;
    w->num_samples = 0;
}
