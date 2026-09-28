#pragma once

#include <stdbool.h>

/* Starts Wi-Fi STA with g_cfg credentials and keeps reconnecting.
 * Also starts SNTP once an IP is obtained. */
void net_wifi_start(void);
bool net_wifi_connected(void);
/* Block until connected or timeout (ms). */
bool net_wifi_wait(int timeout_ms);
