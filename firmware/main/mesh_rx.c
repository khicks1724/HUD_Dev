#include "mesh_rx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_state.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hud_config.h"
#include "hud_takproto.h"
#include "imu_task.h"
#include "lwip/sockets.h"
#include "net_wifi.h"

static const char *TAG = "mesh";

#define SA_GROUP "239.2.3.1"
#define SA_PORT 6969

static int open_udp(uint16_t port, const char *group)
{
    const int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s < 0) return -1;
    const int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    struct sockaddr_in a = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(s, (struct sockaddr *)&a, sizeof(a)) < 0) {
        close(s);
        return -1;
    }
    if (group) {
        struct ip_mreq m = {0};
        inet_aton(group, &m.imr_multiaddr);
        m.imr_interface.s_addr = htonl(INADDR_ANY);
        if (setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP, &m, sizeof(m)) < 0) {
            ESP_LOGW(TAG, "join %s failed", group);
        }
    }
    const struct timeval tv = {.tv_sec = 0, .tv_usec = 100000};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    return s;
}

static void handle_text(char *msg)
{
    if (strncmp(msg, "POS,", 4) == 0) {
        double lat, lon, hae = 0;
        if (sscanf(msg + 4, "%lf,%lf,%lf", &lat, &lon, &hae) >= 2) {
            app_set_own(lat, lon, hae, POS_UDP);
        }
    } else if (strncmp(msg, "HDG,", 4) == 0) {
        float hdg, gain = 0.05f;
        if (sscanf(msg + 4, "%f,%f", &hdg, &gain) >= 1) {
            imu_set_heading(hdg, gain, HDG_PHONE);
        }
    }
}

static void mesh_task(void *arg)
{
    (void)arg;
    static char buf[4096];
    int sa = -1, ctl = -1;
    for (;;) {
        if (!net_wifi_wait(5000)) {
            continue;
        }
        if (sa < 0 && g_cfg.mesh_sa) sa = open_udp(SA_PORT, SA_GROUP);
        if (ctl < 0) ctl = open_udp(g_cfg.udp_port, NULL);

        const int socks[2] = {sa, ctl};
        for (int i = 0; i < 2; i++) {
            if (socks[i] < 0) continue;
            const int n = recv(socks[i], buf, sizeof(buf) - 1, 0);
            if (n <= 0) continue;
            buf[n] = '\0';
            hud_cot_event_t ev;
            const bool proto = hud_takproto_is_mesh((const uint8_t *)buf, (size_t)n);
            if (proto || strstr(buf, "<event")) {
                const bool ok = proto ? hud_takproto_decode_mesh((const uint8_t *)buf, (size_t)n, &ev)
                                      : hud_cot_parse_event(buf, (size_t)n, &ev);
                if (ok) {
                    app_ingest_cot(&ev, i == 0 ? "mesh" : "udp");
                    app_lock();
                    g_app.mesh_rx++;
                    app_unlock();
                }
            } else {
                handle_text(buf);
                app_lock();
                g_app.udp_rx++;
                app_unlock();
            }
        }
    }
}

void mesh_rx_start(void)
{
    xTaskCreatePinnedToCore(mesh_task, "mesh", 6144, NULL, 4, NULL, 0);
}
