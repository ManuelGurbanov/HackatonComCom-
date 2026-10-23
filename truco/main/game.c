#include "game.h"

#include <stdio.h>
#include <string.h>

#include "esp_random.h"

static const char SUITS[] = "EBOC";
static const uint8_t NUMS[10] = {1, 2, 3, 4, 5, 6, 7, 10, 11, 12};

static int card_num(uint8_t id) { return NUMS[id % 10]; }
static int card_suit(uint8_t id) { return id / 10; }

// Jerarquia del truco: mayor numero = mas fuerte.
int card_power(uint8_t id)
{
    int s = card_suit(id);
    switch (card_num(id)) {
    case 1:  return s == 0 ? 14 : s == 1 ? 13 : 8;
    case 7:  return s == 0 ? 12 : s == 2 ? 11 : 4;
    case 3:  return 10;
    case 2:  return 9;
    case 12: return 7;
    case 11: return 6;
    case 10: return 5;
    case 6:  return 3;
    case 5:  return 2;
    default: return 1;
    }
}

void card_str(uint8_t id, char out[4])
{
    if (id == NO_CARD) {
        strcpy(out, "---");
        return;
    }
    snprintf(out, 4, "%d%c", card_num(id), SUITS[card_suit(id)]);
}

static void push_event(game_t *g, uint8_t code, int who, int a, int b, int c)
{
    g->ev = (event_t){code, (uint8_t)who, (uint8_t)a, (uint8_t)b, (uint8_t)c};
}

static int envido_points(const uint8_t h[3])
{
    int v[3], best = 0;
    for (int i = 0; i < 3; i++) {
        int n = card_num(h[i]);
        v[i] = n >= 10 ? 0 : n;
        if (v[i] > best) best = v[i];
    }
    for (int i = 0; i < 3; i++) {
        for (int j = i + 1; j < 3; j++) {
            if (card_suit(h[i]) == card_suit(h[j]) && 20 + v[i] + v[j] > best) {
                best = 20 + v[i] + v[j];
            }
        }
    }
    return best;
}

static void deal(game_t *g)
{
    uint8_t deck[40];
    for (int i = 0; i < 40; i++) deck[i] = i;
    for (int i = 39; i > 0; i--) {
        int j = esp_random() % (i + 1);
        uint8_t t = deck[i]; deck[i] = deck[j]; deck[j] = t;
    }
    for (int k = 0; k < 3; k++) {
        g->dealt[0][k] = g->hand[0][k] = deck[k];
        g->dealt[1][k] = g->hand[1][k] = deck[3 + k];
    }
    memset(g->played, NO_CARD, sizeof(g->played));
    memset(g->trick_win, -1, sizeof(g->trick_win));
    g->trick = 0;
    g->mano = g->hand_no % 2;   // la mano alterna cada ronda
    g->leader = g->to_act = g->mano;
    g->truco_level = 1;
    g->truco_owner = -1;
    g->pending = PEND_NONE;
    g->envido_done = false;
    g->phase = PH_PLAY;
    push_event(g, EV_DEAL, 0, g->hand_no + 1, 0, 0);
}

void game_new(game_t *g)
{
    memset(g, 0, sizeof(*g));
    g->winner = -1;
    deal(g);
}

void game_next_hand(game_t *g)
{
    g->hand_no++;
    deal(g);
}

static void add_points(game_t *g, int p, int n)
{
    g->score[p] += n;
    if (g->score[p] >= WIN_SCORE) {
        g->score[p] = WIN_SCORE;
        g->winner = p;
        g->phase = PH_GAME_OVER;
        g->pending = PEND_NONE;
        push_event(g, EV_GAME_WIN, p, 0, 0, 0);
    }
}

static void end_hand(game_t *g, int winner, int pts, int reason)
{
    g->pending = PEND_NONE;
    g->phase = PH_HAND_END;
    push_event(g, EV_HAND_WIN, winner, pts, reason, 0);
    add_points(g, winner, pts);   // si termina el juego pisa el evento
}

// n = cantidad de bazas ya jugadas. Devuelve ganador de la mano o -1.
static int eval_hand(const game_t *g, int n)
{
    int a = g->trick_win[0], b = g->trick_win[1], c = g->trick_win[2];
    if (n == 2) {
        if (a < 2) return (b == a || b == 2) ? a : -1;
        return b < 2 ? b : -1;
    }
    if (n == 3) {
        if (c < 2) return c;
        if (a < 2) return a;
        if (b < 2) return b;
        return g->mano;   // todo parda: gana la mano
    }
    return -1;
}

static void resolve_trick(game_t *g)
{
    int t = g->trick;
    int p0 = card_power(g->played[0][t]);
    int p1 = card_power(g->played[1][t]);
    int w = p0 > p1 ? 0 : p1 > p0 ? 1 : 2;
    g->trick_win[t] = w;
    g->trick++;

    int h = eval_hand(g, g->trick);
    if (h >= 0) {
        end_hand(g, h, g->truco_level, 0);
        return;
    }
    if (w != 2) g->leader = w;
    g->to_act = g->leader;
}

static void resolve_envido(game_t *g, bool accepted)
{
    int caller = g->pending_from, resp = 1 - caller, kind = g->pending_arg;
    g->pending = PEND_NONE;
    g->to_act = caller;

    if (!accepted) {
        push_event(g, EV_NOQUIERO, resp, 0, 0, 0);
        add_points(g, caller, 1);
        return;
    }
    int pts = kind == 0 ? 2 : kind == 1 ? 3 : 0;
    if (kind == 2) {
        int best = g->score[0] > g->score[1] ? g->score[0] : g->score[1];
        pts = WIN_SCORE - best;
        if (pts < 1) pts = 1;
    }
    int e0 = envido_points(g->dealt[0]);
    int e1 = envido_points(g->dealt[1]);
    int w = e0 > e1 ? 0 : e1 > e0 ? 1 : g->mano;
    push_event(g, EV_ENV_RESULT, w, pts, e0, e1);
    add_points(g, w, pts);
}

static bool can_envido(const game_t *g, int p)
{
    return g->pending == PEND_NONE && !g->envido_done && g->trick == 0 &&
           g->played[p][0] == NO_CARD && g->truco_level == 1;
}

static int can_raise(const game_t *g, int p)
{
    if (g->pending == PEND_TRUCO) {
        return g->pending_arg < 4 ? g->pending_arg + 1 : 0;
    }
    if (g->pending == PEND_NONE && g->truco_level < 4 &&
        (g->truco_owner == -1 || g->truco_owner == p)) {
        return g->truco_level + 1;
    }
    return 0;
}

bool game_action(game_t *g, int p, int act, int arg)
{
    if (act == ACT_RESTART) {
        if (g->phase != PH_GAME_OVER) return false;
        game_new(g);
        return true;
    }
    if (g->phase != PH_PLAY || g->to_act != p) return false;
    int o = 1 - p;

    // Hay un canto pendiente: p tiene que responder.
    if (g->pending != PEND_NONE) {
        if (act == ACT_QUIERO) {
            if (g->pending == PEND_TRUCO) {
                g->truco_level = g->pending_arg;
                g->truco_owner = p;
                g->pending = PEND_NONE;
                g->to_act = g->pending_from;
                push_event(g, EV_QUIERO, p, 0, 0, 0);
            } else {
                resolve_envido(g, true);
            }
            return true;
        }
        if (act == ACT_NOQUIERO) {
            if (g->pending == PEND_TRUCO) {
                push_event(g, EV_NOQUIERO, p, 0, 0, 0);
                end_hand(g, o, g->truco_level, 2);
            } else {
                resolve_envido(g, false);
            }
            return true;
        }
        if (act == ACT_TRUCO && g->pending == PEND_TRUCO && can_raise(g, p)) {
            int lvl = g->pending_arg + 1;
            g->truco_level = g->pending_arg;   // subir implica aceptar el anterior
            g->truco_owner = p;
            g->pending_from = p;
            g->pending_arg = lvl;
            g->to_act = o;
            push_event(g, EV_TRUCO, p, lvl, 0, 0);
            return true;
        }
        return false;
    }

    switch (act) {
    case ACT_PLAY: {
        if (arg < 0 || arg > 2 || g->hand[p][arg] == NO_CARD) return false;
        g->played[p][g->trick] = g->hand[p][arg];
        g->hand[p][arg] = NO_CARD;
        if (g->played[o][g->trick] != NO_CARD) {
            resolve_trick(g);
        } else {
            g->to_act = o;
        }
        return true;
    }
    case ACT_TRUCO: {
        int lvl = can_raise(g, p);
        if (!lvl) return false;
        g->envido_done = true;   // despues del truco ya no se canta envido
        g->pending = PEND_TRUCO;
        g->pending_from = p;
        g->pending_arg = lvl;
        g->to_act = o;
        push_event(g, EV_TRUCO, p, lvl, 0, 0);
        return true;
    }
    case ACT_ENVIDO:
    case ACT_REAL:
    case ACT_FALTA: {
        if (!can_envido(g, p)) return false;
        g->envido_done = true;
        g->pending = PEND_ENVIDO;
        g->pending_from = p;
        g->pending_arg = act - ACT_ENVIDO;
        g->to_act = o;
        push_event(g, EV_ENVIDO, p, g->pending_arg, 0, 0);
        return true;
    }
    case ACT_MAZO:
        end_hand(g, o, g->truco_level, 1);
        return true;
    }
    return false;
}

void game_view(const game_t *g, int p, view_t *v)
{
    int o = 1 - p;
    memset(v, 0, sizeof(*v));
    v->phase = g->phase;
    for (int t = 0; t < 3; t++) {
        v->hand[t] = g->hand[p][t];
        v->my_played[t] = g->played[p][t];
        v->opp_played[t] = g->played[o][t];
        int w = g->trick_win[t];
        v->trick_win[t] = w < 0 ? 0xFF : w == 2 ? 2 : (w == p ? 0 : 1);
    }
    v->score_me = g->score[p];
    v->score_opp = g->score[o];
    v->mano_me = g->mano == p;
    v->turn = g->phase == PH_PLAY && g->to_act == p;
    v->pending = g->pending;
    v->pending_arg = g->pending_arg;
    v->pending_mine = g->pending != PEND_NONE && g->pending_from == p;
    v->truco_level = g->truco_level;
    v->raise = v->turn ? can_raise(g, p) : 0;
    v->can_envido = v->turn && can_envido(g, p);
    v->hand_no = g->hand_no + 1;
    v->winner_me = g->winner == p;

    v->ev = g->ev;
    v->ev.who = g->ev.who == p ? 0 : 1;
    if (g->ev.code == EV_ENV_RESULT) {
        v->ev.b = p == 0 ? g->ev.b : g->ev.c;
        v->ev.c = p == 0 ? g->ev.c : g->ev.b;
    }
}
