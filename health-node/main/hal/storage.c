#include "storage.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "storage";

#define NS           "mhn"
#define K_BASELINE   "baseline"
#define K_SETTINGS   "settings"

esp_err_t storage_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

esp_err_t storage_save_baseline(const baseline_t *b)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(h, K_BASELINE, b, sizeof(*b));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "baseline saved (%d bytes)", (int)sizeof(*b));
    return err;
}

esp_err_t storage_load_baseline(baseline_t *b)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    size_t len = sizeof(*b);
    err = nvs_get_blob(h, K_BASELINE, b, &len);
    nvs_close(h);
    if (err == ESP_OK && len != sizeof(*b)) return ESP_ERR_INVALID_SIZE;
    return err;
}

esp_err_t storage_clear_baseline(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_erase_key(h, K_BASELINE);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : err;
}

esp_err_t storage_save_settings(const node_settings_t *s)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(h, K_SETTINGS, s, sizeof(*s));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t storage_load_settings(node_settings_t *s)
{
    /* Defaults first, overwritten if a saved copy exists. */
    memset(s, 0, sizeof(*s));
    strncpy(s->machine_name, "Machine 1", sizeof(s->machine_name) - 1);
    s->sensitivity = 0.5f;
    s->false_alarm_margin = 0.0f;

    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READONLY, &h);
    if (err != ESP_OK) return ESP_OK;   /* no namespace yet -> defaults */
    size_t len = sizeof(*s);
    nvs_get_blob(h, K_SETTINGS, s, &len);
    nvs_close(h);
    return ESP_OK;
}
