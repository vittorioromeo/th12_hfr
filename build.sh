#!/bin/sh
# Builds th12_hfr.dll and th12_hfr.exe into build/ with mingw-w64 (i686-w64-mingw32-gcc).
set -e
cd "$(dirname "$0")"
mkdir -p build
i686-w64-mingw32-gcc -O2 -Wall -Wno-unused-function -shared -static-libgcc -o build/th12_hfr.dll src/hfr.c -ld3d9 -lwinmm -Wl,--kill-at
i686-w64-mingw32-gcc -O2 -s -mwindows -static-libgcc -o build/th12_hfr.exe src/launcher.c
cp build/th12_hfr.dll build/dinput8.dll
ls -la build/
