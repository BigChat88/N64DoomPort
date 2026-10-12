#!/usr/bin/env python3
"""Finds music lumps that are byte-identical copies of an earlier one.

Several IWADs ship the same song under more than one lump name - DOOM2.WAD
alone has 13 such copies (D_THE_DA, its longest track at ~7 minutes, is
there three times as D_THE_DA/D_THEDA2/D_THEDA3), and TNT, Plutonia and
DOOM/Chex have more. Every copy used to be rendered and packed into the ROM
as its own Opus stream, which is most of what pushed DOOM2's ROM past 64MB.

Instead, each song is packed once - under the first lump name it appears
with, in WAD order - and an alias table maps the other names to it. The
engine reads that table (mus/aliases.txt, see I_RegisterSong in
src/i_sound.c) before opening a song.

Usage:
    python3 music_aliases.py some.wad -o aliases.txt
writes one "ALIAS ORIGINAL" line per copy, e.g. "D_THEDA2 D_THE_DA".
"""
import argparse
import hashlib
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from merge_wad import read_wad  # noqa: E402

MUS_MAGIC = b"MUS\x1a"


def music_aliases(lumps):
    """lumps: [(name, data), ...] in WAD order. Returns {alias: original} for
    every music lump whose data is identical to an earlier music lump's."""
    first = {}
    aliases = {}
    for name, data in lumps:
        if not name.startswith("D_") or data[:4] != MUS_MAGIC:
            continue
        digest = hashlib.sha1(data).digest()
        if digest in first:
            if name != first[digest]:
                aliases[name] = first[digest]
        else:
            first[digest] = name
    return aliases


def write_aliases(path, aliases):
    with open(path, "w", newline="\n") as fh:
        for alias, original in aliases.items():
            fh.write(f"{alias} {original}\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("wad", help="the WAD the ROM packs (merged, for CHEX2)")
    ap.add_argument("-o", "--output", required=True, help="alias table to write")
    args = ap.parse_args()

    _, lumps = read_wad(args.wad)
    aliases = music_aliases(lumps)
    write_aliases(args.output, aliases)
    print(f"{len(aliases)} duplicate music lump(s) aliased -> {args.output}")


if __name__ == "__main__":
    main()
