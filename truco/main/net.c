#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "lwip/sockets.h"

#include "net.h"

static const char *TAG = "net";

#define WIFI_SSID   "TRUCO-ESP32"
#define WIFI_PASS   "truco-hackaton"
#define SERVER_IP   "192.168.4.1"   // IP por defecto del softAP
#define SERVER_PORT 3333
#define GOT_IP_BIT  BIT0

static QueueHandle_t s_rxq;
static SemaphoreHandle_t s_tx_lock;
static EventGroupHandle_t s_wifi_ev;
static volatile int s_sock = -1;
static volatile uint32_t s_session;

bool net_connected(void) { return s_sock >= 0; }
uint32_t net_session(void) { return s_session; }
QueueHandle_t net_rx_queue(void) { return s_rxq; }

static bool send_frame(uint8_t type, const void *payload, size_t len)
{
    uint8_t buf[1 + sizeof(view_t)];
    buf[0] = type;
    memcpy(buf + 1, payload, len);
    len += 1;

    bool ok = false;
    xSemaphoreTake(s_tx_lock, portMAX_DELAY);
    int sock = s_sock;
    if (sock >= 0) {
        size_t sent = 0;
        ok = true;
        while (sent < len) {
            int n = send(sock, buf + sent, len - sent, 0);
            if (n <= 0) {
                ok = false;
                break;
            }
            sent += n;
        }
    }
    xSemaphoreGive(s_tx_lock);
    return ok;
}

bool net_send_view(const view_t *v) { return send_frame(MSG_VIEW, v, sizeof(*v)); }

bool net_send_action(uint8_t act, uint8_t arg)
{
    action_msg_t a = {act, arg};
    return send_frame(MSG_ACTION, &a, sizeof(a));
}

static bool read_exact(int sock, void *dst, size_t n)
{
    uint8_t *p = dst;
    while (n) {
        int r = recv(sock, p, n, 0);
        if (r <= 0) return false;
        p += r;
        n -= r;
    }
    return true;
}

static void tune_socket(int sock)
{
    int one = 1, idle = 3, intvl = 1, cnt = 3;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    // keepalive agresivo: detecta que el otro se cayo en ~6 s
    setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one));
    setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
    setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof(intvl));
    setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &cnt, sizeof(cnt));
}

// Atiende una conexion hasta que se cae. Bloqueante.
static void serve_connection(int sock)
{
    tune_socket(sock);
    xQueueReset(s_rxq);
    s_session++;
    s_sock = sock;
    ESP_LOGI(TAG, "peer connected (session %u)", (unsigned)s_session);

    for (;;) {
        net_msg_t m;
        if (!read_exact(sock, &m.type, 1)) break;
        if (m.type == MSG_VIEW) {
            if (!read_exact(sock, &m.view, sizeof(m.view))) break;
        } else if (m.type == MSG_ACTION) {
            if (!read_exact(sock, &m.action, sizeof(m.action))) break;
        } else {
            break;
        }
        xQueueSend(s_rxq, &m, 0);
    }

    xSemaphoreTake(s_tx_lock, portMAX_DELAY);
    s_sock = -1;
    close(sock);
    xSemaphoreGive(s_tx_lock);
    ESP_LOGW(TAG, "peer disconnected");
}

static void server_task(void *arg)
{
    int ls = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    int one = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(SERVER_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(ls, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(ls, 1) != 0) {
        ESP_LOGE(TAG, "bind/listen failed");
        vTaskDelete(NULL);
    }
    for (;;) {
        int sock = accept(ls, NULL, NULL);
        if (sock < 0) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        serve_connection(sock);
    }
}

static void client_task(void *arg)
{
    for (;;) {
        xEventGroupWaitBits(s_wifi_ev, GOT_IP_BIT, pdFALSE, pdTRUE, portMAX_DELAY);

        int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
        struct sockaddr_in addr = {
            .sin_family = AF_INET,
            .sin_port = htons(SERVER_PORT),
        };
        inet_pton(AF_INET, SERVER_IP, &addr.sin_addr);
        if (sock < 0 || connect(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
            if (sock >= 0) close(sock);
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        serve_connection(sock);
        vTaskDelay(pdMS_TO_TICKS(300));
    }
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_wifi_ev, GOT_IP_BIT);
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_wifi_ev, GOT_IP_BIT);
    }
}

esp_err_t net_start(net_role_t role)
{
    s_rxq = xQueueCreate(8, sizeof(net_msg_t));
    s_tx_lock = xSemaphoreCreateMutex();
    s_wifi_ev = xEventGroupCreate();
    ESP_RETURN_ON_FALSE(s_rxq && s_tx_lock && s_wifi_ev, ESP_ERR_NO_MEM, TAG, "no mem");

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(err, TAG, "nvs init failed");

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init failed");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop failed");

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();

    if (role == NET_SERVER) {
        esp_netif_create_default_wifi_ap();
        ESP_RETURN_ON_ERROR(esp_wifi_init(&init), TAG, "wifi init failed");

        wifi_config_t cfg = {0};
        strcpy((char *)cfg.ap.ssid, WIFI_SSID);
        strcpy((char *)cfg.ap.password, WIFI_PASS);
        cfg.ap.ssid_len = strlen(WIFI_SSID);
        cfg.ap.channel = 1;
        cfg.ap.max_connection = 2;
        cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
        ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), TAG, "set mode");
        ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &cfg), TAG, "set config");
        ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start");
        esp_wifi_set_ps(WIFI_PS_NONE);

        ESP_RETURN_ON_FALSE(xTaskCreate(server_task, "tcp_srv", 4096, NULL, 5, NULL) == pdPASS,
                            ESP_ERR_NO_MEM, TAG, "task create failed");
        ESP_LOGI(TAG, "AP '%s' up, TCP server on :%d", WIFI_SSID, SERVER_PORT);
    } else {
        esp_netif_create_default_wifi_sta();
        ESP_RETURN_ON_ERROR(esp_wifi_init(&init), TAG, "wifi init failed");
        ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL),
                            TAG, "register wifi handler");
        ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi_event, NULL),
                            TAG, "register ip handler");

        wifi_config_t cfg = {0};
        strcpy((char *)cfg.sta.ssid, WIFI_SSID);
        strcpy((char *)cfg.sta.password, WIFI_PASS);
        cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "set mode");
        ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &cfg), TAG, "set config");
        ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start");
        esp_wifi_set_ps(WIFI_PS_NONE);

        ESP_RETURN_ON_FALSE(xTaskCreate(client_task, "tcp_cli", 4096, NULL, 5, NULL) == pdPASS,
                            ESP_ERR_NO_MEM, TAG, "task create failed");
        ESP_LOGI(TAG, "STA connecting to '%s'", WIFI_SSID);
    }
    return ESP_OK;
}
