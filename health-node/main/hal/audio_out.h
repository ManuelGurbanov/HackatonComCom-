#pragma once

#include "esp_err.h"

/* Minimal speaker output (MAX98357A on I2S1), reused from the board example.
   Used for an audible alert chirp and demo feedback. audio_play_tone blocks
   for ~duration_ms, so never call it from the DSP/audio loop. */
esp_err_t audio_out_init(void);
esp_err_t audio_play_tone(int freq_hz, int duration_ms);
