#include "audio_out.h"

#include <math.h>
#include <stdint.h>

#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include "board.h"

static const char *TAG = "audio_out";

#define SAMPLE_RATE_HZ    16000
#define TONE_CHUNK_FRAMES 256

static i2s_chan_handle_t s_tx;

esp_err_t audio_out_init(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_tx, NULL), TAG, "new tx channel failed");

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = BOARD_SPK_BCLK_IO,
            .ws = BOARD_SPK_LRCLK_IO,
            .dout = BOARD_SPK_DIN_IO,
            .din = I2S_GPIO_UNUSED,
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx, &std_cfg), TAG, "tx init failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx), TAG, "tx enable failed");
    ESP_LOGI(TAG, "speaker ready (I2S1)");
    return ESP_OK;
}

esp_err_t audio_play_tone(int freq_hz, int duration_ms)
{
    const int total = SAMPLE_RATE_HZ * duration_ms / 1000;
    const int fade = SAMPLE_RATE_HZ / 200;
    const float amp = 32767.0f * CONFIG_HACK_SPEAKER_VOLUME / 100.0f;
    const float step = 2.0f * (float)M_PI * freq_hz / SAMPLE_RATE_HZ;

    int16_t chunk[TONE_CHUNK_FRAMES * 2];
    float phase = 0;

    for (int done = 0; done < total;) {
        int n = total - done < TONE_CHUNK_FRAMES ? total - done : TONE_CHUNK_FRAMES;
        for (int i = 0; i < n; i++) {
            int pos = done + i;
            float env = 1.0f;
            if (pos < fade) env = (float)pos / fade;
            else if (total - pos < fade) env = (float)(total - pos) / fade;
            int16_t s = (int16_t)(sinf(phase) * amp * env);
            chunk[i * 2] = s;
            chunk[i * 2 + 1] = s;
            phase += step;
            if (phase > 2.0f * (float)M_PI) phase -= 2.0f * (float)M_PI;
        }
        size_t written = 0;
        ESP_RETURN_ON_ERROR(i2s_channel_write(s_tx, chunk, n * 2 * sizeof(int16_t), &written, 1000), TAG,
                            "tone write failed");
        done += n;
    }
    return ESP_OK;
}
