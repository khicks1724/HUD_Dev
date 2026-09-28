#include "telemetry.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_state.h"
#include "board.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hud_config.h"
#include "imu_task.h"
#include "render_task.h"
#include "thermal.h"

static const char *TAG = "telem";

static volatile bool s_serial_on;
static volatile int s_serial_hz = 5;
static TaskHandle_t s_serial_task;

/* ---------- JSON ---------- */

typedef struct {
    char *p;
    size_t left;
    bool overflow;
} jb_t;

static void jb_printf(jb_t *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void jb_printf(jb_t *b, const char *fmt, ...)
{
    if (b->overflow) return;
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(b->p, b->left, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= b->left) {
        b->overflow = true;
        return;
    }
    b->p += n;
    b->left -= (size_t)n;
}

static void jb_str(jb_t *b, const char *s)
{
    jb_printf(b, "\"");
    for (; *s && !b->overflow; s++) {
        const unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') jb_printf(b, "\\%c", c);
        else if (c < 0x20) jb_printf(b, "\\u%04x", c);
        else jb_printf(b, "%c", c);
    }
    jb_printf(b, "\"");
}

int telemetry_state_json(char *buf, size_t cap, int max_targets)
{
    jb_t b = {buf, cap, false};
    const hud_quat_t q = imu_get_quat();
    hud_euler_t e;
    hud_quat_to_euler(q, &e);

    app_lock();
    const hud_lla_t own = g_app.own;
    const bool own_ok = g_app.own_valid;
    jb_printf(&b,
              "{\"t\":%lld,\"att\":{\"h\":%.2f,\"p\":%.2f,\"r\":%.2f,\"q\":[%.5f,%.5f,%.5f,%.5f],\"src\":\"%s\"},"
              "\"own\":{\"ok\":%d,\"src\":\"%s\",\"lat\":%.7f,\"lon\":%.7f,\"hae\":%.1f},"
              "\"link\":%d,\"mode\":%d,\"thermal\":%d,\"fov\":[%.1f,%.1f],\"bore\":[%.1f,%.1f],"
              "\"ip\":\"%s\",\"tracks\":%d,\"targets\":[",
              (long long)app_mono_ms(), e.heading_deg, e.pitch_deg, e.roll_deg, q.w, q.x, q.y, q.z,
              imu_heading_source_name(), own_ok, app_pos_src_name(g_app.pos_src), own.lat_deg, own.lon_deg,
              own.hae_m, g_app.link_state, (int)g_app.mode, (int)g_app.thermal_mode, g_cfg.hfov_deg, g_cfg.vfov_deg,
              g_cfg.bore_dx_px, g_cfg.bore_dy_px, g_app.ip, g_app.targets->count);
    int n = 0;
    const int64_t now = app_mono_ms();
    for (int i = 0; i < HUD_MAX_TARGETS && n < max_targets && own_ok; i++) {
        const hud_target_t *t = &g_app.targets->items[i];
        if (!t->used) continue;
        const hud_lla_t tl = {t->ev.lat, t->ev.lon, t->ev.hae};
        hud_vec3d_t enu;
        hud_lla_to_enu(&own, &tl, &enu);
        jb_printf(&b, "%s{\"uid\":", n ? "," : "");
        jb_str(&b, t->ev.uid);
        jb_printf(&b, ",\"cs\":");
        jb_str(&b, t->ev.callsign);
        jb_printf(&b, ",\"type\":\"%s\",\"a\":%d,\"d\":%d,\"lat\":%.7f,\"lon\":%.7f,\"hae\":%.1f,"
                      "\"e\":[%.1f,%.1f,%.1f],\"age\":%.1f}",
                  t->ev.type, (int)t->ev.affil, (int)t->ev.dim, t->ev.lat, t->ev.lon, t->ev.hae, enu.x, enu.y,
                  enu.z, (double)(now - t->updated_mono_ms) / 1000.0);
        n++;
    }
    app_unlock();
    jb_printf(&b, "]}");
    return b.overflow ? -1 : (int)(cap - b.left);
}

/* ---------- serial stream ---------- */

static void serial_task(void *arg)
{
    (void)arg;
    char *buf = malloc(4096);
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000 / (s_serial_hz > 0 ? s_serial_hz : 5)));
        if (!s_serial_on || !buf) continue;
        /* 115200 baud ~ 11 kB/s: keep each line small. */
        if (telemetry_state_json(buf, 4096, 20) > 0) {
            printf("@HUD %s\n", buf);
            fflush(stdout);
        }
    }
}

void telemetry_serial_enable(bool on, int hz)
{
    s_serial_hz = hz < 1 ? 1 : (hz > 20 ? 20 : hz);
    s_serial_on = on;
    if (on && !s_serial_task) {
        xTaskCreatePinnedToCore(serial_task, "telem_ser", 4096, NULL, 2, &s_serial_task, 0);
    }
}

/* ---------- HTTP ---------- */

static void cors(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
}

static esp_err_t h_state(httpd_req_t *req)
{
    char *buf = malloc(16384);
    if (!buf) return httpd_resp_send_500(req);
    const int n = telemetry_state_json(buf, 16384, 64);
    cors(req);
    httpd_resp_set_type(req, "application/json");
    const esp_err_t err = n > 0 ? httpd_resp_send(req, buf, n) : httpd_resp_send_500(req);
    free(buf);
    return err;
}

static esp_err_t h_frame(httpd_req_t *req)
{
    const uint16_t *fb = render_framebuffer();
    if (!fb) return httpd_resp_send_500(req);
    cors(req);
    httpd_resp_set_type(req, "application/octet-stream");
    /* Sent straight from the live buffer: may tear, fine for a viewer. */
    return httpd_resp_send(req, (const char *)fb, BOARD_LCD_W * BOARD_LCD_H * 2);
}

static int query_int(httpd_req_t *req, const char *key, int dflt)
{
    char q[64], v[16];
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK &&
        httpd_query_key_value(q, key, v, sizeof(v)) == ESP_OK) {
        return atoi(v);
    }
    return dflt;
}

static esp_err_t h_mode(httpd_req_t *req)
{
    const int m = query_int(req, "m", -1);
    if (m >= 0) {
        app_lock();
        g_app.mode = (hud_mode_t)(m % HUD_MODE_COUNT);
        app_unlock();
    }
    cors(req);
    return httpd_resp_sendstr(req, "ok");
}

static esp_err_t h_thermal(httpd_req_t *req)
{
    thermal_cycle_mode();
    cors(req);
    return httpd_resp_sendstr(req, "ok");
}

static esp_err_t h_hdg(httpd_req_t *req)
{
    const int d = query_int(req, "deg", -1);
    if (d >= 0) imu_set_heading((float)d, 1.0f, HDG_BORE);
    cors(req);
    return httpd_resp_sendstr(req, "ok");
}

void telemetry_http_start(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.core_id = 0;
    cfg.stack_size = 6144;
    httpd_handle_t srv = NULL;
    if (httpd_start(&srv, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "http server failed to start");
        return;
    }
    const httpd_uri_t uris[] = {
        {.uri = "/api/state", .method = HTTP_GET, .handler = h_state},
        {.uri = "/api/frame", .method = HTTP_GET, .handler = h_frame},
        {.uri = "/api/mode", .method = HTTP_GET, .handler = h_mode},
        {.uri = "/api/thermal", .method = HTTP_GET, .handler = h_thermal},
        {.uri = "/api/hdg", .method = HTTP_GET, .handler = h_hdg},
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        httpd_register_uri_handler(srv, &uris[i]);
    }
    ESP_LOGI(TAG, "live view API on http://<hud-ip>/api/state");
}
