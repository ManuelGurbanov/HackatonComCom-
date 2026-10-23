#pragma once

#include "esp_err.h"

/*
    Gateway-only Wi-Fi dashboard: brings up a SoftAP and an HTTP server that
    serves a single-page view of all nodes (health score, state, live mini
    spectrum, per-node history sparkline, and an alert log). Call after
    mhn_net_gateway_start() so ESP-NOW and the SoftAP share one channel.

    Connect a phone/laptop to the SoftAP and open http://192.168.4.1/.
    A /api/nodes JSON endpoint is also exposed for MQTT/bridge integrations.
*/
esp_err_t dashboard_start(void);
