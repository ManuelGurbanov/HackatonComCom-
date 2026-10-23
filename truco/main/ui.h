#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "buttons.h"
#include "game.h"

typedef struct {
    int sel;        // carta seleccionada de la mano
    int env_kind;   // 0 envido, 1 real envido, 2 falta envido
} ui_t;

// Dibuja la partida completa (sin flush).
void ui_draw_view(const view_t *v, ui_t *u);

// Pantalla de texto simple (hasta 3 lineas centradas), sin flush.
void ui_draw_text(const char *l1, const char *l2, const char *l3);

// Traduce un boton en una accion. Devuelve true si hay que ejecutar act/arg.
bool ui_button(const view_t *v, ui_t *u, button_id_t id, uint8_t *act, uint8_t *arg);
