#include "hud_config.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

static const char *TAG = "cfg";

/* Kconfig bools are either defined as 1 or not defined at all. */
#ifdef CONFIG_HUD_FAKE_TARGETS
#define K_FAKE true
#else
#define K_FAKE false
#endif
#ifdef CONFIG_HUD_MESH_SA
#define K_MESH true
#else
#define K_MESH false
#endif
#ifdef CONFIG_HUD_MIRROR_X
#define K_MIRROR_X true
#else
#define K_MIRROR_X false
#endif
#ifdef CONFIG_HUD_MIRROR_Y
#define K_MIRROR_Y true
#else
#define K_MIRROR_Y false
#endif
static const char *PART = "hudcfg";
static const char *NS = "hud";

hud_config_t g_cfg;

static void copy(char *dst, size_t cap, const char *src)
{
    strncpy(dst, src ? src : "", cap - 1);
    dst[cap - 1] = '\0';
}

void hud_config_defaults(hud_config_t *c)
{
    memset(c, 0, sizeof(*c));
    copy(c->wifi_ssid, sizeof(c->wifi_ssid), CONFIG_HUD_WIFI_SSID);
    copy(c->wifi_pass, sizeof(c->wifi_pass), CONFIG_HUD_WIFI_PASS);
#if CONFIG_HUD_TAK_PROTO_TLS
    c->tak_proto = TAK_TLS;
#elif CONFIG_HUD_TAK_PROTO_TCP
    c->tak_proto = TAK_TCP;
#else
    c->tak_proto = TAK_OFF;
#endif
    copy(c->tak_host, sizeof(c->tak_host), CONFIG_HUD_TAK_HOST);
    c->tak_port = CONFIG_HUD_TAK_PORT;
    copy(c->own_uid, sizeof(c->own_uid), CONFIG_HUD_OWN_UID);
    copy(c->own_callsign, sizeof(c->own_callsign), CONFIG_HUD_OWN_CALLSIGN);
    copy(c->hud_uid, sizeof(c->hud_uid), CONFIG_HUD_UID);
    copy(c->hud_callsign, sizeof(c->hud_callsign), CONFIG_HUD_UID);
    c->fake_targets = K_FAKE;
    c->mesh_sa = K_MESH;
    c->udp_port = CONFIG_HUD_UDP_PORT;
    c->man_lat = atof(CONFIG_HUD_FAKE_LAT);
    c->man_lon = atof(CONFIG_HUD_FAKE_LON);
    c->man_hae = 0;
    c->hfov_deg = CONFIG_HUD_HFOV_DEG / 10.0f;
    c->vfov_deg = CONFIG_HUD_VFOV_DEG / 10.0f;
    /* Default: viewer looks along sensor -Y (the prism-case orientation that
     * goes with the vertically mirrored display). 'cal flip' toggles it. */
    const float mount0[9] = {-1, 0, 0, 0, -1, 0, 0, 0, 1};
    memcpy(c->mount, mount0, sizeof(mount0));
    c->max_range_m = 5000.0f;
    c->radar_range_m = 2000.0f;
    c->layers = 0x1FF; /* HUD_L_DEFAULT */
    c->brightness = 80;
    c->mirror_x = K_MIRROR_X;
    c->mirror_y = K_MIRROR_Y;
}

/* Each field is a separate NVS key so provision.py and the console can set
 * them individually. Floats/doubles are stored as blobs. */
#define STR(key, field) nvs_get_str_s(h, key, c->field, sizeof(c->field))
#define BLOB(key, field) nvs_get_blob_s(h, key, &c->field, sizeof(c->field))

static void nvs_get_str_s(nvs_handle_t h, const char *k, char *dst, size_t cap)
{
    size_t len = cap;
    if (nvs_get_str(h, k, dst, &len) != ESP_OK) {
        /* keep default */
    }
}

static void nvs_get_blob_s(nvs_handle_t h, const char *k, void *dst, size_t size)
{
    size_t len = size;
    uint8_t tmp[64];
    if (size > sizeof(tmp)) return;
    if (nvs_get_blob(h, k, tmp, &len) == ESP_OK && len == size) {
        memcpy(dst, tmp, size);
    }
}

static void get_u8_bool(nvs_handle_t h, const char *k, bool *dst)
{
    uint8_t v;
    if (nvs_get_u8(h, k, &v) == ESP_OK) *dst = v != 0;
}

esp_err_t hud_config_load(void)
{
    hud_config_t *c = &g_cfg;
    hud_config_defaults(c);

    esp_err_t err = nvs_flash_init_partition(PART);
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "erasing hudcfg (%s)", esp_err_to_name(err));
        nvs_flash_erase_partition(PART);
        err = nvs_flash_init_partition(PART);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "hudcfg init failed: %s (using defaults)", esp_err_to_name(err));
        return err;
    }
    nvs_handle_t h;
    if (nvs_open_from_partition(PART, NS, NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGI(TAG, "no stored config, using Kconfig defaults");
        return ESP_OK;
    }
    STR("wifi_ssid", wifi_ssid);
    STR("wifi_pass", wifi_pass);
    STR("tak_host", tak_host);
    STR("tak_cn", tak_server_cn);
    STR("own_uid", own_uid);
    STR("own_cs", own_callsign);
    STR("hud_uid", hud_uid);
    STR("hud_cs", hud_callsign);
    uint8_t u8;
    uint16_t u16;
    int32_t i32;
    if (nvs_get_u8(h, "tak_proto", &u8) == ESP_OK) c->tak_proto = (tak_proto_t)u8;
    if (nvs_get_u16(h, "tak_port", &u16) == ESP_OK) c->tak_port = u16;
    if (nvs_get_u16(h, "udp_port", &u16) == ESP_OK) c->udp_port = u16;
    if (nvs_get_i32(h, "bright", &i32) == ESP_OK) c->brightness = (int)i32;
    if (nvs_get_i32(h, "mode", &i32) == ESP_OK) c->mode = (int)i32;
    if (nvs_get_i32(h, "layers", &i32) == ESP_OK) c->layers = (uint32_t)i32;
    get_u8_bool(h, "send_sa", &c->send_sa);
    get_u8_bool(h, "fake", &c->fake_targets);
    get_u8_bool(h, "mesh", &c->mesh_sa);
    get_u8_bool(h, "man_ok", &c->man_valid);
    get_u8_bool(h, "mirror_x", &c->mirror_x);
    get_u8_bool(h, "mirror_y", &c->mirror_y);
    BLOB("man_lat", man_lat);
    BLOB("man_lon", man_lon);
    BLOB("man_hae", man_hae);
    BLOB("hfov", hfov_deg);
    BLOB("vfov", vfov_deg);
    BLOB("bore_dx", bore_dx_px);
    BLOB("bore_dy", bore_dy_px);
    BLOB("bore_p", bore_pitch_deg);
    BLOB("bore_r", bore_roll_deg);
    BLOB("mount", mount);
    BLOB("mag_off", mag_offset);
    BLOB("th_dx", th_shift_x);
    BLOB("th_dy", th_shift_y);
    BLOB("th_roll", th_roll_deg);
    BLOB("max_rng", max_range_m);
    BLOB("radar_rng", radar_range_m);
    nvs_close(h);
    ESP_LOGI(TAG, "loaded: ssid='%s' tak=%d %s:%u own_uid='%s'", c->wifi_ssid, c->tak_proto, c->tak_host,
             c->tak_port, c->own_uid);
    return ESP_OK;
}

esp_err_t hud_config_save(void)
{
    const hud_config_t *c = &g_cfg;
    nvs_handle_t h;
    esp_err_t err = nvs_open_from_partition(PART, NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    nvs_set_str(h, "wifi_ssid", c->wifi_ssid);
    nvs_set_str(h, "wifi_pass", c->wifi_pass);
    nvs_set_str(h, "tak_host", c->tak_host);
    nvs_set_str(h, "tak_cn", c->tak_server_cn);
    nvs_set_str(h, "own_uid", c->own_uid);
    nvs_set_str(h, "own_cs", c->own_callsign);
    nvs_set_str(h, "hud_uid", c->hud_uid);
    nvs_set_str(h, "hud_cs", c->hud_callsign);
    nvs_set_u8(h, "tak_proto", (uint8_t)c->tak_proto);
    nvs_set_u16(h, "tak_port", c->tak_port);
    nvs_set_u16(h, "udp_port", c->udp_port);
    nvs_set_i32(h, "bright", c->brightness);
    nvs_set_i32(h, "mode", c->mode);
    nvs_set_i32(h, "layers", (int32_t)c->layers);
    nvs_set_u8(h, "send_sa", c->send_sa);
    nvs_set_u8(h, "fake", c->fake_targets);
    nvs_set_u8(h, "mesh", c->mesh_sa);
    nvs_set_u8(h, "man_ok", c->man_valid);
    nvs_set_u8(h, "mirror_x", c->mirror_x);
    nvs_set_u8(h, "mirror_y", c->mirror_y);
    nvs_set_blob(h, "man_lat", &c->man_lat, sizeof(c->man_lat));
    nvs_set_blob(h, "man_lon", &c->man_lon, sizeof(c->man_lon));
    nvs_set_blob(h, "man_hae", &c->man_hae, sizeof(c->man_hae));
    nvs_set_blob(h, "hfov", &c->hfov_deg, sizeof(c->hfov_deg));
    nvs_set_blob(h, "vfov", &c->vfov_deg, sizeof(c->vfov_deg));
    nvs_set_blob(h, "bore_dx", &c->bore_dx_px, sizeof(float));
    nvs_set_blob(h, "bore_dy", &c->bore_dy_px, sizeof(float));
    nvs_set_blob(h, "bore_p", &c->bore_pitch_deg, sizeof(float));
    nvs_set_blob(h, "bore_r", &c->bore_roll_deg, sizeof(float));
    nvs_set_blob(h, "mount", c->mount, sizeof(c->mount));
    nvs_set_blob(h, "mag_off", c->mag_offset, sizeof(c->mag_offset));
    nvs_set_blob(h, "th_dx", &c->th_shift_x, sizeof(float));
    nvs_set_blob(h, "th_dy", &c->th_shift_y, sizeof(float));
    nvs_set_blob(h, "th_roll", &c->th_roll_deg, sizeof(float));
    nvs_set_blob(h, "max_rng", &c->max_range_m, sizeof(float));
    nvs_set_blob(h, "radar_rng", &c->radar_range_m, sizeof(float));
    err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t hud_config_get_pem(const char *name, char **out, size_t *len)
{
    nvs_handle_t h;
    *out = NULL;
    esp_err_t err = nvs_open_from_partition(PART, NS, NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    size_t n = 0;
    /* provision.py stores PEMs as NVS "file" entries (binary blobs). */
    err = nvs_get_blob(h, name, NULL, &n);
    if (err == ESP_OK && n > 0) {
        char *buf = malloc(n + 1);
        if (!buf) {
            nvs_close(h);
            return ESP_ERR_NO_MEM;
        }
        err = nvs_get_blob(h, name, buf, &n);
        buf[n] = '\0';
        *out = buf;
        *len = n + 1; /* mbedTLS wants the NUL counted for PEM */
    }
    nvs_close(h);
    return err;
}
