/*
 * cot_dump - connect to a CoT TCP stream (e.g. tools/sim_server.py) and print
 * what the firmware's parser extracts, with ENU from a given observer.
 * End-to-end check of hud_cot + hud_geo against a live feed.
 *
 *   ./cot_dump 127.0.0.1 8087 36.5967 -121.875 [seconds]
 */
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock_t;
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int sock_t;
#define closesocket close
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "hud_cot.h"
#include "hud_geo.h"

static hud_lla_t g_obs;
static int g_count;

static void on_ev(const hud_cot_event_t *ev, void *ctx)
{
    (void)ctx;
    hud_lla_t t = {ev->lat, ev->lon, ev->hae};
    hud_vec3d_t enu;
    hud_lla_to_enu(&g_obs, &t, &enu);
    hud_polar_t p;
    hud_enu_to_polar(&enu, &p);
    printf("%-18s %-8s %-14s brg %6.1f el %5.1f rng %7.0f m  life %lld ms\n", ev->uid, ev->callsign, ev->type,
           p.bearing_deg, p.elevation_deg, p.range_m, (long long)(ev->stale_ms - ev->time_ms));
    g_count++;
}

int main(int argc, char **argv)
{
    if (argc < 5) {
        fprintf(stderr, "usage: %s host port obs_lat obs_lon [seconds]\n", argv[0]);
        return 2;
    }
#ifdef _WIN32
    WSADATA w;
    WSAStartup(MAKEWORD(2, 2), &w);
#endif
    g_obs = (hud_lla_t){atof(argv[3]), atof(argv[4]), 20.0};
    const int secs = argc > 5 ? atoi(argv[5]) : 3;
    struct addrinfo hints = {0}, *res;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(argv[1], argv[2], &hints, &res) != 0) return 1;
    sock_t s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (connect(s, res->ai_addr, (int)res->ai_addrlen) != 0) {
        fprintf(stderr, "connect failed\n");
        return 1;
    }
    static hud_cot_stream_t st;
    hud_cot_stream_init(&st);
    const time_t end = time(NULL) + secs;
    char buf[1500];
    while (time(NULL) < end) {
        const int n = recv(s, buf, sizeof(buf), 0);
        if (n <= 0) break;
        hud_cot_stream_feed(&st, buf, (size_t)n, on_ev, NULL);
    }
    closesocket(s);
    printf("parsed %d events (bad %u, dropped bytes %u)\n", g_count, st.events_bad, st.bytes_dropped);
    return g_count > 0 ? 0 : 1;
}
