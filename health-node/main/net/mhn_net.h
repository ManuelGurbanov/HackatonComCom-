#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "mhn_proto.h"

/*
    ESP-NOW transport.

    Node role:    mhn_net_node_start() then mhn_net_send_report() periodically.
    Gateway role: mhn_net_gateway_start(); incoming reports land in a table you
                  read with mhn_net_gateway_snapshot(). The gateway also brings
                  up the Wi-Fi dashboard (see dashboard.h).
*/

#define MHN_MAX_NODES 8
#define MHN_FIXED_CHANNEL 1   /* nodes + gateway SoftAP must share one channel */

typedef struct {
    bool         used;
    uint8_t      mac[6];
    mhn_report_t last;
    uint32_t     last_rx_ms;   /* esp_timer ms when last heard */
    uint16_t     lost;         /* gaps detected via seq */
} node_entry_t;

/* ---- node side ---- */
esp_err_t mhn_net_node_start(void);
esp_err_t mhn_net_send_report(const mhn_report_t *r);

/* ---- gateway side ---- */
esp_err_t mhn_net_gateway_start(void);
/* Copies the current node table into `out` (length MHN_MAX_NODES).
   Returns the number of active entries. Thread-safe. */
int mhn_net_gateway_snapshot(node_entry_t *out);

/* Inject this board's own report into the local table (for a combined
   node+gateway "host" board, so it appears on its own dashboard without
   transmitting). Requires mhn_net_gateway_start() to have run. */
esp_err_t mhn_net_gateway_self_report(const mhn_report_t *r);
