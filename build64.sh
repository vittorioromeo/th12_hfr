#!/bin/sh
# Experimental x64 fixed-clock runtime. Needs an x86_64 MinGW-w64 toolchain; the x86
# games are built by build.sh and are unaffected by anything here.
set -eu
cd "$(dirname "$0")"
CC=${CC64:-x86_64-w64-mingw32-gcc}
CXX=${CXX64:-x86_64-w64-mingw32-g++}
mkdir -p build/obj64
CFLAGS="-std=gnu11 -O2 -g -Wall -Wextra -Wno-unused-function -Wno-unused-parameter"
for source in src/hfr64.c third_party/minhook/src/buffer.c third_party/minhook/src/hook.c \
              third_party/minhook/src/trampoline.c third_party/minhook/src/hde/hde64.c; do
    $CC $CFLAGS -c "$source" -o "build/obj64/$(basename "${source%.c}").o"
done
for source in third_party/imgui/imgui.cpp third_party/imgui/imgui_draw.cpp \
              third_party/imgui/imgui_tables.cpp third_party/imgui/imgui_widgets.cpp \
              third_party/imgui/backends/imgui_impl_dx11.cpp third_party/imgui/backends/imgui_impl_win32.cpp \
              src/ui/menu.cpp src/ui/menu_dx11.cpp src/ui/overlay_dx11.cpp; do
    $CXX -std=gnu++17 -O2 -g -fno-exceptions -fno-rtti -Wall -Wno-unused-parameter \
        -Ithird_party/imgui -c "$source" -o "build/obj64/$(basename "${source%.cpp}").o"
done
$CXX -shared -static -static-libgcc -static-libstdc++ -o build/touhou_hfr64.dll build/obj64/*.o \
    -ldxgi -ld3d11 -ld3dcompiler -ldxguid -lbcrypt -lgdi32 -ldwmapi
$CC $CFLAGS -static-libgcc src/launcher64.c -o build/touhou_hfr64.exe -lbcrypt
# The dxgi.dll proxy: the game loads it by name, so this is how the patch installs itself
# when Steam is the launcher. Deliberately built on its own -- it must not pull in the
# runtime, the menu or the CRT beyond what a forwarding stub needs.
$CC $CFLAGS -shared -s -static-libgcc src/proxy_dxgi.c -o build/dxgi.dll
echo "Built the experimental x64 fixed-clock runtime, its dxgi proxy and the launcher helper."
