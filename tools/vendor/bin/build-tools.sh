#!/usr/bin/env bash
# build-tools.sh -- compile the 3 libdragon host tools pack_rom.py needs,
# natively, straight from the libdragon submodule.
#
# They are single-translation-unit C programs (audioconv64 is a unity build
# of its vendored opus / libsamplerate), so this sidesteps libdragon/tools/
# Makefile and its MSYS2-environment checks.
#
#   ./build-tools.sh            # uses `gcc` from PATH
#   CC=/path/to/gcc ./build-tools.sh
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
TOOLS="$HERE/../../../libdragon/tools"
CC="${CC:-gcc}"
CF="-O2 -std=gnu11 -I$TOOLS/../include -Wno-unused-result -Wno-error -static"

[ -d "$TOOLS" ] || { echo "libdragon not checked out ($TOOLS)"; exit 1; }

set -x
"$CC" $CF -o "$HERE/n64tool.exe"        "$TOOLS/n64tool.c"
"$CC" $CF -o "$HERE/mkdfs.exe"          "$TOOLS/mkdfs/mkdfs.c"
"$CC" $CF -I"$TOOLS/audioconv64" -o "$HERE/audioconv64.exe" "$TOOLS/audioconv64/audioconv64.c"
set +x

echo "built: n64tool mkdfs audioconv64  -> $HERE"
