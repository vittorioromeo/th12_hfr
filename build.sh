#!/bin/sh
# Requires an i686 MinGW-w64 cross compiler. Both adapters are always included.
set -eu
cd "$(dirname "$0")"
mkdir -p build
i686-w64-mingw32-gcc -std=gnu11 -O2 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -shared -static-libgcc -o build/touhou_hfr.dll src/hfr.c -ld3d9 -lwinmm -Wl,--kill-at
i686-w64-mingw32-gcc -std=gnu11 -O2 -s -mwindows -static-libgcc -o build/touhou_hfr.exe src/launcher.c
cp build/touhou_hfr.dll build/dinput8.dll
cp touhou_hfr.ini build/touhou_hfr.ini
