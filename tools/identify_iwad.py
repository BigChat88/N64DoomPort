#!/usr/bin/env python3
"""Identifies which IWAD_PREFIX (src/d_main.c's IdentifyVersion) a WAD file
actually is, from its own contents rather than its filename.

build_all.sh's first guess at IWAD_PREFIX is just the uppercased filename
(DOOM2.WAD -> DOOM2), matching how the engine's own IdentifyVersion() looks
it up (see the "identifier" file the Makefile writes) - but that breaks for
a real IWAD that isn't named exactly what the engine expects. The most
common case in practice: The Ultimate Doom gets distributed under both
DOOMU.WAD and UDOOM.WAD in the wild, and only the former matches.

This looks at which episode lumps are actually present instead, which is
unambiguous for shareware/registered/retail Doom (DOOM1/DOOM/DOOMU): each
is defined purely by how many episodes it ships (E1 only, E1-E3, or E1-E4).
Commercial IWADs (DOOM2/PLUTONIA/TNT) all share the same MAP01-MAP32
structure and aren't distinguishable by content alone this way -
build_all.sh keeps using the filename for those, same as it always did.

Usage:
    python3 identify_iwad.py path/to/some.wad
Prints one of DOOM1, DOOM, DOOMU, CHEX, CHEX2, COMMERCIAL, or UNKNOWN.
"""
import hashlib
import struct
import sys
from pathlib import Path

# chex.wad as shipped with Chex Quest (1996) - note its header says PWAD.
CHEX_SHA1 = {"eca9cff1014ce5081804e193588d96c6ddb35432"}
# chex2.wad (Chex Quest 2, 1997): an add-on for chex.wad, not a standalone
# IWAD - the build merges it onto CHEX.WAD (see merge_wad.py).
CHEX2_SHA1 = {"d5b970834b8ff364d377ef04eb7d12fa6035e10a"}


def file_sha1(path):
    h = hashlib.sha1()
    with open(path, "rb") as fh:
        for block in iter(lambda: fh.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def load_lump_names(wad_path):
    with open(wad_path, "rb") as fh:
        header = fh.read(12)
        if header[:4] not in (b"IWAD", b"PWAD"):
            return None
        numlumps, infotableofs = struct.unpack_from("<ii", header, 4)
        fh.seek(infotableofs)
        names = set()
        for _ in range(numlumps):
            entry = fh.read(16)
            if len(entry) < 16:
                break
            name = entry[8:16].split(b"\x00")[0].decode("ascii", errors="replace").upper()
            names.add(name)
        return names


def identify(wad_path):
    names = load_lump_names(wad_path)
    if not names:
        return "UNKNOWN"

    # Chex Quest can't be told apart by its maps: only E1M1-E1M5 are real,
    # but chex.wad fills every slot up to E4M9 with copies, so it looks
    # exactly like The Ultimate Doom's lump list. Recognize the one
    # released chex.wad by its hash, or anything named CHEX.WAD.
    sha1 = file_sha1(wad_path)
    stem = Path(wad_path).stem.upper()
    if sha1 in CHEX2_SHA1 or stem == "CHEX2":
        return "CHEX2"
    if sha1 in CHEX_SHA1 or stem == "CHEX":
        return "CHEX"
    if "E4M1" in names:
        return "DOOMU"
    if "E2M1" in names:
        return "DOOM"
    if "E1M1" in names:
        return "DOOM1"
    if "MAP01" in names:
        return "COMMERCIAL"
    return "UNKNOWN"


if __name__ == "__main__":
    print(identify(sys.argv[1]))
