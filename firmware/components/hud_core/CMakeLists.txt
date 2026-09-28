# Portable HUD logic: no ESP-IDF APIs are used in here, so the same sources are
# unit-tested on a PC (see ../../test_host).
idf_component_register(
    SRCS
        "src/hud_geo.c"
        "src/hud_attitude.c"
        "src/hud_projection.c"
        "src/hud_cot.c"
        "src/hud_targets.c"
        "src/hud_nmea.c"
        "src/hud_takproto.c"
        "src/gfx.c"
        "src/hud_render.c"
    INCLUDE_DIRS "include"
)
