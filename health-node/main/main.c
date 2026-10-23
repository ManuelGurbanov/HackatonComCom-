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

    /* Role select: hold KEY1 during boot to come up as the gateway,
       otherwise run as a listening node. A compile-time default is also
       available via menuconfig for a board permanently wired as gateway. */
    vTaskDelay(pdMS_TO_TICKS(50));   /* let buttons debounce once */
    bool key1 = buttons_read() & (1u << BTN_KEY1);
    bool gateway = key1;
#if CONFIG_MHN_ROLE_GATEWAY
    gateway = true;
#endif

    if (gateway) {
        ESP_LOGI(TAG, "role: GATEWAY");
        display_clear();
        display_text(10, 28, "GATEWAY boot", false);
        display_flush();
        app_run_gateway();
    } else {
        ESP_LOGI(TAG, "role: NODE id=%d", CONFIG_MHN_NODE_ID);
        display_clear();
        display_text(10, 28, "NODE boot", false);
        display_flush();
        app_run_node();
    }
}
