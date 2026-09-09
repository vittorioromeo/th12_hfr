#!/bin/sh
# package.sh <version> : builds and creates releases/th12_hfr_v<version>.zip
set -e
V=$1; [ -n "$V" ] || { echo "usage: package.sh <version>"; exit 1; }
cd "$(dirname "$0")"; ./build.sh >/dev/null
P=build/pkg; rm -rf $P; mkdir -p $P/th12_hfr_v$V/source
cp build/dinput8.dll build/th12_hfr.dll build/th12_hfr.exe th12_hfr.ini README.md $P/th12_hfr_v$V/
cp src/hfr.c src/launcher.c build.sh package.sh $P/th12_hfr_v$V/source/
(cd $P && zip -qr ../../releases/th12_hfr_v$V.zip th12_hfr_v$V)
ls -la releases/th12_hfr_v$V.zip
