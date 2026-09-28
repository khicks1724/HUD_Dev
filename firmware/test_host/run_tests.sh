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
if [ "$1" = "preview" ]; then
  $CC $FLAGS render_preview.c $SRC -lm -o render_preview
  ./render_preview
fi
