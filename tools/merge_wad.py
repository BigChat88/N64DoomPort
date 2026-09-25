#!/usr/bin/env python3
"""Merges a PWAD on top of an IWAD into one standalone WAD.

The engine loads a single WAD from the ROM, so an add-on that the original
game loaded with -file - Chex Quest 2's chex2.wad on top of chex.wad - is
merged into its base here instead, producing what the engine would have
seen with both loaded:

  * a map in the PWAD (E1M1 / MAP01 plus its THINGS, LINEDEFS, ... lumps)
    replaces the whole map block of the same name in the base;
  * sprites, flats and patches (S_START/S_END, F_START/F_END and
    P_START/P_END, single or double letter) replace the base lump of the
    same name *in the same namespace* - the same name can mean different
    things in different namespaces;
  * for sprites, a PWAD frame (e.g. TROOA) also drops every base lump of
    that frame it doesn't itself provide, so a frame redrawn with a
    different set of rotations (TROOA1 vs TROOA2A8) can't end up with a mix
    of both, which R_InitSprites rejects;
  * any other lump replaces the base lump of the same name - a name that
    appears more than once (e.g. a patch in both P1 and P2) is replaced
    occurrence by occurrence, in order;
  * lumps the base doesn't have go at the end of their namespace.

Usage:
    python3 merge_wad.py base.wad addon.wad -o merged.wad
"""
import argparse
import re
import struct
import sys

MAP_HEADER = re.compile(r"^(E\dM\d|MAP\d\d)$")
MAP_LUMPS = {"THINGS", "LINEDEFS", "SIDEDEFS", "VERTEXES", "SEGS", "SSECTORS",
             "NODES", "SECTORS", "REJECT", "BLOCKMAP", "BEHAVIOR"}

# namespace -> (start markers, end markers)
NAMESPACES = {
    "S": ({"S_START", "SS_START"}, {"S_END", "SS_END"}),
    "F": ({"F_START", "FF_START"}, {"F_END", "FF_END"}),
    "P": ({"P_START", "PP_START"}, {"P_END", "PP_END"}),
}
SUB_MARKER = re.compile(r"^[FP][123]_(START|END)$")


def read_wad(path):
    with open(path, "rb") as fh:
        data = fh.read()
    ident = data[:4]
    if ident not in (b"IWAD", b"PWAD"):
        raise SystemExit(f"{path}: not a WAD file")
    numlumps, infotableofs = struct.unpack_from("<ii", data, 4)
    lumps = []
    for i in range(numlumps):
        pos, size, raw = struct.unpack_from("<ii8s", data, infotableofs + 16 * i)
        name = raw.split(b"\0")[0].decode("ascii", errors="replace").upper()
        lumps.append((name, data[pos:pos + size]))
    return ident, lumps


def classify(lumps):
    """Yields (kind, key, lumps) units: ("map", name, [...]),
    ("marker", name, [lump]) or ("lump", (namespace, name), [lump])."""
    ns = ""
    i = 0
    while i < len(lumps):
        name, data = lumps[i]
        if MAP_HEADER.match(name):
            j = i + 1
            while j < len(lumps) and lumps[j][0] in MAP_LUMPS:
                j += 1
            yield "map", name, lumps[i:j]
            i = j
            continue
        started = [k for k, (starts, _) in NAMESPACES.items() if name in starts]
        ended = [k for k, (_, ends) in NAMESPACES.items() if name in ends]
        if started or ended or SUB_MARKER.match(name):
            if started:
                ns = started[0]
            elif ended:
                ns = ""
            yield "marker", name, [lumps[i]]
        else:
            yield "lump", (ns, name), [lumps[i]]
        i += 1


def merge(base, addon):
    maps = {}
    replace = {}
    sprite_frames = set()
    for kind, key, block in classify(addon):
        if kind == "map":
            maps[key] = block
        elif kind == "lump":
            replace.setdefault(key, []).append(block[0])
            if key[0] == "S":
                sprite_frames.add(key[1][:5])

    out = []
    used = set()      # maps taken from the addon
    seen = {}         # lump key -> base occurrences replaced so far
    for kind, key, block in classify(base):
        if kind == "map":
            if key in maps:
                out.extend(maps[key])
                used.add(("map", key))
            else:
                out.extend(block)
            continue
        if kind == "marker":
            ends = [k for k, (_, e) in NAMESPACES.items() if key in e]
            if ends:
                # base is about to close this namespace: add what the addon
                # has for it that the base didn't
                for (ns, name), lumps in replace.items():
                    if ns == ends[0] and (ns, name) not in seen:
                        out.extend(lumps)
                        seen[(ns, name)] = len(lumps)
            out.extend(block)
            continue
        if key in replace:
            n = seen.get(key, 0)
            lumps = replace[key]
            # i-th base occurrence -> i-th addon occurrence (the last one
            # again if the base has more)
            out.append(lumps[min(n, len(lumps) - 1)])
            seen[key] = n + 1
        elif key[0] == "S" and key[1][:5] in sprite_frames:
            continue  # stale rotation of a frame the addon redrew
        else:
            out.extend(block)

    for name, block in maps.items():
        if ("map", name) not in used:
            out.extend(block)
    for key, lumps in replace.items():
        if key not in seen:
            out.extend(lumps)
    return out


def write_wad(path, ident, lumps):
    header_size = 12
    body = bytearray()
    directory = bytearray()
    for name, data in lumps:
        directory += struct.pack("<ii8s", header_size + len(body), len(data),
                                 name.encode("ascii"))
        body += data
    with open(path, "wb") as fh:
        fh.write(struct.pack("<4sii", ident, len(lumps), header_size + len(body)))
        fh.write(body)
        fh.write(directory)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("base", help="the IWAD the add-on runs on (e.g. CHEX.WAD)")
    ap.add_argument("addon", help="the PWAD to merge on top (e.g. CHEX2.WAD)")
    ap.add_argument("-o", "--output", required=True, help="merged WAD to write")
    args = ap.parse_args()

    _, base = read_wad(args.base)
    _, addon = read_wad(args.addon)
    merged = merge(base, addon)
    write_wad(args.output, b"IWAD", merged)
    print(f"merged {args.addon} ({len(addon)} lumps) onto {args.base} "
          f"({len(base)} lumps) -> {args.output} ({len(merged)} lumps)")


if __name__ == "__main__":
    sys.exit(main())
