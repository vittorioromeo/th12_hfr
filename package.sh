#!/bin/sh
set -eu
V=${1:-0.5.2-test}
# HFR64=1 also builds and ships the experimental x64 New Classic runtime.
case "$V" in *[!A-Za-z0-9.-]*|'') echo 'Invalid version'; exit 1;; esac
cd "$(dirname "$0")"
./build.sh
if [ "${HFR64:-0}" = 1 ]; then ./build64.sh; fi
P=$(mktemp -d build/package.XXXXXX)
mkdir -p "$P/touhou_hfr_v$V/source" releases
cp build/dinput8.dll build/touhou_hfr.dll build/touhou_hfr.exe touhou_hfr.ini README.md ARCHITECTURE.md DEVNOTES.md DEVNOTES_RUNTIME.md TH10_DEVNOTES.md TH11_DEVNOTES.md TH13_DEVNOTES.md ADDING_A_GAME.md TH11_README.md TH12_README.md RESOLUTION.md install.ps1 "$P/touhou_hfr_v$V/"
mkdir -p "$P/touhou_hfr_v$V/shaders"
cp shaders/*.hlsl shaders/README.md "$P/touhou_hfr_v$V/shaders/"
cp -R src tools shaders third_party "$P/touhou_hfr_v$V/source/"
cp TH06NC_DEVNOTES.md TH06NC_VS_TH10_13.md "$P/touhou_hfr_v$V/"
cp build64.ps1 test64.ps1 build64.sh test64.sh TH06NC_DEVNOTES.md TH06NC_VS_TH10_13.md "$P/touhou_hfr_v$V/source/"
if [ "${HFR64:-0}" = 1 ]; then cp build/touhou_hfr64.exe build/touhou_hfr64.dll build/dxgi.dll "$P/touhou_hfr_v$V/"; fi
cp build.sh build.ps1 package.sh package.ps1 test.sh test.ps1 test_th11.ps1 install.ps1 touhou_hfr.ini README.md ARCHITECTURE.md DEVNOTES.md DEVNOTES_RUNTIME.md TH10_DEVNOTES.md TH11_DEVNOTES.md TH13_DEVNOTES.md ADDING_A_GAME.md TH11_README.md TH12_README.md RESOLUTION.md "$P/touhou_hfr_v$V/source/"
# Exclude local Python caches; never include build/test images or game files.
(cd "$P" && zip -qr "../../releases/touhou_hfr_v$V.zip" "touhou_hfr_v$V" -x '*/__pycache__/*' '*.pyc')
echo "releases/touhou_hfr_v$V.zip"
