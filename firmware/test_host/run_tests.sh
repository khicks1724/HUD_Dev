#!/usr/bin/env sh
# Build and run the hud_core host tests with any C11 compiler (gcc/clang).
set -e
cd "$(dirname "$0")"
CC=${CC:-gcc}
$CC -std=c11 -O2 -Wall -Wextra -I../components/hud_core/include test_main.c ../components/hud_core/src/*.c -lm -o hud_tests
./hud_tests
