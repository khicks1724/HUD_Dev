#!/usr/bin/env sh
# Build and run the hud_core host tests with any C11 compiler (gcc/clang).
#   ./run_tests.sh           unit tests
#   ./run_tests.sh preview   also render HUD preview images (PPM)
set -e
cd "$(dirname "$0")"
CC=${CC:-gcc}
SRC="../components/hud_core/src/hud_geo.c ../components/hud_core/src/hud_attitude.c \
     ../components/hud_core/src/hud_projection.c ../components/hud_core/src/hud_cot.c \
     ../components/hud_core/src/hud_targets.c ../components/hud_core/src/hud_nmea.c \
     ../components/hud_core/src/hud_takproto.c ../components/hud_core/src/gfx.c ../components/hud_core/src/hud_render.c"
FLAGS="-std=c11 -O2 -Wall -Wextra -I../components/hud_core/include"
$CC $FLAGS test_main.c $SRC -lm -o hud_tests
./hud_tests
if [ "$1" = "e2e" ]; then
  # needs tools/sim_server.py running on this PC (TCP 8087)
  $CC $FLAGS cot_dump.c ../components/hud_core/src/hud_cot.c ../components/hud_core/src/hud_geo.c       -lm $( [ "$OS" = "Windows_NT" ] && echo -lws2_32 ) -o cot_dump
  ./cot_dump 127.0.0.1 8087 36.5967 -121.875 3
fi
if [ "$1" = "preview" ]; then
  $CC $FLAGS render_preview.c $SRC -lm -o render_preview
  ./render_preview
fi
