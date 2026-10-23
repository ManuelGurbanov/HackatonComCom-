#include "ui.h"

#include <stdio.h>
#include <string.h>

#include "board.h"
#include "display.h"
#include "mhn_config.h"

#define LCD_W BOARD_LCD_WIDTH
#define LCD_H BOARD_LCD_HEIGHT

static void header(const char *name, int net_ok)
{
    char line[22];
    snprintf(line, sizeof(line), "%-16s %s", name ? name : "", net_ok ? "NET" : "---");
    line[21] = 0;
    display_text(0, 0, line, false);
    for (int x = 0; x < LCD_W; x++) display_set_pixel(x, 10, true);
}

static void draw_main(const ui_model_t *m)
{
    char buf[16];

    /* Big score, left side. */
    snprintf(buf, sizeof(buf), "%3d", m->score);
    /* Scale the 6x8 font up x3 by drawing blocks per pixel is heavy; instead
       show score large-ish by stacking label + value and a bar. */
    display_text(0, 16, "HEALTH", false);
    display_text(0, 28, buf, false);

    /* State box on the right. */
    const char *st = scoring_state_name(m->state);
    bool flash = (m->state == HEALTH_ALERT);
    display_text(70, 20, st, flash);

    /* Health bar across the bottom. */
    int bw = LCD_W - 4;
    int fill = m->score * bw / 100;
    display_draw_rect(2, 44, bw, 10);
    display_fill_rect(2, 44, fill, 10, true);

    if (m->learning) {
        display_fill_rect(0, 14, LCD_W, 50, false);
        display_text(10, 22, "LEARNING", true);
        snprintf(buf, sizeof(buf), "%d%%", m->learn_pct);
        display_text(50, 38, buf, false);
        int fw = m->learn_pct * (LCD_W - 8) / 100;
        display_draw_rect(4, 50, LCD_W - 8, 8);
        display_fill_rect(4, 50, fw, 8, true);
    }
}

static void draw_spectrum(const ui_model_t *m)
{
    if (!m->feat) {
        display_text(0, 28, "no audio", false);
        return;
    }
    /* Auto-scale to the current max band so bars are always visible. */
    float mx = -1e9f, mn = 1e9f;
    for (int b = 0; b < MHN_NUM_BANDS; b++) {
        float v = m->feat->bands[b];
        if (v > mx) mx = v;
        if (v < mn) mn = v;
    }
    float range = mx - mn;
    if (range < 1.0f) range = 1.0f;

    int top = 12, bottom = 63;
    int h = bottom - top;
    int bw = LCD_W / MHN_NUM_BANDS;
    for (int b = 0; b < MHN_NUM_BANDS; b++) {
        int bh = (int)((m->feat->bands[b] - mn) / range * h);
        display_fill_rect(b * bw, bottom - bh, bw - 1, bh, true);

        /* Baseline mean outline (dotted top) so drift is visible. */
        if (m->base && m->base->frames >= 2) {
            int bbh = (int)((m->base->band_mean[b] - mn) / range * h);
            int y = bottom - bbh;
            if (y >= top && y <= bottom) display_set_pixel(b * bw + bw / 2, y, true);
        }
    }
}

static void draw_info(const ui_model_t *m)
{
    char l[22];
    snprintf(l, sizeof(l), "dev   %.2f z", m->deviation);          display_text(0, 14, l, false);
    if (m->feat) {
        snprintf(l, sizeof(l), "rms   %.0f dB", m->feat->rms_db);  display_text(0, 24, l, false);
        snprintf(l, sizeof(l), "cen   %.0f Hz", m->feat->centroid_hz); display_text(0, 34, l, false);
    }
    snprintf(l, sizeof(l), "sens  %d%%", (int)(m->sensitivity * 100)); display_text(0, 44, l, false);
    snprintf(l, sizeof(l), "drop  %u", m->dropped);               display_text(0, 54, l, false);
}

void ui_render(const ui_model_t *m)
{
    display_clear();
    header(m->name, m->net_ok);
    switch (m->screen) {
    case UI_SCREEN_SPECTRUM: draw_spectrum(m); break;
    case UI_SCREEN_INFO:     draw_info(m);     break;
    case UI_SCREEN_MAIN:
    default:                 draw_main(m);     break;
    }
    display_flush();
}
