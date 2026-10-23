#pragma once

/* Top-level roles, chosen at boot (see main.c):
     node    - listen to a machine, transmit reports over ESP-NOW.
     host    - listen to a machine AND run the gateway + dashboard locally
               (best for a 2-board setup: both boards monitor, one also hosts).
     gateway - dashboard only, no local listening (for 3+ board setups). */
void app_run_node(void);
void app_run_host(void);
void app_run_gateway(void);
