/*
 * console_cmds.c - USB-C serial console (2 Mbit/s, CH343 COM port; see usb_link.h).
 *
 *   idf.py -p COMx monitor      then type "help"
 *
 * Settings changed here take effect immediately where possible; "save"
 * writes them to the hudcfg partition, "reboot" applies Wi-Fi/TAK changes.
 * "stream on" makes the HUD print its state as "@HUD {json}" lines so the
 * web page can live-view it over Web Serial (docs/LIVE_VIEW.md).
 */
#include "console_cmds.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_state.h"
#include "esp_console.h"
#include "esp_log.h"
#include "esp_system.h"
#include "hud_config.h"
#include "imu_task.h"
#include "lcd_st7789.h"
#include "render_task.h"
#include "telemetry.h"
#include "thermal.h"
#include "usb_link.h"

static void set_str(char *dst, size_t cap, const char *src)
{
    snprintf(dst, cap, "%s", src);
}

static int cmd_status(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    hud_euler_t e;
    hud_quat_to_euler(imu_get_quat(), &e);
    app_lock();
    printf("ip=%s link=%d time=%s err='%s'\n", g_app.ip, g_app.link_state, g_app.time_valid ? "ok" : "no",
           g_app.last_error);
    printf("own=%s valid=%d %.6f %.6f %.1f\n", app_pos_src_name(g_app.pos_src), g_app.own_valid, g_app.own.lat_deg,
           g_app.own.lon_deg, g_app.own.hae_m);
    printf("tracks=%d cot=%lu mesh=%lu udp=%lu\n", g_app.targets->count, (unsigned long)g_app.cot_rx,
           (unsigned long)g_app.mesh_rx, (unsigned long)g_app.udp_rx);
    app_unlock();
    printf("att hdg=%.1f pitch=%.1f roll=%.1f src=%s\n", e.heading_deg, e.pitch_deg, e.roll_deg,
           imu_heading_source_name());
    printf("wifi='%s' tak=%d %s:%u own_uid='%s' own_cs='%s' fake=%d mesh=%d\n", g_cfg.wifi_ssid, g_cfg.tak_proto,
           g_cfg.tak_host, g_cfg.tak_port, g_cfg.own_uid, g_cfg.own_callsign, g_cfg.fake_targets, g_cfg.mesh_sa);
    printf("fov=%.1fx%.1f bore=(%.1f,%.1f)px trim p=%.2f r=%.2f mirror=%d,%d\n", g_cfg.hfov_deg, g_cfg.vfov_deg,
           g_cfg.bore_dx_px, g_cfg.bore_dy_px, g_cfg.bore_pitch_deg, g_cfg.bore_roll_deg, g_cfg.mirror_x,
           g_cfg.mirror_y);
    return 0;
}

static int cmd_wifi(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: wifi <ssid> [password]\n");
        return 1;
    }
    set_str(g_cfg.wifi_ssid, sizeof(g_cfg.wifi_ssid), argv[1]);
    set_str(g_cfg.wifi_pass, sizeof(g_cfg.wifi_pass), argc > 2 ? argv[2] : "");
    printf("ok - 'save' then 'reboot'\n");
    return 0;
}

static int cmd_tak(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: tak off | tak <host> <port> <tcp|tls> [server_cn]\n");
        return 1;
    }
    if (strcmp(argv[1], "off") == 0) {
        g_cfg.tak_proto = TAK_OFF;
        return 0;
    }
    if (argc < 4) return 1;
    set_str(g_cfg.tak_host, sizeof(g_cfg.tak_host), argv[1]);
    g_cfg.tak_port = (uint16_t)atoi(argv[2]);
    g_cfg.tak_proto = strcmp(argv[3], "tls") == 0 ? TAK_TLS : TAK_TCP;
    set_str(g_cfg.tak_server_cn, sizeof(g_cfg.tak_server_cn), argc > 4 ? argv[4] : "");
    printf("ok - 'save' then 'reboot'\n");
    return 0;
}

static int cmd_own(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "clear") == 0) {
        g_cfg.own_uid[0] = '\0';
        g_cfg.own_callsign[0] = '\0';
        return 0;
    }
    if (argc < 3) {
        printf("usage: own uid <ATAK uid> | own cs <callsign> | own clear\n");
        return 1;
    }
    if (strcmp(argv[1], "uid") == 0) {
        set_str(g_cfg.own_uid, sizeof(g_cfg.own_uid), argv[2]);
    } else if (strcmp(argv[1], "cs") == 0) {
        set_str(g_cfg.own_callsign, sizeof(g_cfg.own_callsign), argv[2]);
    } else {
        return 1;
    }
    return 0;
}

static int cmd_pos(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "off") == 0) {
        g_cfg.man_valid = false;
        return 0;
    }
    if (argc < 3) {
        printf("usage: pos <lat> <lon> [hae_m] | pos off\n");
        return 1;
    }
    g_cfg.man_lat = atof(argv[1]);
    g_cfg.man_lon = atof(argv[2]);
    g_cfg.man_hae = argc > 3 ? atof(argv[3]) : 0.0;
    g_cfg.man_valid = true;
    app_set_own(g_cfg.man_lat, g_cfg.man_lon, g_cfg.man_hae, POS_MANUAL);
    return 0;
}

static int cmd_hdg(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: hdg <true_heading_deg>   (snap current view heading)\n");
        return 1;
    }
    const float gain = argc > 2 ? (float)atof(argv[2]) : 1.0f;
    imu_set_heading((float)atof(argv[1]), gain, gain >= 1.0f ? HDG_BORE : HDG_PHONE);
    return 0;
}

/* Input from a USB-attached ATAK phone (plugin writes these lines):
 *   trk <uid> <type> <lat> <lon> <hae> <stale_s> <callsign>
 *   fix <lat> <lon> <hae>            (phone GPS = own position)
 * Exactly 8 tokens: the plugin sends spaces in names as '_' ("-" = no name),
 * so a longer line is two lines merged on the serial link and is dropped. */
static int cmd_trk(int argc, char **argv)
{
    if (argc != 8) {
        printf("usage: trk <uid> <type> <lat> <lon> <hae> <stale_s> <callsign>\n");
        return 1;
    }
    char *e3, *e4;
    const double lat = strtod(argv[3], &e3), lon = strtod(argv[4], &e4);
    if (*e3 || *e4 || !app_ingest_track(argv[1], argv[2], lat, lon, atof(argv[5]), atoi(argv[6]), argv[7], "usb")) {
        printf("trk: rejected\n");
        return 1;
    }
    return 0;
}

/* show [<mask>|full|clean|combat|nav|+name|-name]: what the HUD draws */
static const struct {
    const char *name;
    uint32_t bit;
} k_layers[] = {
    {"tape", HUD_L_TAPE},   {"horizon", HUD_L_HORIZON}, {"reticle", HUD_L_RETICLE}, {"radar", HUD_L_RADAR},
    {"status", HUD_L_STATUS}, {"names", HUD_L_NAMES},   {"ranges", HUD_L_RANGES},   {"info", HUD_L_INFO},
    {"edge", HUD_L_EDGE},   {"enemyonly", HUD_L_ENEMY_ONLY},
};

static int cmd_show(int argc, char **argv)
{
    static const char *layouts[] = {"full", "clean", "combat", "nav"};
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        bool done = false;
        for (int l = 0; l < HUD_LAYOUT_COUNT && !done; l++) {
            if (strcmp(a, layouts[l]) == 0) {
                g_cfg.layers = hud_layout_mask((hud_layout_t)l);
                done = true;
            }
        }
        if (!done && (a[0] == '+' || a[0] == '-')) {
            for (size_t k = 0; k < sizeof(k_layers) / sizeof(k_layers[0]); k++) {
                if (strcmp(a + 1, k_layers[k].name) == 0) {
                    g_cfg.layers = a[0] == '+' ? (g_cfg.layers | k_layers[k].bit) : (g_cfg.layers & ~k_layers[k].bit);
                    done = true;
                }
            }
        }
        if (!done) {
            char *end;
            const unsigned long v = strtoul(a, &end, 0);
            if (*end) {
                printf("show: unknown '%s'\n", a);
                return 1;
            }
            g_cfg.layers = (uint32_t)v & HUD_L_ALL;
        }
    }
    printf("layers=%lu:", (unsigned long)g_cfg.layers);
    for (size_t k = 0; k < sizeof(k_layers) / sizeof(k_layers[0]); k++) {
        if (g_cfg.layers & k_layers[k].bit) printf(" %s", k_layers[k].name);
    }
    printf("\n");
    return 0;
}

/* range <show_m> [radar_m]: hide units beyond show_m; radar scale */
static int cmd_range(int argc, char **argv)
{
    if (argc >= 2) {
        const float m = (float)atof(argv[1]);
        if (m < 100.0f) {
            printf("range: too small\n");
            return 1;
        }
        g_cfg.max_range_m = m;
        g_cfg.radar_range_m = argc >= 3 ? (float)atof(argv[2]) : m / 4.0f;
    }
    printf("range=%.0f radar=%.0f\n", g_cfg.max_range_m, g_cfg.radar_range_m);
    return 0;
}

static int cmd_fix(int argc, char **argv)
{
    if (argc < 3) {
        printf("usage: fix <lat> <lon> [hae]\n");
        return 1;
    }
    app_set_own(atof(argv[1]), atof(argv[2]), argc > 3 ? atof(argv[3]) : 0.0, POS_USB);
    return 0;
}

static int toggle(bool *flag, int argc, char **argv, const char *name)
{
    if (argc < 2) {
        printf("%s is %s\n", name, *flag ? "on" : "off");
        return 0;
    }
    *flag = strcmp(argv[1], "on") == 0;
    return 0;
}

static int cmd_fake(int argc, char **argv)
{
    return toggle(&g_cfg.fake_targets, argc, argv, "fake");
}

static int cmd_mesh(int argc, char **argv)
{
    return toggle(&g_cfg.mesh_sa, argc, argv, "mesh");
}

static int cmd_stream(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: stream on|off [hz]\n");
        return 1;
    }
    telemetry_serial_enable(strcmp(argv[1], "on") == 0, argc > 2 ? atoi(argv[2]) : 10);
    return 0;
}

static int cmd_fov(int argc, char **argv)
{
    if (argc < 3) {
        printf("usage: fov <hfov_deg> <vfov_deg>\n");
        return 1;
    }
    g_cfg.hfov_deg = (float)atof(argv[1]);
    g_cfg.vfov_deg = (float)atof(argv[2]);
    return 0;
}

static int cmd_bore(int argc, char **argv)
{
    if (argc < 3) {
        printf("usage: bore <dx_px> <dy_px>   (shift optical centre)\n");
        return 1;
    }
    g_cfg.bore_dx_px = (float)atof(argv[1]);
    g_cfg.bore_dy_px = (float)atof(argv[2]);
    return 0;
}

static int cmd_cal(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: cal level | cal nose | cal trim | cal flip | cal reset\n"
               "  level: hold HUD level, looking forward\n"
               "  nose : then pitch the nose up 30-60 deg and hold still\n"
               "  trim : looking at the true horizon, zero pitch/roll\n"
               "  flip : pitch/roll move the wrong way - turn the mount 180 deg about up\n");
        return 1;
    }
    if (strcmp(argv[1], "level") == 0) {
        imu_cal_capture_level();
    } else if (strcmp(argv[1], "nose") == 0) {
        if (imu_cal_capture_nose_up()) {
            printf("mount matrix updated - 'save'\n");
        } else {
            printf("failed: run 'cal level' first and pitch up more\n");
        }
    } else if (strcmp(argv[1], "trim") == 0) {
        imu_trim_level();
    } else if (strcmp(argv[1], "flip") == 0) {
        /* Viewing direction reversed: rotate the mount 180 deg about body up
         * (negate the right and forward rows). Pitch and roll change sign;
         * yaw direction is unchanged. */
        for (int i = 0; i < 6; i++) g_cfg.mount[i] = -g_cfg.mount[i];
        g_cfg.bore_pitch_deg = 0;
        g_cfg.bore_roll_deg = 0;
        printf("mount flipped - 'save' to keep\n");
    } else if (strcmp(argv[1], "reset") == 0) {
        const float mount0[9] = {-1, 0, 0, 0, -1, 0, 0, 0, 1}; /* same as the default */
        memcpy(g_cfg.mount, mount0, sizeof(mount0));
        g_cfg.bore_pitch_deg = 0;
        g_cfg.bore_roll_deg = 0;
    }
    return 0;
}

static int cmd_imu(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    float a[3], g[3], m[3];
    imu_get_raw(a, g, m);
    printf("acc %.3f %.3f %.3f g  gyr %.3f %.3f %.3f rad/s  mag %.3f %.3f %.3f G\n", a[0], a[1], a[2], g[0], g[1],
           g[2], m[0], m[1], m[2]);
    return 0;
}

static int cmd_mode(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: mode <0 normal|1 minimal|2 calib|3 status>\n");
        return 1;
    }
    app_lock();
    g_app.mode = (hud_mode_t)(atoi(argv[1]) % HUD_MODE_COUNT);
    g_cfg.mode = g_app.mode;
    app_unlock();
    return 0;
}

static int cmd_thermal(int argc, char **argv)
{
    if (argc > 1) { /* thermal <0 off|1 full|2 hot> */
        app_lock();
        g_app.thermal_mode = (hud_thermal_mode_t)(atoi(argv[1]) % HUD_THERMAL_COUNT);
        app_unlock();
    } else {
        thermal_cycle_mode();
    }
    uint32_t ok, bad, rs;
    usb_link_stats(&ok, &bad, &rs);
    printf("thermal mode %d (available=%d) source=%s usb_fps=%.1f packets ok=%lu bad=%lu resync=%lu\n",
           (int)g_app.thermal_mode, thermal_available(), thermal_source_name(), (double)thermal_usb_fps(),
           (unsigned long)ok, (unsigned long)bad, (unsigned long)rs);
    return 0;
}

static int cmd_thal(int argc, char **argv)
{
    if (argc < 3) {
        printf("usage: thal <dx_px> <dy_px> [roll_deg]   (align a side-mounted thermal camera)\n"
               "now: dx=%.1f dy=%.1f roll=%.2f\n", g_cfg.th_shift_x, g_cfg.th_shift_y, g_cfg.th_roll_deg);
        return 1;
    }
    g_cfg.th_shift_x = (float)atof(argv[1]);
    g_cfg.th_shift_y = (float)atof(argv[2]);
    g_cfg.th_roll_deg = argc > 3 ? (float)atof(argv[3]) : 0.0f;
    return 0;
}

static int cmd_mirror(int argc, char **argv)
{
    if (argc < 3) {
        printf("usage: mirror <x 0|1> <y 0|1>\n");
        return 1;
    }
    g_cfg.mirror_x = atoi(argv[1]) != 0;
    g_cfg.mirror_y = atoi(argv[2]) != 0;
    lcd_set_mirror(g_cfg.mirror_x, g_cfg.mirror_y);
    return 0;
}

static int cmd_bright(int argc, char **argv)
{
    if (argc < 2) return 1;
    g_cfg.brightness = atoi(argv[1]);
    lcd_set_backlight(g_cfg.brightness);
    return 0;
}

static int cmd_save(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    const esp_err_t err = hud_config_save();
    printf("save: %s\n", esp_err_to_name(err));
    return err == ESP_OK ? 0 : 1;
}

static int cmd_reboot(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    esp_restart();
    return 0;
}

void console_start(void)
{
    /* No REPL: usb_link.c reads the port and runs each line, because the
     * same port also carries binary thermal frames from the WinTAK plugin. */
    esp_console_config_t cc = ESP_CONSOLE_CONFIG_DEFAULT();
    cc.max_cmdline_length = 256;
    cc.max_cmdline_args = 16;
    ESP_ERROR_CHECK(esp_console_init(&cc));
    esp_console_register_help_command();

    const esp_console_cmd_t cmds[] = {
        {.command = "status", .help = "show state and settings", .func = cmd_status},
        {.command = "wifi", .help = "wifi <ssid> [password]", .func = cmd_wifi},
        {.command = "tak", .help = "tak <host> <port> <tcp|tls> [server_cn] | tak off", .func = cmd_tak},
        {.command = "own", .help = "own uid <ATAK uid> | own cs <callsign>: whose position is mine", .func = cmd_own},
        {.command = "pos", .help = "pos <lat> <lon> [hae] | pos off: manual own position", .func = cmd_pos},
        {.command = "hdg", .help = "hdg <deg> [gain]: set/nudge current true heading", .func = cmd_hdg},
        {.command = "trk", .help = "trk <uid> <type> <lat> <lon> <hae> <stale_s> <callsign>: track from USB phone", .func = cmd_trk},
        {.command = "fix", .help = "fix <lat> <lon> [hae]: own position from USB phone", .func = cmd_fix},
        {.command = "fake", .help = "fake on|off: simulated targets", .func = cmd_fake},
        {.command = "mesh", .help = "mesh on|off: ATAK SA multicast", .func = cmd_mesh},
        {.command = "stream", .help = "stream on|off [hz]: @HUD json lines for the web live view", .func = cmd_stream},
        {.command = "fov", .help = "fov <h> <v>: prism field of view, deg", .func = cmd_fov},
        {.command = "bore", .help = "bore <dx> <dy>: optical centre offset, px", .func = cmd_bore},
        {.command = "cal", .help = "cal level|nose|trim|reset: IMU mount calibration", .func = cmd_cal},
        {.command = "imu", .help = "raw sensor values", .func = cmd_imu},
        {.command = "mode", .help = "mode <0-3>: display mode", .func = cmd_mode},
        {.command = "show", .help = "show [full|clean|combat|nav|<mask>|+layer|-layer]: what the HUD draws", .func = cmd_show},
        {.command = "range", .help = "range <show_m> [radar_m]: hide units beyond; radar scale", .func = cmd_range},
        {.command = "thermal", .help = "cycle thermal underlay", .func = cmd_thermal},
        {.command = "thal", .help = "thal <dx> <dy> [roll]: thermal camera alignment", .func = cmd_thal},
        {.command = "mirror", .help = "mirror <x> <y>: LCD mirroring for the prism", .func = cmd_mirror},
        {.command = "bright", .help = "bright <0-100>", .func = cmd_bright},
        {.command = "save", .help = "persist settings to flash", .func = cmd_save},
        {.command = "reboot", .help = "restart", .func = cmd_reboot},
    };
    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        ESP_ERROR_CHECK(esp_console_cmd_register(&cmds[i]));
    }
    usb_link_start();
}
