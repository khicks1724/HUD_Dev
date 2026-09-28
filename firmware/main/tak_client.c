#include "tak_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_state.h"
#include "esp_log.h"
#include "esp_tls.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hud_config.h"
#include "lwip/sockets.h"
#include "net_wifi.h"

static const char *TAG = "tak";

#define PING_PERIOD_MS 15000
#define SA_PERIOD_MS 5000
#define RX_CHUNK 1024

static hud_cot_stream_t *s_stream; /* 8 KB, heap */

static void on_event(const hud_cot_event_t *ev, void *ctx)
{
    (void)ctx;
    app_ingest_cot(ev, "tak");
    app_lock();
    g_app.cot_rx++;
    app_unlock();
}

static void set_link(int state)
{
    app_lock();
    g_app.link_state = state;
    app_unlock();
}

static int write_all(esp_tls_t *tls, const char *buf, size_t len)
{
    size_t done = 0;
    while (done < len) {
        const ssize_t n = esp_tls_conn_write(tls, buf + done, len - done);
        if (n > 0) {
            done += (size_t)n;
        } else if (n != ESP_TLS_ERR_SSL_WANT_READ && n != ESP_TLS_ERR_SSL_WANT_WRITE) {
            return -1;
        }
    }
    return (int)done;
}

static void session(void)
{
    char *ca = NULL, *cert = NULL, *key = NULL;
    size_t ca_len = 0, cert_len = 0, key_len = 0;
    esp_tls_cfg_t cfg = {
        .timeout_ms = 10000,
        .non_block = false,
    };

    if (g_cfg.tak_proto == TAK_TLS) {
        hud_config_get_pem("ca", &ca, &ca_len);
        hud_config_get_pem("cert", &cert, &cert_len);
        hud_config_get_pem("key", &key, &key_len);
        if (!ca || !cert || !key) {
            app_set_error("TLS: provision ca/cert/key");
            ESP_LOGE(TAG, "TLS selected but certs missing (run tools/provision.py)");
            free(ca);
            free(cert);
            free(key);
            vTaskDelay(pdMS_TO_TICKS(10000));
            return;
        }
        cfg.cacert_buf = (const unsigned char *)ca;
        cfg.cacert_bytes = ca_len;
        cfg.clientcert_buf = (const unsigned char *)cert;
        cfg.clientcert_bytes = cert_len;
        cfg.clientkey_buf = (const unsigned char *)key;
        cfg.clientkey_bytes = key_len;
        if (g_cfg.tak_server_cn[0]) {
            cfg.common_name = g_cfg.tak_server_cn;
        }
    } else {
        cfg.is_plain_tcp = true;
    }

    esp_tls_t *tls = esp_tls_init();
    if (!tls) {
        goto out;
    }
    ESP_LOGI(TAG, "connecting %s://%s:%u", g_cfg.tak_proto == TAK_TLS ? "ssl" : "tcp", g_cfg.tak_host,
             g_cfg.tak_port);
    if (esp_tls_conn_new_sync(g_cfg.tak_host, (int)strlen(g_cfg.tak_host), g_cfg.tak_port, &cfg, tls) != 1) {
        ESP_LOGW(TAG, "connect failed");
        app_set_error("TAK connect failed");
        esp_tls_conn_destroy(tls);
        goto out;
    }
    ESP_LOGI(TAG, "connected");
    set_link(2);
    app_set_error("");

    /* Short read timeout so the loop can send pings. */
    int sock = -1;
    esp_tls_get_conn_sockfd(tls, &sock);
    if (sock >= 0) {
        const struct timeval tv = {.tv_sec = 0, .tv_usec = 200000};
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }

    hud_cot_stream_init(s_stream);
    char rx[RX_CHUNK];
    char tx[1024];
    int64_t last_ping = 0, last_sa = 0;

    for (;;) {
        const ssize_t n = esp_tls_conn_read(tls, rx, sizeof(rx));
        if (n > 0) {
            hud_cot_stream_feed(s_stream, rx, (size_t)n, on_event, NULL);
        } else if (n == 0) {
            ESP_LOGW(TAG, "server closed connection");
            break;
        } else if (n != ESP_TLS_ERR_SSL_WANT_READ && n != ESP_TLS_ERR_SSL_WANT_WRITE && n != -1) {
            /* -1 with EAGAIN is the receive timeout on plain TCP */
            ESP_LOGW(TAG, "read error %d", (int)n);
            break;
        }

        const int64_t now = app_mono_ms();
        const int64_t utc = app_utc_ms();
        if (now - last_ping > PING_PERIOD_MS) {
            last_ping = now;
            const int len = hud_cot_build_ping(tx, sizeof(tx), g_cfg.hud_uid, utc ? utc : now);
            if (len > 0 && write_all(tls, tx, (size_t)len) < 0) break;
        }
        if (g_cfg.send_sa && now - last_sa > SA_PERIOD_MS && utc) {
            last_sa = now;
            app_lock();
            const bool ok = g_app.own_valid && g_app.pos_src == POS_GNSS;
            const hud_lla_t own = g_app.own;
            const float crs = g_app.own_course_deg, spd = g_app.own_speed_mps;
            app_unlock();
            if (ok) {
                const int len = hud_cot_build_sa(tx, sizeof(tx), g_cfg.hud_uid, g_cfg.hud_callsign, NULL, own.lat_deg,
                                                 own.lon_deg, own.hae_m, 5.0f, crs, spd, utc, 30);
                if (len > 0 && write_all(tls, tx, (size_t)len) < 0) break;
            }
        }
    }
    esp_tls_conn_destroy(tls);
out:
    set_link(net_wifi_connected() ? 1 : 0);
    free(ca);
    free(cert);
    free(key);
}

static void tak_task(void *arg)
{
    (void)arg;
    int backoff_ms = 2000;
    for (;;) {
        if (g_cfg.tak_proto == TAK_OFF || !g_cfg.tak_host[0]) {
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }
        if (!net_wifi_wait(5000)) {
            continue;
        }
        if (g_cfg.tak_proto == TAK_TLS) {
            /* Certificate date checks need wall-clock time. */
            for (int i = 0; i < 20 && !app_utc_ms(); i++) {
                vTaskDelay(pdMS_TO_TICKS(500));
            }
            if (!app_utc_ms()) {
                ESP_LOGW(TAG, "no SNTP time yet; TLS date validation may fail");
            }
        }
        const int64_t started = app_mono_ms();
        session();
        /* Reset backoff after a session that lasted a while. */
        backoff_ms = (app_mono_ms() - started > 30000) ? 2000 : (backoff_ms * 2 > 60000 ? 60000 : backoff_ms * 2);
        vTaskDelay(pdMS_TO_TICKS(backoff_ms));
    }
}

void tak_client_start(void)
{
    s_stream = calloc(1, sizeof(hud_cot_stream_t));
    xTaskCreatePinnedToCore(tak_task, "tak", 8192, NULL, 5, NULL, 0);
}
