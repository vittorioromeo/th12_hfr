#!/bin/sh
# Native regression harness. Runs under Wine when a Windows host is not available.
# Usage: ./test.sh <game.exe> [more.exe ...]
set -eu
cd "$(dirname "$0")"
mkdir -p build/tests
CC=${CC:-i686-w64-mingw32-gcc}
RUN=${RUN:-wine}
$CC -std=gnu11 -O2 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -static-libgcc \
    tools/test_hfr.c -o build/tests/test_hfr.exe -ld3d9 -lwinmm \
    -Wl,--image-base,0x300000,--disable-dynamicbase,--section-start,.fixture=0x400000
python3 tools/fill_pe_gaps.py build/tests/test_hfr.exe
for f in "$@"; do
    echo "--- $f"
    $RUN ./build/tests/test_hfr.exe "$f" "build/tests/$(basename "$f")"
done
