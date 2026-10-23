#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include "app.h"
#include "buttons.h"
#include "display.h"
#include "storage.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_ERROR_CHECK(storage_init());

    if (display_init() != ESP_OK) {
        ESP_LOGE(TAG, "display init failed; continuing headless");
    }
    ESP_ERROR_CHECK(buttons_init());

    /* Role select:
         - menuconfig "Build as gateway" -> dashboard-only gateway (3+ boards).
         - hold KEY1 during boot          -> HOST: listen + run the dashboard
                                             locally (ideal for a 2-board demo).
         - otherwise                      -> plain listening node. */
    vTaskDelay(pdMS_TO_TICKS(50));   /* let buttons debounce once */
    bool key1 = buttons_read() & (1u << BTN_KEY1);

    display_clear();
#if CONFIG_MHN_ROLE_GATEWAY
    ESP_LOGI(TAG, "role: GATEWAY (dashboard only)");
    display_text(10, 28, "GATEWAY boot", false);
    display_flush();
    app_run_gateway();
#else
    if (key1) {
        ESP_LOGI(TAG, "role: HOST (node + dashboard) id=%d", CONFIG_MHN_NODE_ID);
        display_text(6, 28, "HOST boot", false);
        display_flush();
        app_run_host();
    } else {
        ESP_LOGI(TAG, "role: NODE id=%d", CONFIG_MHN_NODE_ID);
        display_text(10, 28, "NODE boot", false);
        display_flush();
        app_run_node();
    }
#endif
}
