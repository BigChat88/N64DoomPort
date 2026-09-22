#!/usr/bin/env python3
"""
pack_rom.py -- turn your own IWAD into a Nintendo 64 ROM without Docker.

This is the "packer": it does everything src/Makefile's z64 rule does
*except* compile the engine and render music. Those two inputs come from
elsewhere:

  * the compiled engine (doom.elf.stripped + doom.sym + doom.msym) is the
    same bytes for every IWAD (see src/Makefile's `engine` target) - grab a
    prebuilt copy from a GitHub Release, or build it yourself with
    `make engine` inside `libdragon exec`. See tools/vendor/engine/README.md.
  * the rendered music comes from tools/render_music.py, which needs
    fluidsynth + ffmpeg on your host - run that first.

    python tools/pack_rom.py DOOM2
    python tools/pack_rom.py DOOM2 --wad path/to/DOOM2.WAD --music-dir path/to/wavs

Requires the libdragon host tools (mkdfs, n64tool, audioconv64) -- found on
PATH, in $N64_INST/bin, or in tools/vendor/bin/. See tools/vendor/bin/README.md.
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent

KNOWN_PREFIXES = ("DOOM1", "DOOM", "DOOMU", "DOOM2", "PLUTONIA", "TNT")

# Loose, non-WAD assets baked into every ROM (menu graphics, boot animation)
# - everything in src/filesystem/ except the per-IWAD pieces this script
# fills in itself (the WAD, its "identifier" stamp, and its music).
STATIC_FS_SKIP = {"mus"}
STATIC_FS_SKIP_SUFFIX = (".WAD",)

TOOL_NAMES = ("mkdfs", "n64tool", "audioconv64")
ENGINE_FILES = ("doom.elf.stripped", "doom.sym", "doom.msym")


def project_version() -> str:
    f = ROOT / "VERSION"
    try:
        return f.read_text(encoding="utf-8").strip() or "0.0.0"
    except OSError:
        return "0.0.0"


def log(msg: str) -> None:
    print(msg, flush=True)


def die(msg: str):
    print(f"error: {msg}", file=sys.stderr)
    sys.exit(1)


def run(cmd: list, **kw) -> None:
    log("  $ " + " ".join(str(c) for c in cmd))
    subprocess.run([str(c) for c in cmd], check=True, **kw)


def exe(name: str) -> str:
    return name + (".exe" if os.name == "nt" else "")


def find_tool(name: str, extra_dir: Path | None) -> Path:
    cands: list = []
    if extra_dir:
        cands.append(extra_dir / exe(name))
    n64_inst = os.environ.get("N64_INST")
    if n64_inst:
        cands.append(Path(n64_inst) / "bin" / exe(name))
    cands.append(HERE / "vendor" / "bin" / exe(name))
    which = shutil.which(name)
    if which:
        cands.append(Path(which))
    for c in cands:
        if c.is_file():
            return c
    die(
        f"host tool '{name}' not found.\n"
        f"  looked in: {', '.join(str(c) for c in cands) or '(nowhere)'}\n"
        f"  build it with tools/vendor/bin/build-tools.sh, install the\n"
        f"  libdragon toolchain and set N64_INST, or pass --tools-dir"
    )


def find_engine(engine_dir: Path | None) -> tuple:
    dirs = []
    if engine_dir:
        dirs.append(engine_dir)
    dirs.append(HERE / "vendor" / "engine")
    for d in dirs:
        elf = d / "doom.elf.stripped"
        sym = d / "doom.sym"
        msym = d / "doom.msym"
        if elf.is_file() and sym.is_file() and msym.is_file():
            return elf, sym, msym
    die(
        "compiled engine not found (doom.elf.stripped + doom.sym + doom.msym).\n"
        f"  looked in: {', '.join(str(d) for d in dirs)}\n"
        "  download one from a Release, build it with `make engine` inside\n"
        "  `libdragon exec`, or pass --engine-dir. See tools/vendor/engine/README.md"
    )


def stage_static_filesystem(dest: Path) -> None:
    src_fs = ROOT / "src" / "filesystem"
    if not src_fs.is_dir():
        return
    for item in sorted(src_fs.iterdir()):
        if item.name in STATIC_FS_SKIP or item.name == "identifier":
            continue
        if item.name.endswith(STATIC_FS_SKIP_SUFFIX):
            continue
        if item.is_dir():
            shutil.copytree(item, dest / item.name)
        else:
            shutil.copy2(item, dest / item.name)


def main() -> None:
    ap = argparse.ArgumentParser(
        description="Pack your own IWAD + prebuilt engine into a Nintendo 64 ROM.")
    ap.add_argument("--version", action="version",
                    version=f"N64DoomPort {project_version()}")
    ap.add_argument("prefix", choices=KNOWN_PREFIXES,
                    help="which IWAD this is (matches its filename)")
    ap.add_argument("--wad", type=Path, default=None,
                    help="path to the IWAD (default: input/<prefix>.WAD)")
    ap.add_argument("--music-dir", type=Path, default=None,
                    help="dir of <lump>.wav files rendered by "
                         "tools/render_music.py (default: music_wav/<prefix>)")
    ap.add_argument("--out", type=Path, default=None,
                    help="output ROM path (default: output/<prefix>.z64)")
    ap.add_argument("--title", default=None,
                    help="ROM title, max 20 chars (default: <prefix>)")
    ap.add_argument("--engine-dir", type=Path,
                    help="dir with doom.elf.stripped + doom.sym + doom.msym "
                         "(default: tools/vendor/engine)")
    ap.add_argument("--tools-dir", type=Path,
                    help="dir with the libdragon host tools "
                         "(default: PATH, $N64_INST/bin, tools/vendor/bin)")
    ap.add_argument("--keep-work", action="store_true",
                    help="keep the temporary work directory")
    args = ap.parse_args()

    log(f"N64DoomPort {project_version()}")

    wad = args.wad or (ROOT / "input" / f"{args.prefix}.WAD")
    if not wad.is_file():
        die(f"{wad}: no such file (put your IWAD there, or pass --wad)")

    music_dir = args.music_dir or (ROOT / "music_wav" / args.prefix)
    music_wavs = sorted(music_dir.glob("*.wav")) if music_dir.is_dir() else []
    if not music_wavs:
        die(
            f"no rendered music in {music_dir}\n"
            f"  run this first (needs fluidsynth + ffmpeg on PATH):\n"
            f"  python3 tools/render_music.py {wad} -o {music_dir}"
        )

    out = args.out or (ROOT / "output" / f"{args.prefix}.z64")
    title = (args.title or args.prefix)[:20]

    elf, sym, msym = find_engine(args.engine_dir)
    tools = {t: find_tool(t, args.tools_dir) for t in TOOL_NAMES}
    log(f"wad    : {wad}")
    log(f"music  : {music_dir}  ({len(music_wavs)} track(s))")
    log(f"engine : {elf}")
    for t, p in tools.items():
        log(f"tool   : {t:12s} {p}")

    work = Path(tempfile.mkdtemp(prefix="doompack-"))
    fsroot = work / "filesystem"
    fsroot.mkdir()
    try:
        log("[1/4] staging filesystem")
        stage_static_filesystem(fsroot)
        shutil.copy2(wad, fsroot / f"{args.prefix}.WAD")
        (fsroot / "identifier").write_text(f"{args.prefix}.WAD\n")

        log("[2/4] audioconv64 -> mus/*.wav64")
        mus_out = fsroot / "mus"
        mus_out.mkdir()
        for wav in music_wavs:
            run([tools["audioconv64"], "--wav-compress", "3", "--wav-mono",
                 "--wav-loop", "true", "-o", str(mus_out), wav.name],
                cwd=str(music_dir))

        log("[3/4] mkdfs")
        dfs = work / "rom.dfs"
        run([tools["mkdfs"], str(dfs), str(fsroot) + "/"])

        log("[4/4] n64tool")
        out.parent.mkdir(parents=True, exist_ok=True)
        run([tools["n64tool"], "--title", title, "--toc", "--output", str(out),
             "--align", "256", str(elf),
             "--align", "8", str(sym),
             "--align", "8", str(msym),
             "--align", "16", str(dfs)])

        size = out.stat().st_size
        log(f"\nOK -> {out}  ({size/1024/1024:.1f} MiB)")
    finally:
        if args.keep_work:
            log(f"work dir kept: {work}")
        else:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    main()
