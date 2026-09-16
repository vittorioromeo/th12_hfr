#!/bin/sh
# x64 regression suite. Runs the Windows harness and launchers under Wine when the host is
# not Windows, so the same checks run wherever the runtime is built.
# Usage: ./test64.sh <th06nc.exe>
set -eu
cd "$(dirname "$0")"
CC=${CC64:-x86_64-w64-mingw32-gcc}
# The harness and launchers are PE32+, which a 32-bit-only `wine` refuses with "Bad EXE
# format". Prefer an explicit 64-bit wine and its own prefix, so the suite runs rather than
# looking like a failure of the code it is testing.
if [ -z "${RUN64:-}" ] && [ -x /usr/lib/wine/wine64 ] && ! wine --version >/dev/null 2>&1; then
    RUN64=/usr/lib/wine/wine64
fi
if [ -z "${RUN64:-}" ] && [ -x /usr/lib/wine/wine64 ]; then
    RUN64=/usr/lib/wine/wine64
    : "${WINEPREFIX:=$HOME/.wine64}"
    export WINEPREFIX
fi
RUN=${RUN64:-wine}
export RUN64="$RUN"
if [ "$#" -ne 1 ]; then
    echo "--- no New Classic executable given; the signature checks would be skipped" >&2
    echo "    usage: ./test64.sh <th06nc.exe>" >&2
    exit 2
fi
mkdir -p build/tests
$CC -std=gnu11 -O2 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -static-libgcc \
    tools/test_fixed.c build/obj64/buffer.o build/obj64/hook.o build/obj64/trampoline.o \
    build/obj64/hde64.o -lbcrypt -o build/tests/test_fixed.exe
$RUN ./build/tests/test_fixed.exe build/tests/fixed-plan.json
python3 tools/test_fixed_stubs.py build/tests/fixed-plan.json
# The dxgi proxy against the system's real dxgi.dll: every export reachable, the factory
# call answering exactly as the real library does, and inert in a process that is not the game.
$CC -std=gnu11 -O1 -Wall -Wextra -o build/tests/test_dxgi_proxy.exe tools/test_dxgi_proxy.c -lole32
$RUN ./build/tests/test_dxgi_proxy.exe build/dxgi.dll
# The actual proxy basename, with a stand-in runtime: every factory must start it once.
# This checks activation independently of whether Wine can forward same-name DXGI modules.
mkdir -p build/tests/proxy-start
cp build/dxgi.dll build/tests/proxy-start/dxgi.dll
$CC -std=gnu11 -O2 -Wall -Wextra -shared -static-libgcc -DPROXY_TEST_RUNTIME \
    tools/test_dxgi_proxy_start.c -o build/tests/proxy-start/touhou_hfr64.dll
$CC -std=gnu11 -O2 -Wall -Wextra -static-libgcc tools/test_dxgi_proxy_start.c \
    -o build/tests/proxy-start/test_proxy_start.exe
for factory in CreateDXGIFactory CreateDXGIFactory1 CreateDXGIFactory2; do
    WINEDLLOVERRIDES="${WINEDLLOVERRIDES:+$WINEDLLOVERRIDES;}dxgi=n,b" \
        $RUN ./build/tests/proxy-start/test_proxy_start.exe "$factory"
    WINEDLLOVERRIDES="${WINEDLLOVERRIDES:+$WINEDLLOVERRIDES;}dxgi=n,b" \
        $RUN ./build/tests/proxy-start/test_proxy_start.exe "$factory" --system-factory
done
WINEDLLOVERRIDES="${WINEDLLOVERRIDES:+$WINEDLLOVERRIDES;}dxgi=n,b" \
    $RUN ./build/tests/proxy-start/test_proxy_start.exe CreateDXGIFactory2 --unload
python3 tools/test_fixed_profile.py "$1"
