#!/bin/sh
# x64 regression suite. Runs the Windows harness and launchers under Wine when the host is
# not Windows, so the same checks run wherever the runtime is built.
# Usage: ./test64.sh <th06nc.exe>
set -eu
cd "$(dirname "$0")"
CC=${CC64:-x86_64-w64-mingw32-gcc}
RUN=${RUN64:-wine}
mkdir -p build/tests
$CC -std=gnu11 -O2 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -static-libgcc \
    tools/test_fixed.c build/obj64/buffer.o build/obj64/hook.o build/obj64/trampoline.o \
    build/obj64/hde64.o -lbcrypt -o build/tests/test_fixed.exe
$RUN ./build/tests/test_fixed.exe build/tests/fixed-plan.json
python3 tools/test_fixed_stubs.py build/tests/fixed-plan.json
python3 tools/test_fixed_profile.py "$1"
