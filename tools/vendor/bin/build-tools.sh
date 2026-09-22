#!/usr/bin/env bash
# build-tools.sh -- build the 3 libdragon host tools pack_rom.py needs,
# natively, straight from the libdragon submodule's own tools/Makefile.
#
# These aren't simple single-file programs any more (audioconv64 in
# particular pulls in several vendored codecs - opus, libsamplerate, xm,
# vadpcm...), so this shells out to libdragon/tools/Makefile's own build
# rules for them instead of re-implementing the compile line by hand. That
# Makefile always links through $CXX (even C-only tools), so a C++
# cross-compiler has to be available too, not just a C one.
#
#   ./build-tools.sh                                        # native gcc/g++ on PATH
#   CC=x86_64-w64-mingw32-gcc CXX=x86_64-w64-mingw32-g++ ./build-tools.sh   # Windows cross-build
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
TOOLS="$HERE/../../../libdragon/tools"
CC="${CC:-gcc}"
CXX="${CXX:-g++}"

[ -d "$TOOLS" ] || { echo "libdragon not checked out ($TOOLS)"; exit 1; }

# Matches libdragon/tools/Makefile's own EXE detection (findstring mingw in
# $CC -dumpmachine): that's what decides whether it names the binaries it
# just built *.exe or not.
EXE=""
case "$("$CC" -dumpmachine 2>/dev/null)" in
    *mingw*) EXE=".exe" ;;
esac

set -x
# Force a clean rebuild: several tools share object files under common/
# (e.g. common/assetcomp.a), and make has no idea CC/CXX changed since a
# previous build - reusing objects built for a different target here would
# silently mix native and cross-compiled code in the same link (or just
# fail to link at all). Cheap enough to always pay for a handful of tools.
make -C "$TOOLS" clean
make -C "$TOOLS" CC="$CC" CXX="$CXX" n64tool mkdfs audioconv64
set +x

# Release binaries here are always named *.exe (see tools/vendor/bin/README.md
# - this project's own dev platform is Windows, and pack_rom.py's tool
# lookup on Windows expects that extension) regardless of what this build
# actually targeted.
cp "$TOOLS/n64tool$EXE"                 "$HERE/n64tool.exe"
cp "$TOOLS/mkdfs/mkdfs$EXE"             "$HERE/mkdfs.exe"
cp "$TOOLS/audioconv64/audioconv64$EXE" "$HERE/audioconv64.exe"

echo "built: n64tool mkdfs audioconv64  -> $HERE"
