#include "audio_capture.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/i2s_pdm.h"
#include "esp_check.h"
#include "esp_log.h"

#include "board.h"
#include "mhn_config.h"

static const char *TAG = "audio_cap";

#define HOP          (MHN_FFT_SIZE / 2)   /* 50% overlap, matches test bench   */
#define NUM_FRAMES   3                    /* pool size: in-flight double buffer */
#define READ_FRAMES  256                  /* i2s read chunk (stereo frames)     */

static i2s_chan_handle_t s_rx;

/* Preallocated frame pool + index queues (no heap use in the loop). */
static float        s_pool[NUM_FRAMES][MHN_FFT_SIZE];
static QueueHandle_t s_free;    /* indices of buffers the producer may fill   */
static QueueHandle_t s_ready;   /* indices of filled buffers for the consumer */

static float s_line[MHN_FFT_SIZE];   /* sliding accumulation window */
static int   s_fill;
static volatile unsigned s_dropped;

static esp_err_t mic_init(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 6;
    chan_cfg.dma_frame_num = READ_FRAMES;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, NULL, &s_rx), TAG, "new rx channel failed");

    i2s_pdm_rx_config_t pdm_cfg = {
        .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(MHN_SAMPLE_RATE_HZ),
        .slot_cfg = I2S_PDM_RX_SLOT_PCM_FMT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .clk = BOARD_MIC_CLK_IO,
            .din = BOARD_MIC_DOUT_IO,
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_pdm_rx_mode(s_rx, &pdm_cfg), TAG, "rx init failed");
    return i2s_channel_enable(s_rx);
}

/* Emit the current sliding window as a frame, then slide by HOP (overlap). */
static void emit_frame(void)
{
    int idx;
    if (xQueueReceive(s_free, &idx, 0) != pdTRUE) {
        /* Consumer is behind: recycle the oldest ready frame instead of
           blocking the mic. Counted as a drop. */
        if (xQueueReceive(s_ready, &idx, 0) != pdTRUE) {
            s_dropped++;
            return;
        }
        s_dropped++;
    }
    memcpy(s_pool[idx], s_line, sizeof(s_line));
    xQueueSend(s_ready, &idx, 0);

    memmove(s_line, s_line + HOP, (MHN_FFT_SIZE - HOP) * sizeof(float));
    s_fill = MHN_FFT_SIZE - HOP;
}

static void capture_task(void *arg)
{
    static int16_t raw[READ_FRAMES * 2];   /* stereo int16 */
    while (1) {
        size_t bytes = 0;
        if (i2s_channel_read(s_rx, raw, sizeof(raw), &bytes, 1000) != ESP_OK) {
            continue;
        }
        int frames = bytes / (2 * sizeof(int16_t));
        for (int i = 0; i < frames; i++) {
            float mono = ((float)raw[i * 2] + (float)raw[i * 2 + 1]) * 0.5f / 32768.0f;
            s_line[s_fill++] = mono;
            if (s_fill >= MHN_FFT_SIZE) {
                emit_frame();
            }
        }
    }
}

esp_err_t audio_capture_init(void)
{
    s_free = xQueueCreate(NUM_FRAMES, sizeof(int));
    s_ready = xQueueCreate(NUM_FRAMES, sizeof(int));
    ESP_RETURN_ON_FALSE(s_free && s_ready, ESP_ERR_NO_MEM, TAG, "no mem for queues");
    for (int i = 0; i < NUM_FRAMES; i++) {
        xQueueSend(s_free, &i, 0);
    }

    ESP_RETURN_ON_ERROR(mic_init(), TAG, "mic init failed");
    ESP_RETURN_ON_FALSE(xTaskCreate(capture_task, "mic_cap", 4096, NULL, 6, NULL) == pdPASS,
                        ESP_ERR_NO_MEM, TAG, "capture task failed");
    ESP_LOGI(TAG, "PDM mic capture @ %d Hz, %d-sample frames (%d%% overlap)",
             MHN_SAMPLE_RATE_HZ, MHN_FFT_SIZE, 100 * HOP / MHN_FFT_SIZE);
    return ESP_OK;
}

esp_err_t audio_capture_read(float *dst, int timeout_ms)
{
    int idx;
    if (xQueueReceive(s_ready, &idx, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    memcpy(dst, s_pool[idx], MHN_FFT_SIZE * sizeof(float));
    xQueueSend(s_free, &idx, 0);
    return ESP_OK;
}

unsigned audio_capture_dropped(void)
{
    return s_dropped;
}
