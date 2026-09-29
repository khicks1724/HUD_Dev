/*
 * hud_config.h - persistent settings, stored in the "hudcfg" NVS partition.
 *
 * Defaults come from Kconfig (idf.py menuconfig). tools/provision.py builds a
 * hudcfg image with Wi-Fi, TAK and certificate entries; the serial console
 * edits individual keys at runtime.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef enum { TAK_OFF = 0, TAK_TCP = 1, TAK_TLS = 2 } tak_proto_t;

typedef struct {
    char wifi_ssid[33];
    char wifi_pass[65];

    tak_proto_t tak_proto;
    char tak_host[64];
    uint16_t tak_port;
    char tak_server_cn[64]; /* expected server cert CN if it differs from host */

    char own_uid[64];       /* ATAK device supplying own position */
    char own_callsign[32];
    char hud_uid[48];
    char hud_callsign[32];
    bool send_sa;           /* publish own SA (only useful with onboard GNSS) */

    bool fake_targets;
    bool mesh_sa;
    uint16_t udp_port;

    bool man_valid;         /* manual own position */
    double man_lat, man_lon, man_hae;

    float hfov_deg, vfov_deg;
    float bore_dx_px, bore_dy_px;       /* optical centre offset */
    float bore_pitch_deg, bore_roll_deg; /* level trim */
    float mount[9];                     /* sensor -> HUD body rotation, row major */
    float mag_offset[3];                /* hard-iron, gauss */
    float th_shift_x, th_shift_y;       /* thermal camera alignment, HUD px */
    float th_roll_deg;                  /* thermal camera roll correction */

    float max_range_m;
    float radar_range_m;
    int brightness;
    bool mirror_x, mirror_y;
    int mode;
    uint32_t layers;                    /* HUD_L_* mask (what NORMAL mode draws) */
} hud_config_t;

extern hud_config_t g_cfg;

esp_err_t hud_config_load(void);
esp_err_t hud_config_save(void);
void hud_config_defaults(hud_config_t *c);

/* Certificates / keys stored as NUL-terminated PEM strings.
 * name: "ca", "cert", "key". Caller frees *out. */
esp_err_t hud_config_get_pem(const char *name, char **out, size_t *len);
