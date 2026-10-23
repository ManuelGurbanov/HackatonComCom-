#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "audio.h"
#include "buttons.h"
#include "display.h"
#include "game.h"
#include "net.h"
#include "ui.h"

static const char *TAG = "truco";

#define UI_PERIOD_MS     40
#define HAND_END_MS      3000

static bool s_display_ok;
static bool s_audio_ok;
static ui_t s_ui;
static view_t s_view;   // lo que se dibuja (en el servidor se genera local)

static void show_text(const char *l1, const char *l2, const char *l3)
{
    if (!s_display_ok) return;
    ui_draw_text(l1, l2, l3);
    display_flush();
}

static void beep(int f, int ms)
{
    if (s_audio_ok) audio_play_tone(f, ms);
}

// Sonidos segun lo que cambio entre dos vistas
static void sound_update(const view_t *old, const view_t *cur, bool had_old)
{
    if (cur->phase == PH_GAME_OVER && (!had_old || old->phase != PH_GAME_OVER)) {
        const int win[] = {523, 659, 784, 1047}, lose[] = {392, 330, 262, 196};
        const int *n = cur->winner_me ? win : lose;
        for (int i = 0; i < 4; i++) beep(n[i], 140);
    } else if (cur->phase == PH_HAND_END && (!had_old || old->phase != PH_HAND_END)) {
        beep(cur->ev.who == 0 ? 784 : 330, 200);
    } else if (cur->turn && (!had_old || !old->turn)) {
        beep(cur->pending ? 660 : 880, 70);
    }
}

static void draw_game(const view_t *v)
{
    if (!s_display_ok) return;
    ui_draw_view(v, &s_ui);
    display_flush();
}

static net_role_t choose_role(void)
{
    QueueHandle_t q = buttons_event_queue();
    show_text("TRUCO", "KEY1: SERVIDOR", "KEY2: CLIENTE");
    for (;;) {
        button_event_t ev;
        if (xQueueReceive(q, &ev, portMAX_DELAY) == pdTRUE && ev.pressed) {
            if (ev.id == BTN_KEY1) return NET_SERVER;
            if (ev.id == BTN_KEY2) return NET_CLIENT;
        }
    }
}

static void run_server(void)
{
    QueueHandle_t btn_q = buttons_event_queue();
    game_t g;
    uint32_t session = 0;
    bool in_game = false, had_view = false, hand_end_armed = false;
    TickType_t hand_end_at = 0;
    view_t prev = {0};

    for (;;) {
        bool changed = false;

        if (!net_connected()) {
            in_game = false;
            had_view = false;
        } else if (!in_game || session != net_session()) {
            session = net_session();
            game_new(&g);
            in_game = true;
            hand_end_armed = false;
            changed = true;
            ESP_LOGI(TAG, "rival connected, new game");
        }

        button_event_t ev;
        if (xQueueReceive(btn_q, &ev, pdMS_TO_TICKS(UI_PERIOD_MS)) == pdTRUE && ev.pressed && in_game) {
            view_t v;
            uint8_t act, arg;
            game_view(&g, 0, &v);
            if (ui_button(&v, &s_ui, ev.id, &act, &arg) && game_action(&g, 0, act, arg)) {
                changed = true;
            }
        }

        net_msg_t m;
        while (xQueueReceive(net_rx_queue(), &m, 0) == pdTRUE) {
            if (m.type == MSG_ACTION && in_game && game_action(&g, 1, m.action.act, m.action.arg)) {
                changed = true;
            }
        }

        // Despues de cada mano esperamos un rato y repartimos de nuevo
        if (in_game && g.phase == PH_HAND_END) {
            if (!hand_end_armed) {
                hand_end_armed = true;
                hand_end_at = xTaskGetTickCount() + pdMS_TO_TICKS(HAND_END_MS);
            } else if ((int32_t)(xTaskGetTickCount() - hand_end_at) >= 0) {
                game_next_hand(&g);
                hand_end_armed = false;
                changed = true;
            }
        } else {
            hand_end_armed = false;
        }

        if (!in_game) {
            show_text("SERVIDOR", "WIFI: TRUCO-ESP32", "ESPERANDO RIVAL");
            continue;
        }

        if (changed) {
            view_t other;
            game_view(&g, 1, &other);
            net_send_view(&other);
        }
        game_view(&g, 0, &s_view);
        if (changed) {
            sound_update(&prev, &s_view, had_view);
            prev = s_view;
            had_view = true;
        }
        draw_game(&s_view);
    }
}

static void run_client(void)
{
    QueueHandle_t btn_q = buttons_event_queue();
    bool have_view = false;
    view_t prev = {0};

    for (;;) {
        button_event_t ev;
        if (xQueueReceive(btn_q, &ev, pdMS_TO_TICKS(UI_PERIOD_MS)) == pdTRUE && ev.pressed &&
            have_view && net_connected()) {
            uint8_t act, arg;
            if (ui_button(&s_view, &s_ui, ev.id, &act, &arg)) {
                net_send_action(act, arg);
            }
        }

        net_msg_t m;
        while (xQueueReceive(net_rx_queue(), &m, 0) == pdTRUE) {
            if (m.type == MSG_VIEW) {
                sound_update(&prev, &m.view, have_view);
                s_view = m.view;
                prev = m.view;
                have_view = true;
            }
        }

        if (!net_connected()) {
            have_view = false;
            show_text("CLIENTE", "BUSCANDO TRUCO-ESP32", "CONECTANDO...");
        } else if (!have_view) {
            show_text("CLIENTE", "CONECTADO", "ESPERANDO PARTIDA");
        } else {
            draw_game(&s_view);
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Truco starting");

    s_display_ok = display_init() == ESP_OK;
    if (!s_display_ok) ESP_LOGE(TAG, "display init failed - continuing without screen");
    ESP_ERROR_CHECK(buttons_init());
    s_audio_ok = audio_init() == ESP_OK;

    net_role_t role = choose_role();
    show_text(role == NET_SERVER ? "SERVIDOR" : "CLIENTE", "INICIANDO WIFI...", NULL);
    ESP_ERROR_CHECK(net_start(role));

    if (role == NET_SERVER) run_server();
    else run_client();
}
