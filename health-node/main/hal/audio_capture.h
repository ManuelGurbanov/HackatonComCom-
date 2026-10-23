#pragma once

#include "esp_err.h"

/*
    Raw microphone capture for the DSP pipeline.

    The board's stock audio.c only exposes a smoothed 0..100 level; the
    machine-health node needs the actual samples. This driver configures the
    PDM mic (I2S0, 16 kHz) with DMA and hands the DSP task overlapping frames
    of MHN_FFT_SIZE mono float samples (normalized to ~[-1, 1]).

    Memory model: a small fixed pool of frame buffers is allocated once at
    init; the capture loop never calls malloc/free, so there is no heap
    fragmentation in the audio path. If the consumer falls behind, the
    oldest frame is dropped and counted (audio_capture_dropped()) rather than
    stalling the mic -- that is an honest "we couldn't keep up" signal, not a
    silent data loss.
*/

esp_err_t audio_capture_init(void);

/* Block up to timeout_ms for the next frame. Copies MHN_FFT_SIZE floats into
   `dst`. Returns ESP_OK or ESP_ERR_TIMEOUT. */
esp_err_t audio_capture_read(float *dst, int timeout_ms);

/* Count of frames dropped because the DSP task was too slow (diagnostics). */
unsigned audio_capture_dropped(void);
