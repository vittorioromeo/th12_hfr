#!/bin/sh
# Native regression harness. Runs under Wine when a Windows host is not available.
# Usage: ./test.sh <game.exe> [more.exe ...]
set -eu
cd "$(dirname "$0")"
mkdir -p build/tests
CC=${CC:-i686-w64-mingw32-gcc}
RUN=${RUN:-wine}
$CC -std=gnu11 -O2 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -static-libgcc \
    tools/test_hfr.c -o build/tests/test_hfr.exe -ld3d9 -lwinmm -lgdi32 -ldwmapi -lpsapi \
    -Wl,--image-base,0x300000,--disable-dynamicbase,--section-start,.fixture=0x400000
python3 tools/fill_pe_gaps.py build/tests/test_hfr.exe
if [ "$#" -eq 0 ]; then
    echo "--- no game executables given; the native harness is the bulk of this suite" >&2
    echo "    usage: ./test.sh <game.exe> [more.exe ...]" >&2
    exit 2
fi
for f in "$@"; do
    echo "--- $f"
    $RUN ./build/tests/test_hfr.exe "$f" "build/tests/$(basename "$f")"
done

echo "--- documentation links"
python3 tools/check_docs.py

echo "--- generated files"
python3 tools/gen_signature_json.py --check

# What the menu's sections offer, with no device and no window: a control that is silently
# not drawn is invisible to the device test below, which only proves the overlay renders.
if [ -d third_party/imgui ]; then
    echo "--- menu contents"
    ${CXX:-i686-w64-mingw32-g++} -std=gnu++11 -O1 -static-libgcc -static-libstdc++ -Ithird_party/imgui \
        -o build/tests/test_menu_logic.exe tools/test_menu_logic.cpp build/obj/menu.o \
        build/obj/imgui.o build/obj/imgui_draw.o build/obj/imgui_tables.o build/obj/imgui_widgets.o \
        build/obj/imgui_impl_win32.o -lgdi32 -ldwmapi
    $RUN ./build/tests/test_menu_logic.exe
fi

# The overlay on a real Direct3D 9Ex device, with the same swap chain, render target and
# state block the patch uses. Needs a display; skipped when there is none.
if [ -n "${DISPLAY:-}" ] && [ -d third_party/imgui ]; then
    echo "--- in-game menu"
    $CC -std=gnu11 -O1 -c tools/test_menu.c -o build/tests/test_menu.o -Ithird_party/imgui
    ${CXX:-i686-w64-mingw32-g++} -O1 -static-libgcc -static-libstdc++ -o build/tests/test_menu.exe \
        build/tests/test_menu.o build/obj/menu.o build/obj/menu_dx9.o build/obj/imgui.o build/obj/imgui_draw.o \
        build/obj/imgui_tables.o build/obj/imgui_widgets.o build/obj/imgui_impl_dx9.o \
        build/obj/imgui_impl_win32.o -ld3d9 -lgdi32 -ldwmapi
    $RUN ./build/tests/test_menu.exe
fi
