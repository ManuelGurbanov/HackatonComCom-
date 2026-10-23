#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
    Logica pura del Truco (sin FreeRTOS ni red).
    Jugador 0 = servidor, jugador 1 = cliente. El servidor es el dueño de la
    partida: el cliente manda acciones y recibe una "vista" (view_t) con lo que
    puede ver (su mano, las cartas jugadas, puntajes, etc.).

    Mazo español de 40 cartas: id = palo*10 + indice, indice 0..9 -> 1..7,10,11,12
    Palos: 0 Espada, 1 Basto, 2 Oro, 3 Copa
*/

#define NO_CARD   0xFF
#define WIN_SCORE 15

typedef enum {
    ACT_NONE,
    ACT_PLAY,      // arg = slot de la mano (0..2)
    ACT_TRUCO,     // truco / retruco / vale cuatro (sube un nivel)
    ACT_ENVIDO,
    ACT_REAL,      // real envido
    ACT_FALTA,     // falta envido
    ACT_QUIERO,
    ACT_NOQUIERO,
    ACT_MAZO,
    ACT_RESTART,   // nueva partida (solo en GAME_OVER)
} act_t;

typedef enum { PH_PLAY, PH_HAND_END, PH_GAME_OVER } phase_t;
typedef enum { PEND_NONE, PEND_TRUCO, PEND_ENVIDO } pend_t;

enum {
    EV_NONE,
    EV_DEAL,        // a = numero de mano
    EV_TRUCO,       // who, a = nivel pedido (2,3,4)
    EV_ENVIDO,      // who, a = tipo (0 envido, 1 real, 2 falta)
    EV_QUIERO,      // who
    EV_NOQUIERO,    // who
    EV_ENV_RESULT,  // who = ganador, a = puntos, b = tantos mios, c = tantos rival
    EV_HAND_WIN,    // who = ganador, a = puntos, b = motivo (0 normal, 1 mazo, 2 no quiso)
    EV_GAME_WIN,    // who
};

typedef struct __attribute__((packed)) {
    uint8_t code, who, a, b, c;
} event_t;

// Lo que ve un jugador (who: 0 = yo, 1 = rival). Es lo que viaja por WiFi.
typedef struct __attribute__((packed)) {
    uint8_t phase;
    uint8_t hand[3];
    uint8_t my_played[3];
    uint8_t opp_played[3];
    uint8_t trick_win[3];   // 0 yo, 1 rival, 2 parda, 0xFF sin jugar
    uint8_t score_me, score_opp;
    uint8_t mano_me;
    uint8_t turn;           // 1 = me toca actuar (jugar o responder)
    uint8_t pending;        // PEND_*
    uint8_t pending_arg;
    uint8_t pending_mine;   // el canto pendiente es mio
    uint8_t truco_level;    // 1..4 aceptado
    uint8_t raise;          // nivel que puedo cantar (0 = no puedo)
    uint8_t can_envido;
    uint8_t hand_no;
    uint8_t winner_me;
    event_t ev;
} view_t;

typedef struct {
    uint8_t dealt[2][3];
    uint8_t hand[2][3];
    uint8_t played[2][3];
    int8_t trick_win[3];
    int trick;
    int leader;
    int to_act;
    int mano;
    int score[2];
    int hand_no;
    int truco_level;
    int truco_owner;   // -1 cualquiera, si no, quien puede subir
    int pending;
    int pending_from;
    int pending_arg;
    bool envido_done;
    int phase;
    int winner;
    event_t ev;
} game_t;

void game_new(game_t *g);
void game_next_hand(game_t *g);
bool game_action(game_t *g, int player, int act, int arg);
void game_view(const game_t *g, int player, view_t *v);

int card_power(uint8_t id);
void card_str(uint8_t id, char out[4]);
