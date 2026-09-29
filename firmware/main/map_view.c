#include "map_view.h"

#include <string.h>

#include "app_state.h"
#include "esp32s3/rom/tjpgd.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "gfx.h"

static const char *TAG = "map";

#define MAX_W 320
#define MAX_H 320

static uint16_t *s_pic[2];
static volatile int s_front = -1;
static int s_w[2], s_h[2];
static volatile int64_t s_mono_ms;
static float s_fps;
static int s_count;
static int64_t s_fps_t0;
static uint8_t *s_work;

typedef struct {
    const uint8_t *src;
    uint32_t len, pos;
    uint16_t *dst;
    int w;
} io_t;

static UINT in_func(JDEC *jd, BYTE *buf, UINT n)
{
    io_t *io = (io_t *)jd->device;
    if (io->pos + n > io->len) n = io->len - io->pos;
    if (buf) memcpy(buf, io->src + io->pos, n);
    io->pos += n;
    return n;
}

static UINT out_func(JDEC *jd, void *bitmap, JRECT *r)
{
    io_t *io = (io_t *)jd->device;
    const uint8_t *rgb = (const uint8_t *)bitmap; /* ROM decoder outputs RGB888 */
    for (int y = r->top; y <= r->bottom; y++) {
        uint16_t *row = io->dst + (size_t)y * io->w;
        for (int x = r->left; x <= r->right; x++, rgb += 3) row[x] = GFX_RGB(rgb[0], rgb[1], rgb[2]);
    }
    return 1;
}

void map_view_init(void)
{
    for (int i = 0; i < 2; i++) s_pic[i] = heap_caps_malloc(MAX_W * MAX_H * 2, MALLOC_CAP_SPIRAM);
    s_work = heap_caps_malloc(4096, MALLOC_CAP_INTERNAL);
    if (!s_pic[0] || !s_pic[1] || !s_work) ESP_LOGE(TAG, "no memory for the map picture");
}

bool map_view_publish_jpeg(const uint8_t *jpg, uint32_t len)
{
    if (!s_work || !s_pic[0]) return false;
    const int back = s_front == 0 ? 1 : 0;
    io_t io = {jpg, len, 0, s_pic[back], 0};
    JDEC jd;
    JRESULT rc = jd_prepare(&jd, in_func, s_work, 4096, &io);
    if (rc != JDR_OK) {
        ESP_LOGW(TAG, "jpeg header: %d", rc);
        return false;
    }
    if (jd.width > MAX_W || jd.height > MAX_H) {
        ESP_LOGW(TAG, "map picture %ux%u too big", jd.width, jd.height);
        return false;
    }
    io.w = (int)jd.width;
    rc = jd_decomp(&jd, out_func, 0);
    if (rc != JDR_OK) {
        ESP_LOGW(TAG, "jpeg decode: %d", rc);
        return false;
    }
    s_w[back] = (int)jd.width;
    s_h[back] = (int)jd.height;
    s_front = back;
    const int64_t now = app_mono_ms();
    s_mono_ms = now;
    if (++s_count >= 5) {
        s_fps = s_count * 1000.0f / (float)(now - s_fps_t0 + 1);
        s_count = 0;
        s_fps_t0 = now;
    }
    return true;
}

const uint16_t *map_view_latest(int *w, int *h)
{
    const int f = s_front;
    if (f < 0 || app_mono_ms() - s_mono_ms > 5000) return NULL;
    *w = s_w[f];
    *h = s_h[f];
    return s_pic[f];
}

float map_view_fps(void)
{
    return s_front >= 0 && app_mono_ms() - s_mono_ms < 5000 ? s_fps : 0.0f;
}
