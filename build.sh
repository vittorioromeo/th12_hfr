#!/bin/sh
# Requires an i686 MinGW-w64 cross compiler (gcc and g++). Both game adapters are always
# included. The runtime is C; only the in-game menu and Dear ImGui are C++.
set -eu
cd "$(dirname "$0")"
mkdir -p build/obj
CC=${CC:-i686-w64-mingw32-gcc}
CXX=${CXX:-i686-w64-mingw32-g++}
CFLAGS="-std=gnu11 -O2 -msse2 -mfpmath=sse -Wall -Wextra -Wno-unused-function -Wno-unused-parameter"
CXXFLAGS="-std=gnu++17 -O2 -msse2 -mfpmath=sse -fno-exceptions -fno-rtti -Wall -Wno-unused-parameter"
IMGUI=third_party/imgui

$CC $CFLAGS -c src/hfr.c -o build/obj/hfr.o
for f in "$IMGUI/imgui" "$IMGUI/imgui_draw" "$IMGUI/imgui_tables" "$IMGUI/imgui_widgets" \
         "$IMGUI/backends/imgui_impl_dx9" "$IMGUI/backends/imgui_impl_win32"; do
    $CXX $CXXFLAGS -I"$IMGUI" -c "$f.cpp" -o "build/obj/$(basename "$f").o"
done
$CXX $CXXFLAGS -I"$IMGUI" -c src/ui/menu.cpp -o build/obj/menu.o
$CXX $CXXFLAGS -I"$IMGUI" -c src/ui/menu_dx9.cpp -o build/obj/menu_dx9.o

$CXX -shared -static -static-libgcc -static-libstdc++ -o build/touhou_hfr.dll build/obj/*.o \
     -ld3d9 -lwinmm -lgdi32 -ldwmapi -lpsapi -Wl,--kill-at
$CC $CFLAGS -O2 -s -mwindows -static-libgcc -o build/touhou_hfr.exe src/launcher.c -lbcrypt
cp build/touhou_hfr.dll build/dinput8.dll
cp touhou_hfr.ini build/touhou_hfr.ini
mkdir -p build/shaders && cp shaders/*.hlsl shaders/README.md build/shaders/
