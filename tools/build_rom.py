#!/usr/bin/env python3
"""
build_rom.py -- one-command N64 Doom ROM build for end users.

Finds your IWAD in input/, renders its music if that hasn't been done yet
(needs fluidsynth + ffmpeg on PATH), and packs everything into a ROM with
tools/pack_rom.py - no Docker, no N64 toolchain, nothing but Python and
those two audio tools. See build.cmd for the one-click Windows wrapper
around this.

    python tools/build_rom.py
    python tools/build_rom.py --force-music

If input/ has more than one IWAD, pass --wad to pick which one:
    python tools/build_rom.py --wad input/DOOM2.WAD
"""
from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent

KNOWN_PREFIXES = ("DOOM1", "DOOM", "DOOMU", "DOOM2", "PLUTONIA", "TNT")


def log(msg: str) -> None:
    print(msg, flush=True)


def die(msg: str):
    print(f"error: {msg}", file=sys.stderr)
    sys.exit(1)


def find_wad(explicit: Path | None) -> Path:
    if explicit:
        if not explicit.is_file():
            die(f"{explicit}: no such file")
        return explicit

    indir = ROOT / "input"
    cands = sorted(indir.glob("*.WAD")) + sorted(indir.glob("*.wad"))
    # de-dupe (case-insensitive filesystems return the same file for both globs)
    seen = set()
    unique = []
    for c in cands:
        key = c.resolve()
        if key not in seen:
            seen.add(key)
            unique.append(c)
    cands = unique

    if not cands:
        die(f"no IWAD found in {indir}\n  see input/README.md - drop your IWAD there and try again")
    if len(cands) > 1:
        names = ", ".join(c.name for c in cands)
        die(f"more than one WAD in {indir} ({names})\n  pass --wad to pick one, e.g. --wad input/DOOM2.WAD")
    return cands[0]


def detect_prefix(wad: Path) -> str:
    prefix = wad.stem.upper()

    # The engine only recognizes the exact filenames below (see
    # src/d_main.c's IdentifyVersion) - a real IWAD not named exactly that
    # (e.g. The Ultimate Doom as UDOOM.WAD instead of DOOMU.WAD) needs its
    # actual lump contents checked instead. identify_iwad.py can only tell
    # the three episodic IWADs apart this way; DOOM2/PLUTONIA/TNT share the
    # same MAP01-MAP32 layout and still rely on the filename.
    try:
        detected = subprocess.run(
            [sys.executable, str(HERE / "identify_iwad.py"), str(wad)],
            check=True, capture_output=True, text=True,
        ).stdout.strip()
    except subprocess.CalledProcessError:
        detected = "UNKNOWN"

    if detected in ("DOOM1", "DOOM", "DOOMU") and detected != prefix:
        log(f"note: {wad.name}'s contents say it's {detected}, not {prefix} (filename) - using {detected}.")
        prefix = detected

    if prefix not in KNOWN_PREFIXES:
        die(
            f"{wad.name}: can't tell which IWAD this is (got '{prefix}').\n"
            f"  rename it to one of: {', '.join(p + '.WAD' for p in KNOWN_PREFIXES)}"
        )
    return prefix


def ensure_music(prefix: str, wad: Path, force: bool) -> None:
    music_dir = ROOT / "music_wav" / prefix
    have_music = music_dir.is_dir() and any(music_dir.glob("*.wav"))

    if have_music and not force:
        log(f"music already rendered in {music_dir} (use --force-music to redo it)")
        return

    log("rendering music (needs fluidsynth + ffmpeg on PATH)...")
    rv = subprocess.run(
        [sys.executable, str(HERE / "render_music.py"), str(wad), "-o", str(music_dir)],
    )
    if rv.returncode != 0:
        die("music rendering failed (see above) - is fluidsynth/ffmpeg installed and on PATH?")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--wad", type=Path, default=None, help="path to your IWAD (default: the one WAD in input/)")
    ap.add_argument("--force-music", action="store_true", help="re-render music even if already present")
    ap.add_argument("--out", type=Path, default=None, help="output ROM path (default: output/<PREFIX>.z64)")
    args = ap.parse_args()

    wad = find_wad(args.wad)
    prefix = detect_prefix(wad)
    log(f"IWAD: {wad} ({prefix})")

    ensure_music(prefix, wad, args.force_music)

    log("packing ROM...")
    cmd = [sys.executable, str(HERE / "pack_rom.py"), prefix, "--wad", str(wad)]
    if args.out:
        cmd += ["--out", str(args.out)]
    rv = subprocess.run(cmd)
    if rv.returncode != 0:
        die("packing failed (see above)")


if __name__ == "__main__":
    main()
