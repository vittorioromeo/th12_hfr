#!/bin/sh
# Builds a release archive in releases/. HFR64=1 also builds and ships the
# experimental x64 New Classic runtime.
set -eu
V=${1:-0.5.3-test}
case "$V" in *[!A-Za-z0-9.-]*|'') echo 'Invalid version'; exit 1;; esac
cd "$(dirname "$0")"
./build.sh
if [ "${HFR64:-0}" = 1 ]; then ./build64.sh; fi
P=$(mktemp -d build/package.XXXXXX)
trap 'rm -rf "$P"' EXIT INT TERM
R="$P/touhou_hfr_v$V"
mkdir -p "$R/source" "$R/shaders" releases

# What a player unzips: the runtime, its configuration template, the installer and the docs.
cp build/dinput8.dll build/touhou_hfr.dll build/touhou_hfr.exe touhou_hfr.ini install.ps1 "$R/"
if [ "${HFR64:-0}" = 1 ]; then cp build/touhou_hfr64.exe build/touhou_hfr64.dll build/dxgi.dll "$R/"; fi
cp shaders/*.hlsl shaders/README.md "$R/shaders/"
# Docs are copied wholesale rather than enumerated: a new document ships without
# anyone remembering to add it here, and to package.ps1, and to both source lists.
cp README.md ARCHITECTURE.md ADDING_A_GAME.md "$R/"
cp -R docs "$R/"

# ... and the complete source beside it, so a release can be rebuilt from itself.
cp -R src tools shaders third_party docs "$R/source/"
cp build.sh build.ps1 build64.sh build64.ps1 test.sh test.ps1 test64.sh test64.ps1 \
   package.sh package.ps1 install.ps1 touhou_hfr.ini \
   README.md ARCHITECTURE.md ADDING_A_GAME.md "$R/source/"

# Exclude local Python caches; never include build/test images or game files.
(cd "$P" && zip -qr "../../releases/touhou_hfr_v$V.zip" "touhou_hfr_v$V" -x '*/__pycache__/*' '*.pyc')
echo "releases/touhou_hfr_v$V.zip"
