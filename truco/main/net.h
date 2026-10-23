#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "game.h"

/*
    Servidor: levanta un access point WiFi (TRUCO-ESP32) y un servidor TCP.
    Cliente:  se conecta a ese WiFi y abre un socket TCP contra el servidor.

    Protocolo: tramas [tipo (1 byte)][payload de largo fijo segun el tipo]
      MSG_VIEW   servidor -> cliente  view_t
      MSG_ACTION cliente -> servidor  action_msg_t
*/

typedef enum { NET_SERVER, NET_CLIENT } net_role_t;

enum { MSG_VIEW = 1, MSG_ACTION = 2 };

typedef struct __attribute__((packed)) {
    uint8_t act;
    uint8_t arg;
} action_msg_t;

typedef struct {
    uint8_t type;
    union {
        view_t view;
        action_msg_t action;
    };
} net_msg_t;

esp_err_t net_start(net_role_t role);

bool net_connected(void);
// Se incrementa cada vez que se establece una conexion nueva.
uint32_t net_session(void);

QueueHandle_t net_rx_queue(void);

bool net_send_view(const view_t *v);
bool net_send_action(uint8_t act, uint8_t arg);
