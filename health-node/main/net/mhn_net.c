#include "mhn_net.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

static const char *TAG = "mhn_net";

static const uint8_t BCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static node_entry_t  s_table[MHN_MAX_NODES];
static SemaphoreHandle_t s_lock;

/* Bring up Wi-Fi far enough for ESP-NOW. In gateway mode the caller switches
   to APSTA and starts SoftAP afterwards; here we just guarantee the netif and
   a fixed channel so every node and the gateway agree. */
static esp_err_t wifi_bootstrap(wifi_mode_t mode)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    esp_netif_init();
    esp_event_loop_create_default();

    /* Create the default netifs BEFORE start so SoftAP DHCP attaches. */
    if (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA) {
        esp_netif_create_default_wifi_sta();
    }
    if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) {
        esp_netif_create_default_wifi_ap();
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "wifi init");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "wifi storage");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(mode), TAG, "wifi mode");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start");
    esp_wifi_set_channel(MHN_FIXED_CHANNEL, WIFI_SECOND_CHAN_NONE);
    return ESP_OK;
}

static esp_err_t espnow_bootstrap(void)
{
    ESP_RETURN_ON_ERROR(esp_now_init(), TAG, "espnow init");
    esp_now_peer_info_t peer = {0};
    memcpy(peer.peer_addr, BCAST, 6);
    peer.channel = MHN_FIXED_CHANNEL;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    return esp_now_add_peer(&peer);
}

/* ---------------- node ---------------- */

esp_err_t mhn_net_node_start(void)
{
    ESP_RETURN_ON_ERROR(wifi_bootstrap(WIFI_MODE_STA), TAG, "node wifi");
    ESP_RETURN_ON_ERROR(espnow_bootstrap(), TAG, "node espnow");
    ESP_LOGI(TAG, "node ESP-NOW ready on channel %d", MHN_FIXED_CHANNEL);
    return ESP_OK;
}

esp_err_t mhn_net_send_report(const mhn_report_t *r)
{
    return esp_now_send(BCAST, (const uint8_t *)r, sizeof(*r));
}

/* ---------------- gateway ---------------- */

static node_entry_t *find_or_add(const uint8_t *mac)
{
    for (int i = 0; i < MHN_MAX_NODES; i++) {
        if (s_table[i].used && memcmp(s_table[i].mac, mac, 6) == 0) return &s_table[i];
    }
    for (int i = 0; i < MHN_MAX_NODES; i++) {
        if (!s_table[i].used) {
            s_table[i].used = true;
            memcpy(s_table[i].mac, mac, 6);
            return &s_table[i];
        }
    }
    return NULL;   /* table full */
}

static void on_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (len != (int)sizeof(mhn_report_t)) return;
    const mhn_report_t *r = (const mhn_report_t *)data;
    if (r->magic != MHN_PROTO_MAGIC || r->version != MHN_PROTO_VERSION) return;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    node_entry_t *e = find_or_add(info->src_addr);
    if (e) {
        uint16_t expected = e->last.seq + 1;
        if (e->last_rx_ms != 0 && r->seq != expected) {
            e->lost += (uint16_t)(r->seq - expected);
        }
        e->last = *r;
        e->last_rx_ms = (uint32_t)(esp_timer_get_time() / 1000);
    }
    xSemaphoreGive(s_lock);
}

esp_err_t mhn_net_gateway_start(void)
{
    s_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_ERROR(wifi_bootstrap(WIFI_MODE_APSTA), TAG, "gw wifi");
    ESP_RETURN_ON_ERROR(espnow_bootstrap(), TAG, "gw espnow");
    ESP_RETURN_ON_ERROR(esp_now_register_recv_cb(on_recv), TAG, "gw recv cb");
    ESP_LOGI(TAG, "gateway ESP-NOW listening on channel %d", MHN_FIXED_CHANNEL);
    return ESP_OK;
}

int mhn_net_gateway_snapshot(node_entry_t *out)
{
    int n = 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int i = 0; i < MHN_MAX_NODES; i++) {
        if (s_table[i].used) out[n++] = s_table[i];
    }
    xSemaphoreGive(s_lock);
    return n;
}

esp_err_t mhn_net_gateway_self_report(const mhn_report_t *r)
{
    if (!s_lock) return ESP_ERR_INVALID_STATE;
    /* Locally-administered MAC derived from the node id; never collides with
       a real sender's address. */
    uint8_t selfmac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, r->node_id};
    xSemaphoreTake(s_lock, portMAX_DELAY);
    node_entry_t *e = find_or_add(selfmac);
    if (e) {
        e->last = *r;
        e->last_rx_ms = (uint32_t)(esp_timer_get_time() / 1000);
    }
    xSemaphoreGive(s_lock);
    return e ? ESP_OK : ESP_ERR_NO_MEM;
}
