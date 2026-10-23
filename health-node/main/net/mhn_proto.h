#pragma once

#include <stdint.h>
#include "mhn_config.h"

/*
    Over-the-air report format shared by nodes (senders) and the gateway
    (receiver). Sent over ESP-NOW, which caps a payload at 250 bytes; this
    struct is ~90 bytes so it fits in one frame with room to grow.

    ESP-NOW is chosen over the TRUCO-style SoftAP+TCP because it is
    connectionless (no association/handshake), low power, and lets up to 4+
    nodes report to one gateway without IP plumbing. The gateway bridges to
    Wi-Fi for the dashboard. The S3 has no 802.15.4 radio, so LoRa/Zigbee are
    not options here (see docs/HARDWARE.md).
*/

#define MHN_PROTO_MAGIC   0xA7
#define MHN_PROTO_VERSION 1

typedef struct __attribute__((packed)) {
    uint8_t  magic;          /* MHN_PROTO_MAGIC */
    uint8_t  version;        /* MHN_PROTO_VERSION */
    uint8_t  node_id;        /* 1..4, set per node */
    uint8_t  state;          /* health_state_t: 0 OK, 1 WARN, 2 ALERT */
    uint8_t  health;         /* 0..100 smoothed score */
    uint8_t  num_bands;      /* how many entries of bands[] are valid */
    uint16_t seq;            /* increments per report, detects loss */
    uint32_t uptime_s;
    char     name[16];       /* machine name, NUL-terminated */
    float    deviation;      /* RMS z-score */
    float    rms_db;
    float    centroid_hz;
    float    top_tone_hz[MHN_NUM_TONES];
    uint8_t  bands[MHN_NUM_BANDS];  /* log band energy quantized 0..255 for UI */
} mhn_report_t;
