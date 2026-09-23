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
    *mingw*)
        EXE=".exe"
        # Link the Windows binaries fully static. libdragon's tools Makefile
        # only adds -static when it runs *on* Windows (OS=Windows_NT, i.e.
        # MSYS2) - cross-compiling from Linux, as the release workflow does,
        # left audioconv64.exe (C++, threaded) importing MinGW's
        # libwinpthread-1.dll, libstdc++-6.dll and libgcc_s_seh-1.dll, which
        # a normal Windows PC doesn't have: it failed to start with "the code
        # execution cannot proceed because libwinpthread-1.dll was not found"
        # (exit 0xC0000135) as soon as pack_rom.py ran it. Passed through the
        # environment, not the make command line, so the Makefile's own
        # "LDFLAGS += -pthread" still appends to it instead of being
        # overridden.
        export LDFLAGS="${LDFLAGS:-} -static"
        ;;
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

# Refuse to ship a Windows binary that still needs a DLL Windows doesn't
# provide (see the -static comment above): only system DLLs are allowed.
if [ -n "$EXE" ]; then
    OBJDUMP="${CC%gcc}objdump"
    command -v "$OBJDUMP" >/dev/null || OBJDUMP=objdump
    bad=0
    for exe in n64tool mkdfs audioconv64; do
        dlls="$("$OBJDUMP" -p "$HERE/$exe.exe" | sed -n 's/^\s*DLL Name: //p')"
        echo "$exe.exe imports: $(echo $dlls)"
        for dll in $dlls; do
            case "$(echo "$dll" | tr 'A-Z' 'a-z')" in
                kernel32.dll|msvcrt.dll|ntdll.dll|user32.dll|advapi32.dll|shell32.dll|ws2_32.dll|ucrtbase.dll|api-ms-win-*) ;;
                *) echo "error: $exe.exe depends on non-system DLL $dll"; bad=1 ;;
            esac
        done
    done
    [ "$bad" -eq 0 ] || exit 1
fi

echo "built: n64tool mkdfs audioconv64  -> $HERE"
