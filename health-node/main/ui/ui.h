#pragma once

#include "features.h"
#include "baseline.h"
#include "scoring.h"

/*
    OLED UI for a node. The SSD1306 is 128x64 and MONOCHROME, so the
    requested "state color" is rendered instead as text + a filled/hollow
    status box + an inverted (flashing) banner on ALERT. See docs/HARDWARE.md.
*/

typedef enum {
    UI_SCREEN_MAIN = 0,   /* machine name, big health score, state            */
    UI_SCREEN_SPECTRUM,   /* live log-band spectrum vs. baseline outline       */
    UI_SCREEN_INFO,       /* numbers: deviation, rms, centroid, tones, net     */
    UI_SCREEN_COUNT,
} ui_screen_t;

typedef struct {
    const char       *name;
    int               score;          /* 0..100 */
    health_state_t    state;
    const features_t *feat;           /* may be NULL */
    const baseline_t *base;           /* may be NULL */
    float             deviation;
    float             sensitivity;    /* 0..1 */
    int               learning;       /* bool: show "learning" progress bar */
    int               learn_pct;      /* 0..100 */
    int               net_ok;         /* bool: ESP-NOW up */
    unsigned          dropped;        /* audio frames dropped */
    ui_screen_t       screen;
} ui_model_t;

void ui_render(const ui_model_t *m);
