#include <stdio.h>
#include <string.h>

#include "board.h"
#include "display.h"
#include "ui.h"

/*
    Controles (se pueden remapear aca, el joystick esta rotado en la placa):
      LEFT / RIGHT  elegir carta
      UP / DOWN     cambiar tipo de envido (envido / real / falta)
      KEY1..KEY4    las 4 acciones que muestra la barra de abajo
*/
#define BTN_PREV_CARD   BTN_LEFT
#define BTN_NEXT_CARD   BTN_RIGHT
#define BTN_ENV_NEXT    BTN_UP
#define BTN_ENV_PREV    BTN_DOWN

#define SLOT_W   23
#define SLOT_H   10
#define SLOT_DX  25

static const char *const TRUCO_NAMES[] = {"", "", "TRUCO", "RETRUCO", "VALE 4"};
static const char *const TRUCO_SHORT[] = {"", "", "TRUCO", "RETRU", "VALE4"};
static const char *const ENV_NAMES[] = {"ENVIDO", "REAL ENVIDO", "FALTA ENVIDO"};
static const char *const ENV_SHORT[] = {"ENVID", "REAL ", "FALTA"};

static const char *who_name(int who) { return who == 0 ? "YO" : "RIV"; }

static void event_text(const view_t *v, char *out, size_t n)
{
    const event_t *e = &v->ev;
    switch (e->code) {
    case EV_DEAL:
        snprintf(out, n, "MANO NUEVA #%d", e->a);
        break;
    case EV_TRUCO:
        snprintf(out, n, "%s: %s!", who_name(e->who), TRUCO_NAMES[e->a < 5 ? e->a : 0]);
        break;
    case EV_ENVIDO:
        snprintf(out, n, "%s: %s!", who_name(e->who), ENV_NAMES[e->a < 3 ? e->a : 0]);
        break;
    case EV_QUIERO:
        snprintf(out, n, "%s: QUIERO!", who_name(e->who));
        break;
    case EV_NOQUIERO:
        snprintf(out, n, "%s: NO QUIERO", who_name(e->who));
        break;
    case EV_ENV_RESULT:
        snprintf(out, n, "ENVIDO %d-%d +%d %s", e->b, e->c, e->a, who_name(e->who));
        break;
    case EV_HAND_WIN:
        if (e->b == 1) {
            if (e->who == 0) snprintf(out, n, "RIVAL AL MAZO +%d", e->a);
            else snprintf(out, n, "TE FUISTE AL MAZO");
        } else if (e->b == 2) {
            if (e->who == 0) snprintf(out, n, "NO QUISO! +%d", e->a);
            else snprintf(out, n, "NO QUISISTE: RIV+%d", e->a);
        } else {
            snprintf(out, n, e->who == 0 ? "GANASTE LA MANO +%d" : "PERDISTE LA MANO +%d", e->a);
        }
        break;
    default:
        out[0] = 0;
    }
}

static void draw_card(int x, int y, uint8_t card, bool inverted)
{
    char s[4];
    card_str(card, s);
    if (inverted) {
        display_fill_rect(x, y, SLOT_W, SLOT_H, true);
    } else {
        display_fill_rect(x, y, SLOT_W, SLOT_H, false);
        display_draw_rect(x, y, SLOT_W, SLOT_H);
    }
    int w = (int)strlen(s) * DISPLAY_FONT_W;
    int tx = x + (SLOT_W - w) / 2;
    for (int i = 0; s[i]; i++) {
        char c[2] = {s[i], 0};
        // display_text(invert=true) dibuja en negro sobre un rect blanco
        display_text(tx + i * DISPLAY_FONT_W, y + 1, c, inverted);
    }
}

static void fix_sel(const view_t *v, ui_t *u)
{
    for (int i = 0; i < 3; i++) {
        int s = (u->sel + i) % 3;
        if (v->hand[s] != NO_CARD) {
            u->sel = s;
            return;
        }
    }
}

static void move_sel(const view_t *v, ui_t *u, int dir)
{
    for (int i = 1; i <= 3; i++) {
        int s = ((u->sel + dir * i) % 3 + 3) % 3;
        if (v->hand[s] != NO_CARD) {
            u->sel = s;
            return;
        }
    }
}

void ui_draw_text(const char *l1, const char *l2, const char *l3)
{
    const char *lines[3] = {l1, l2, l3};
    display_clear();
    for (int i = 0; i < 3; i++) {
        if (!lines[i]) continue;
        int x = (BOARD_LCD_WIDTH - (int)strlen(lines[i]) * DISPLAY_FONT_W) / 2;
        display_text(x < 0 ? 0 : x, 8 + i * 18, lines[i], i == 0);
    }
}

static void draw_label(int slot, const char *s)
{
    if (s && s[0]) display_text(slot * 32 + 1, 56, s, true);
}

void ui_draw_view(const view_t *v, ui_t *u)
{
    char line[32];
    fix_sel(v, u);
    display_clear();

    if (v->phase == PH_GAME_OVER) {
        display_text(22, 2, v->winner_me ? "GANASTE EL JUEGO!" : "PERDISTE EL JUEGO", true);
        snprintf(line, sizeof(line), "YO %d  -  RIVAL %d", v->score_me, v->score_opp);
        display_text(16, 24, line, false);
        draw_label(0, "OTRA ");
        display_text(36, 57, "KEY1: REVANCHA", false);
        return;
    }

    // Encabezado con el puntaje
    snprintf(line, sizeof(line), "YO:%d RIV:%d", v->score_me, v->score_opp);
    display_fill_rect(0, 0, BOARD_LCD_WIDTH, 9, true);
    display_text(2, 1, line, true);
    snprintf(line, sizeof(line), "#%d", v->hand_no);
    display_text(BOARD_LCD_WIDTH - 2 - (int)strlen(line) * DISPLAY_FONT_W, 1, line, true);

    // Mesa: arriba el rival, abajo yo
    for (int t = 0; t < 3; t++) {
        int x = 2 + t * SLOT_DX;
        bool opp_won = v->trick_win[t] == 1, me_won = v->trick_win[t] == 0;
        if (v->opp_played[t] != NO_CARD) draw_card(x, 10, v->opp_played[t], opp_won);
        else display_draw_rect(x, 10, SLOT_W, SLOT_H);
        if (v->my_played[t] != NO_CARD) draw_card(x, 21, v->my_played[t], me_won);
        else display_draw_rect(x, 21, SLOT_W, SLOT_H);
    }

    // Costado: quien es mano y valor del truco
    display_text(80, 11, v->mano_me ? "MANO:YO" : "MANO:RIV", false);
    snprintf(line, sizeof(line), "VALE %d", v->truco_level);
    display_text(80, 22, line, false);

    event_text(v, line, sizeof(line));
    display_text(2, 34, line, false);

    // Mi mano
    for (int i = 0; i < 3; i++) {
        if (v->hand[i] == NO_CARD) continue;
        draw_card(2 + i * SLOT_DX, 44, v->hand[i], i == u->sel);
    }
    if (v->phase == PH_HAND_END) display_text(80, 45, "FIN MANO", true);
    else if (v->turn && v->pending != PEND_NONE) display_text(80, 45, "RESPONDE", true);
    else if (v->turn) display_text(80, 45, "TU TURNO", true);
    else display_text(80, 45, "RIVAL...", false);

    // Barra de acciones (KEY1..KEY4)
    if (v->phase == PH_PLAY && v->turn) {
        if (v->pending != PEND_NONE) {
            draw_label(0, "QUIER");
            if (v->raise) draw_label(1, TRUCO_SHORT[v->raise]);
            draw_label(3, "NOQUI");
        } else {
            draw_label(0, "JUGAR");
            if (v->raise) draw_label(1, TRUCO_SHORT[v->raise]);
            if (v->can_envido) draw_label(2, ENV_SHORT[u->env_kind]);
            draw_label(3, "MAZO ");
        }
    }
}

bool ui_button(const view_t *v, ui_t *u, button_id_t id, uint8_t *act, uint8_t *arg)
{
    *arg = 0;

    if (v->phase == PH_GAME_OVER) {
        if (id == BTN_KEY1) {
            *act = ACT_RESTART;
            return true;
        }
        return false;
    }

    if (id == BTN_PREV_CARD) { move_sel(v, u, -1); return false; }
    if (id == BTN_NEXT_CARD) { move_sel(v, u, +1); return false; }
    if (id == BTN_ENV_NEXT) { u->env_kind = (u->env_kind + 1) % 3; return false; }
    if (id == BTN_ENV_PREV) { u->env_kind = (u->env_kind + 2) % 3; return false; }

    if (v->phase != PH_PLAY || !v->turn) return false;

    if (v->pending != PEND_NONE) {
        switch (id) {
        case BTN_KEY1: *act = ACT_QUIERO; return true;
        case BTN_KEY2: if (v->raise) { *act = ACT_TRUCO; return true; } return false;
        case BTN_KEY4: *act = ACT_NOQUIERO; return true;
        default: return false;
        }
    }

    switch (id) {
    case BTN_KEY1:
        fix_sel(v, u);
        *act = ACT_PLAY;
        *arg = u->sel;
        return v->hand[u->sel] != NO_CARD;
    case BTN_KEY2:
        if (!v->raise) return false;
        *act = ACT_TRUCO;
        return true;
    case BTN_KEY3:
        if (!v->can_envido) return false;
        *act = ACT_ENVIDO + u->env_kind;
        return true;
    case BTN_KEY4:
        *act = ACT_MAZO;
        return true;
    default:
        return false;
    }
}
