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
mkdir -p build/tests
$CC -std=gnu11 -O2 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -static-libgcc \
    tools/test_fixed.c build/obj64/buffer.o build/obj64/hook.o build/obj64/trampoline.o \
    build/obj64/hde64.o -lbcrypt -o build/tests/test_fixed.exe
$RUN ./build/tests/test_fixed.exe build/tests/fixed-plan.json
python3 tools/test_fixed_stubs.py build/tests/fixed-plan.json
python3 tools/test_fixed_profile.py "$1"
