#pragma once

#include "esp_err.h"
#include "baseline.h"

/*
    Persistence of the learned baseline and a few per-node settings in NVS.
    The baseline is stored as an opaque blob (it is plain-old-data), so the
    DSP layer stays unaware of flash. Call storage_init() once after
    nvs_flash_init().
*/

typedef struct {
    char  machine_name[20];
    float sensitivity;        /* 0..1 */
    float false_alarm_margin; /* z-units */
} node_settings_t;

esp_err_t storage_init(void);

esp_err_t storage_save_baseline(const baseline_t *b);
esp_err_t storage_load_baseline(baseline_t *b);   /* ESP_ERR_NVS_NOT_FOUND if none */
esp_err_t storage_clear_baseline(void);

esp_err_t storage_save_settings(const node_settings_t *s);
esp_err_t storage_load_settings(node_settings_t *s);  /* fills defaults if none */
