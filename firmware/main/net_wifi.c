#include "net_wifi.h"

#include <stdio.h>
#include <string.h>

#include "app_state.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "hud_config.h"

static const char *TAG = "wifi";
static EventGroupHandle_t s_ev;
#define BIT_CONNECTED BIT0

static void on_sntp_sync(struct timeval *tv)
{
    (void)tv;
    app_lock();
    g_app.time_valid = true;
    app_unlock();
    ESP_LOGI(TAG, "time synchronised");
}

static void handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_ev, BIT_CONNECTED);
        app_lock();
        g_app.link_state = 0;
        g_app.ip[0] = '\0';
        app_unlock();
        esp_wifi_connect(); /* the driver backs off internally between attempts */
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *e = data;
        app_lock();
        if (g_app.link_state < 1) g_app.link_state = 1;
        snprintf(g_app.ip, sizeof(g_app.ip), IPSTR, IP2STR(&e->ip_info.ip));
        app_unlock();
        ESP_LOGI(TAG, "got ip " IPSTR, IP2STR(&e->ip_info.ip));
        xEventGroupSetBits(s_ev, BIT_CONNECTED);
    }
}

void net_wifi_start(void)
{
    s_ev = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    const wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, handler, NULL));

    wifi_config_t wc = {0};
    memcpy(wc.sta.ssid, g_cfg.wifi_ssid, strnlen(g_cfg.wifi_ssid, sizeof(wc.sta.ssid)));
    memcpy(wc.sta.password, g_cfg.wifi_pass, strnlen(g_cfg.wifi_pass, sizeof(wc.sta.password)));
    wc.sta.threshold.authmode = g_cfg.wifi_pass[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    wc.sta.pmf_cfg.capable = true;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    /* Modem sleep off: lower latency for the CoT stream. */
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    if (!g_cfg.wifi_ssid[0]) {
        ESP_LOGW(TAG, "no SSID configured: use the console 'wifi <ssid> <pass>' command");
        return;
    }
    ESP_ERROR_CHECK(esp_wifi_start());

    /* SNTP: the DHCP-provided server first (tactical networks rarely reach the
     * internet), then pool.ntp.org. */
    esp_sntp_config_t sc = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    sc.server_from_dhcp = true;
    sc.renew_servers_after_new_IP = true;
    sc.index_of_first_server = 1;
    sc.sync_cb = on_sntp_sync;
    esp_netif_sntp_init(&sc);
    ESP_LOGI(TAG, "connecting to '%s'", g_cfg.wifi_ssid);
}

bool net_wifi_connected(void)
{
    return s_ev && (xEventGroupGetBits(s_ev) & BIT_CONNECTED);
}

bool net_wifi_wait(int timeout_ms)
{
    if (!s_ev) return false;
    return xEventGroupWaitBits(s_ev, BIT_CONNECTED, pdFALSE, pdTRUE, pdMS_TO_TICKS(timeout_ms)) & BIT_CONNECTED;
}
