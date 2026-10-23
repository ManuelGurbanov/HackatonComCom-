#include "app.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#include "mhn_config.h"
#include "features.h"
#include "baseline.h"
#include "scoring.h"
#include "tonal.h"

#include "audio_capture.h"
#include "audio_out.h"
#include "buttons.h"
#include "display.h"
#include "storage.h"

#include "ui.h"
#include "mhn_net.h"
#include "dashboard.h"

static const char *TAG = "app";

/* Frame rate of the DSP loop: 16000 / (512/2) = 62.5 Hz. */
#define FRAME_HZ      (MHN_SAMPLE_RATE_HZ / (MHN_FFT_SIZE / 2))
#define LEARN_SECONDS 40
#define LEARN_FRAMES  (LEARN_SECONDS * FRAME_HZ)
#define REPORT_MS     2000

/* Shared state between the DSP task and the UI/net loop. */
typedef struct {
    SemaphoreHandle_t lock;
    features_t  feat;
    tones_t     tones;
    scoring_t   score;
    baseline_t  base;
    int         have_feat;

    int         learning;
    uint32_t    learn_count;
    baseline_acc_t acc;

    int         demo;        /* presentation aid: inject synthetic drift */
    float       demo_gain;

    node_settings_t settings;
} node_state_t;

static node_state_t S;

/* ---- DSP task: the real-time audio path, never blocks on UI or flash ---- */
static void dsp_task(void *arg)
{
    static float frame[MHN_FFT_SIZE];
    features_t f;
    tones_t t;
    while (1) {
        if (audio_capture_read(frame, 1000) != ESP_OK) continue;

        features_extract(frame, &f);
        tonal_find(f.mag, &t);

        xSemaphoreTake(S.lock, portMAX_DELAY);
        /* Optional demo drift: tilt the band energies to fake a developing
           fault so the score can be driven on cue during a pitch. This is a
           DEMO AID, not a measurement -- see docs. */
        if (S.demo) {
            for (int b = MHN_NUM_BANDS / 2; b < MHN_NUM_BANDS; b++) {
                f.bands[b] += S.demo_gain;
            }
        }
        S.feat = f;
        S.tones = t;
        S.have_feat = 1;

        if (S.learning) {
            baseline_add(&S.acc, &f);
            baseline_add_tones(&S.acc, t.hz, t.count);
            if (++S.learn_count >= LEARN_FRAMES) {
                baseline_finish(&S.acc, &S.base);
                S.learning = 0;
                storage_save_baseline(&S.base);
                scoring_mark_serviced(&S.score);
                ESP_LOGI(TAG, "baseline learned (%u frames)", S.learn_count);
            }
        } else {
            float pen = tonal_penalty(S.base.tone_hz, MHN_NUM_TONES, &t, 25.0f);
            scoring_update(&S.score, &S.base, &f, pen);
        }
        xSemaphoreGive(S.lock);
    }
}

static void begin_learning(void)
{
    xSemaphoreTake(S.lock, portMAX_DELAY);
    baseline_begin(&S.acc);
    S.learn_count = 0;
    S.learning = 1;
    xSemaphoreGive(S.lock);
    ESP_LOGI(TAG, "learning baseline for %d s ...", LEARN_SECONDS);
}

static void handle_button(button_event_t ev)
{
    if (!ev.pressed) return;
    xSemaphoreTake(S.lock, portMAX_DELAY);
    switch (ev.id) {
    case BTN_KEY1:   /* learn baseline */
        xSemaphoreGive(S.lock);
        begin_learning();
        return;
    case BTN_KEY2:   /* serviced / reset */
        scoring_mark_serviced(&S.score);
        ESP_LOGI(TAG, "marked serviced");
        break;
    case BTN_KEY3:   /* false alarm -> more tolerant */
        scoring_mark_false_alarm(&S.score);
        S.settings.false_alarm_margin = S.score.false_alarm_margin;
        storage_save_settings(&S.settings);
        ESP_LOGI(TAG, "false alarm, margin=%.1f", S.score.false_alarm_margin);
        break;
    case BTN_KEY4:   /* cycle screens handled in UI loop via static */
        break;
    case BTN_UP:     /* more sensitive */
        scoring_set_sensitivity(&S.score, S.score.sensitivity + 0.1f);
        S.settings.sensitivity = S.score.sensitivity;
        storage_save_settings(&S.settings);
        break;
    case BTN_DOWN:   /* less sensitive */
        scoring_set_sensitivity(&S.score, S.score.sensitivity - 0.1f);
        S.settings.sensitivity = S.score.sensitivity;
        storage_save_settings(&S.settings);
        break;
    case BTN_LEFT:   /* toggle demo drift */
        S.demo = !S.demo;
        S.demo_gain = 0.0f;
        break;
    case BTN_RIGHT:  /* ramp demo drift up while in demo */
        if (S.demo) S.demo_gain += 3.0f;
        break;
    default: break;
    }
    xSemaphoreGive(S.lock);
}

static void build_report(mhn_report_t *r, uint16_t seq)
{
    memset(r, 0, sizeof(*r));
    r->magic = MHN_PROTO_MAGIC;
    r->version = MHN_PROTO_VERSION;
    r->node_id = CONFIG_MHN_NODE_ID;
    r->num_bands = MHN_NUM_BANDS;
    r->seq = seq;
    r->uptime_s = (uint32_t)(esp_timer_get_time() / 1000000);
    strncpy(r->name, S.settings.machine_name, sizeof(r->name) - 1);

    xSemaphoreTake(S.lock, portMAX_DELAY);
    r->state = (uint8_t)S.score.state;
    r->health = (uint8_t)(S.score.score + 0.5f);
    r->deviation = S.score.deviation;
    if (S.have_feat) {
        r->rms_db = S.feat.rms_db;
        r->centroid_hz = S.feat.centroid_hz;
        /* Quantize band energies to 0..255 for the dashboard mini-spectrum. */
        float mx = -1e9f, mn = 1e9f;
        for (int b = 0; b < MHN_NUM_BANDS; b++) {
            if (S.feat.bands[b] > mx) mx = S.feat.bands[b];
            if (S.feat.bands[b] < mn) mn = S.feat.bands[b];
        }
        float range = mx - mn < 1.0f ? 1.0f : mx - mn;
        for (int b = 0; b < MHN_NUM_BANDS; b++) {
            int q = (int)((S.feat.bands[b] - mn) / range * 255.0f);
            r->bands[b] = q < 0 ? 0 : (q > 255 ? 255 : q);
        }
    }
    for (int i = 0; i < MHN_NUM_TONES; i++) r->top_tone_hz[i] = S.tones.hz[i];
    xSemaphoreGive(S.lock);
}

/* Runs the full listening pipeline. When `host` is true this board also
   brings up the gateway (ESP-NOW receiver + Wi-Fi dashboard) and feeds its
   own report into the local table instead of transmitting it, so a single
   extra board is not needed just to see the dashboard. */
static void run_node(bool host)
{
    S.lock = xSemaphoreCreateMutex();
    features_init();
    scoring_init(&S.score);

    storage_load_settings(&S.settings);
    scoring_set_sensitivity(&S.score, S.settings.sensitivity);
    S.score.false_alarm_margin = S.settings.false_alarm_margin;

    if (storage_load_baseline(&S.base) == ESP_OK && baseline_valid(&S.base)) {
        ESP_LOGI(TAG, "loaded baseline (%u frames)", S.base.frames);
    } else {
        memset(&S.base, 0, sizeof(S.base));
        ESP_LOGW(TAG, "no baseline yet -- press KEY1 to learn");
    }

    if (audio_capture_init() != ESP_OK) {
        ESP_LOGE(TAG, "audio capture failed; cannot run");
    }
    bool speaker_ok = (audio_out_init() == ESP_OK);

    bool net_ok;
    if (host) {
        net_ok = (mhn_net_gateway_start() == ESP_OK);
        if (dashboard_start() != ESP_OK) ESP_LOGE(TAG, "dashboard failed");
    } else {
        net_ok = (mhn_net_node_start() == ESP_OK);
    }

    xTaskCreate(dsp_task, "dsp", 8192, NULL, 7, NULL);

    QueueHandle_t evq = buttons_event_queue();
    ui_screen_t screen = UI_SCREEN_MAIN;
    uint32_t last_report = 0;
    uint16_t seq = 0;
    health_state_t last_state = HEALTH_OK;

    while (1) {
        /* Drain button events (non-blocking). */
        button_event_t ev;
        while (xQueueReceive(evq, &ev, 0) == pdTRUE) {
            if (ev.id == BTN_KEY4 && ev.pressed) {
                screen = (screen + 1) % UI_SCREEN_COUNT;
            } else {
                handle_button(ev);
            }
        }

        /* Build and render the UI snapshot. */
        ui_model_t m = {0};
        xSemaphoreTake(S.lock, portMAX_DELAY);
        m.name = S.settings.machine_name;
        m.score = (int)(S.score.score + 0.5f);
        m.state = S.score.state;
        m.feat = S.have_feat ? &S.feat : NULL;
        m.base = baseline_valid(&S.base) ? &S.base : NULL;
        m.deviation = S.score.deviation;
        m.sensitivity = S.score.sensitivity;
        m.learning = S.learning;
        m.learn_pct = S.learning ? (int)(100 * S.learn_count / LEARN_FRAMES) : 0;
        health_state_t cur = S.score.state;
        xSemaphoreGive(S.lock);

        m.net_ok = net_ok;
        m.dropped = audio_capture_dropped();
        m.screen = screen;
        ui_render(&m);

        bool state_changed = (cur != last_state);

        /* Audible chirp on transition into ALERT. */
        if (speaker_ok && cur == HEALTH_ALERT && last_state != HEALTH_ALERT) {
            audio_play_tone(880, 120);
        }

        /* Periodic ESP-NOW report (plus immediately on any state change). */
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
        if (net_ok && (now - last_report >= REPORT_MS || state_changed)) {
            mhn_report_t r;
            build_report(&r, seq++);
            if (host) {
                mhn_net_gateway_self_report(&r);   /* show up on own dashboard */
            } else {
                mhn_net_send_report(&r);           /* transmit to the gateway  */
            }
            last_report = now;
        }

        last_state = cur;
        vTaskDelay(pdMS_TO_TICKS(50));   /* ~20 fps UI */
    }
}

void app_run_node(void) { run_node(false); }
void app_run_host(void) { run_node(true); }

void app_run_gateway(void)
{
    if (mhn_net_gateway_start() != ESP_OK) {
        ESP_LOGE(TAG, "gateway radio failed");
    }
    if (dashboard_start() != ESP_OK) {
        ESP_LOGE(TAG, "dashboard failed");
    }

    static node_entry_t nodes[MHN_MAX_NODES];
    while (1) {
        int n = mhn_net_gateway_snapshot(nodes);
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);

        display_clear();
        display_text(0, 0, "GATEWAY  MHN-GATEWAY", false);
        for (int x = 0; x < 128; x++) display_set_pixel(x, 10, true);
        if (n == 0) {
            display_text(0, 20, "waiting for nodes..", false);
        }
        for (int i = 0; i < n && i < 4; i++) {
            char l[22];
            const mhn_report_t *r = &nodes[i].last;
            int age = (now - nodes[i].last_rx_ms) / 1000;
            snprintf(l, sizeof(l), "%-9s %3d %-4s", r->name, r->health,
                     scoring_state_name((health_state_t)r->state));
            display_text(0, 14 + i * 12, l, age > 8);
        }
        display_flush();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
