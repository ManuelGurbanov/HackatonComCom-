#include "dashboard.h"
#include "mhn_net.h"

#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"

static const char *TAG = "dashboard";

#define AP_SSID "MHN-GATEWAY"
#define AP_PASS "machinehealth"

/* Single-page app. Polls /api/nodes, keeps per-node history in the browser,
   and renders cards + sparklines + an alert log. Kept intentionally small. */
static const char INDEX_HTML[] =
"<!doctype html><html><head><meta charset=utf-8>"
"<meta name=viewport content='width=device-width,initial-scale=1'>"
"<title>Machine Health</title><style>"
"body{font-family:system-ui,sans-serif;margin:0;background:#111;color:#eee}"
"h1{font-size:18px;padding:12px 16px;margin:0;background:#1b1b1b}"
".grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(260px,1fr));gap:12px;padding:12px}"
".card{background:#1e1e1e;border-radius:10px;padding:12px;border-left:6px solid #555}"
".ok{border-color:#2ecc71}.warn{border-color:#f1c40f}.alert{border-color:#e74c3c}"
".name{font-weight:600}.score{font-size:40px;font-weight:700}"
".state{font-size:12px;letter-spacing:1px}.meta{font-size:11px;color:#999;margin-top:4px}"
".spec{display:flex;align-items:flex-end;height:40px;gap:1px;margin:8px 0}"
".spec i{flex:1;background:#3498db;display:block}"
"canvas{width:100%;height:40px;background:#161616;border-radius:4px}"
"#log{padding:12px;font-size:12px}#log div{padding:3px 0;border-bottom:1px solid #222}"
".stale{opacity:.4}"
"</style></head><body><h1>Machine Health Listening &mdash; gateway</h1>"
"<div class=grid id=grid></div><h1>Alert log</h1><div id=log></div>"
"<script>"
"let hist={},log=[],last={};"
"const cls=s=>s==2?'alert':s==1?'warn':'ok',nm=s=>s==2?'ALERT':s==1?'WARNING':'OK';"
"async function tick(){let r;try{r=await(await fetch('/api/nodes')).json()}catch(e){return}"
"let g='';for(const n of r.nodes){"
"hist[n.id]=(hist[n.id]||[]);hist[n.id].push(n.health);if(hist[n.id].length>120)hist[n.id].shift();"
"if(last[n.id]!==undefined&&last[n.id]!=n.state){log.unshift(new Date().toLocaleTimeString()+'  '+n.name+' -> '+nm(n.state));if(log.length>50)log.pop();}"
"last[n.id]=n.state;"
"let bars=n.bands.map(b=>`<i style=height:${(b/255*40)|0}px></i>`).join('');"
"let stale=n.age>8?'stale':'';"
"g+=`<div class='card ${cls(n.state)} ${stale}'><div class=name>${n.name} <span style=color:#777>#${n.id}</span></div>"
"<div class=score>${n.health}</div><div class=state>${nm(n.state)}</div>"
"<div class=spec>${bars}</div><canvas id=c${n.id} width=240 height=40></canvas>"
"<div class=meta>dev ${n.deviation.toFixed(1)}z &middot; rms ${n.rms_db.toFixed(0)}dB &middot; cen ${n.centroid_hz|0}Hz &middot; seen ${n.age}s &middot; lost ${n.lost}</div></div>`;}"
"document.getElementById('grid').innerHTML=g;"
"for(const n of r.nodes){let c=document.getElementById('c'+n.id);if(!c)continue;let x=c.getContext('2d'),h=hist[n.id];"
"x.clearRect(0,0,240,40);x.strokeStyle='#2ecc71';x.beginPath();h.forEach((v,i)=>{let px=i/120*240,py=40-v/100*40;i?x.lineTo(px,py):x.moveTo(px,py)});x.stroke();}"
"document.getElementById('log').innerHTML=log.map(l=>`<div>${l}</div>`).join('');}"
"setInterval(tick,2000);tick();"
"</script></body></html>";

static esp_err_t index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t api_handler(httpd_req_t *req)
{
    static char buf[MHN_MAX_NODES * 500 + 64];
    static node_entry_t nodes[MHN_MAX_NODES];
    int n = mhn_net_gateway_snapshot(nodes);
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);

    int off = snprintf(buf, sizeof(buf), "{\"nodes\":[");
    for (int i = 0; i < n; i++) {
        const mhn_report_t *r = &nodes[i].last;
        off += snprintf(buf + off, sizeof(buf) - off,
            "%s{\"id\":%u,\"name\":\"%s\",\"health\":%u,\"state\":%u,"
            "\"deviation\":%.2f,\"rms_db\":%.1f,\"centroid_hz\":%.0f,"
            "\"age\":%u,\"lost\":%u,\"bands\":[",
            i ? "," : "", r->node_id, r->name, r->health, r->state,
            r->deviation, r->rms_db, r->centroid_hz,
            (now - nodes[i].last_rx_ms) / 1000, nodes[i].lost);
        for (int b = 0; b < r->num_bands && b < MHN_NUM_BANDS; b++) {
            off += snprintf(buf + off, sizeof(buf) - off, "%s%u", b ? "," : "", r->bands[b]);
        }
        off += snprintf(buf + off, sizeof(buf) - off, "]}");
    }
    snprintf(buf + off, sizeof(buf) - off, "]}");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t softap_start(void)
{
    /* The AP netif was created in mhn_net's wifi_bootstrap (APSTA). Here we
       only apply the SSID/password/channel. */
    wifi_config_t ap = {
        .ap = {
            .ssid = AP_SSID,
            .ssid_len = strlen(AP_SSID),
            .password = AP_PASS,
            .channel = MHN_FIXED_CHANNEL,
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &ap), TAG, "ap cfg");
    ESP_LOGI(TAG, "SoftAP '%s' pass '%s' -> http://192.168.4.1/", AP_SSID, AP_PASS);
    return ESP_OK;
}

esp_err_t dashboard_start(void)
{
    ESP_RETURN_ON_ERROR(softap_start(), TAG, "softap");

    httpd_handle_t srv = NULL;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.lru_purge_enable = true;
    ESP_RETURN_ON_ERROR(httpd_start(&srv, &cfg), TAG, "httpd");

    httpd_uri_t idx = {.uri = "/", .method = HTTP_GET, .handler = index_handler};
    httpd_uri_t api = {.uri = "/api/nodes", .method = HTTP_GET, .handler = api_handler};
    httpd_register_uri_handler(srv, &idx);
    httpd_register_uri_handler(srv, &api);
    return ESP_OK;
}
