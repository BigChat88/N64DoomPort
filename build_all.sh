#!/usr/bin/env bash
# Builds a .z64 for every IWAD dropped into input/ (see input/README.md),
# instead of running the render/build steps by hand for each one (see
# README.md's "How to Build"). For each *.WAD found there, this renders its
# music if it hasn't been rendered yet (tools/render_music.py, skipped if
# music_wav/<PREFIX>/ already has it), then builds it via the top-level
# Makefile inside `libdragon exec`, and keeps going on to the next WAD even
# if one fails - so dropping in a new/unfamiliar WAD to see whether it
# builds doesn't block the ones you already know work.
#
# Usage (from the repo root, same prerequisites as the manual steps in
# README.md - Docker/libdragon, fluidsynth+ffmpeg, python3, all on PATH):
#   ./build_all.sh
#   ./build_all.sh --force-music     # re-render music even if already present

set -u
cd "$(dirname "$0")"

# Needed for `libdragon exec` under Git Bash on Windows - without it, Git
# Bash rewrites the container's own path-like arguments as if they were host
# paths. Harmless to set unconditionally on other platforms.
export MSYS_NO_PATHCONV=1

# The only filenames IdentifyVersion() (src/d_main.c) recognizes at runtime -
# anything else still gets attempted below (that's the point: dropping in an
# unfamiliar WAD to see whether it's capable of building at all), but the
# engine falls back to gamemode "indetermined" for it, which isn't a
# supported or tested configuration - expect menus/episode logic that branch
# on gamemode to be wrong, not just cosmetic differences.
KNOWN_PREFIXES=" DOOM1 DOOM DOOMU DOOM2 PLUTONIA TNT CHEX CHEX2 "

force_music=0
if [ "${1:-}" = "--force-music" ]; then
    force_music=1
fi

shopt -s nocaseglob nullglob
wads=(input/*.wad)
shopt -u nocaseglob nullglob

if [ ${#wads[@]} -eq 0 ]; then
    echo "no .WAD files found in input/ - see input/README.md"
    exit 1
fi

built=()
failed=()

for wad in "${wads[@]}"; do
    base="$(basename "$wad")"
    prefix="${base%.*}"
    prefix="$(printf '%s' "$prefix" | tr '[:lower:]' '[:upper:]')"

    # The filename guess above breaks for a real IWAD that isn't named
    # exactly what the engine expects (see identify_iwad.py's docstring -
    # The Ultimate Doom in particular is at least as often UDOOM.WAD in the
    # wild as it is DOOMU.WAD). For the three episodic IWADs, the actual
    # lump contents say unambiguously which one it really is, so prefer
    # that over the filename when they disagree.
    detected="$(python3 tools/identify_iwad.py "$wad" 2>/dev/null || echo UNKNOWN)"
    if [[ "$detected" =~ ^(DOOM1|DOOM|DOOMU|CHEX|CHEX2)$ ]] && [ "$detected" != "$prefix" ]; then
        echo "note: $base's contents say it's $detected, not $prefix (filename) - using $detected."
        prefix="$detected"
    fi

    echo
    echo "== $base (IWAD_PREFIX=$prefix) =="

    if [[ "$KNOWN_PREFIXES" != *" $prefix "* ]]; then
        echo "warning: '$prefix' isn't one of the IWADs src/d_main.c's IdentifyVersion() recognizes ($KNOWN_PREFIXES)."
        echo "         it'll still try to build, but the game will run in gamemode 'indetermined' at runtime - untested."
    fi

    # The Makefile expects the WAD to be literally named
    # $(IWAD_DIRECTORY)/$(IWAD_PREFIX).WAD (see its "cp" step) - stage a
    # correctly-named link next to it whenever the real filename doesn't
    # already match (the UDOOM.WAD/DOOMU case above, or any other
    # mismatched name), rather than reworking IWAD_DIRECTORY's path
    # resolution across the host/container boundary just for this.
    canonical="input/$prefix.WAD"
    if [ "$base" != "$(basename "$canonical")" ] && [ ! -e "$canonical" ]; then
        ln -s "$base" "$canonical" 2>/dev/null || cp "$wad" "$canonical"
        echo "staged $canonical -> $base"
    fi

    music_dir="music_wav/$prefix"
    have_music=0
    if [ "$force_music" -eq 0 ] && [ -d "$music_dir" ] && [ -n "$(ls -A "$music_dir"/*.wav 2>/dev/null)" ]; then
        have_music=1
    fi

    if [ "$have_music" -eq 1 ]; then
        echo "music already rendered in $music_dir, skipping (use --force-music to redo it)"
    else
        echo "rendering music..."
        # Chex Quest 2 only carries the tracks it replaces: render from it
        # merged onto CHEX.WAD, the same WAD the ROM build packs (see
        # tools/merge_wad.py and src/Makefile).
        render_wad="$wad"
        if [ "$prefix" = "CHEX2" ]; then
            shopt -s nocaseglob nullglob
            chex_base=(input/chex.wad)
            shopt -u nocaseglob nullglob
            if [ ${#chex_base[@]} -eq 0 ]; then
                echo "FAILED: Chex Quest 2 needs Chex Quest's CHEX.WAD in input/ too"
                failed+=("$base (no CHEX.WAD)")
                continue
            fi
            # Relative, inside the repo: under Git Bash, mktemp's /tmp/...
            # paths don't resolve for a native Windows python3.
            mkdir -p music_wav
            render_wad="music_wav/.CHEX2-merged.WAD"
            python3 tools/merge_wad.py "${chex_base[0]}" "$wad" -o "$render_wad" || { failed+=("$base (merge)"); continue; }
        fi
        render_ok=1
        python3 tools/render_music.py "$render_wad" -o "$music_dir" || render_ok=0
        [ "$render_wad" != "$wad" ] && rm -f "$render_wad"
        if [ "$render_ok" -eq 0 ]; then
            echo "FAILED: music render for $base"
            failed+=("$base (music render)")
            continue
        fi
    fi

    echo "building ROM..."
    if ! libdragon exec env IWAD_PREFIX="$prefix" make; then
        echo "FAILED: build for $base"
        failed+=("$base (build)")
        continue
    fi

    built+=("output/$prefix.z64")
done

echo
echo "==================== summary ===================="
for z in "${built[@]}"; do
    echo "  built:  $z"
done
for f in "${failed[@]}"; do
    echo "  FAILED: $f"
done

[ ${#failed[@]} -eq 0 ]
