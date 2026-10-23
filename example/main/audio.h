#pragma once

#include "esp_err.h"

esp_err_t audio_init(void);

int16_t audio_mic_sample(void);

esp_err_t audio_play_tone(int freq_hz, int duration_ms);
