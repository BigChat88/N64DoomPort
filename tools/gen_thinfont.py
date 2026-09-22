#!/usr/bin/env python3
"""
Regenerates the thinfont_glyphs table in src/m_thinfont.c by cropping
individual letters out of existing DOOM.WAD menu patches.

Background: the Options menu needed a "Control Settings" row/screen that
doesn't have a pre-rendered WAD graphic. The obvious shortcut - drawing it
with hu_font (the small chat font) scaled up - looks wrong: hu_font is a
different, bolder typeface than the big red "M_" menu font, so scaling it
just produces a chunky mismatch. The "M_" font itself isn't usable as a
generic font either, since the WAD only contains pre-rendered whole words
("OPTIONS", "MOUSE SENSITIVITY", ...), not individual letter graphics.

The fix implemented here: since those whole-word patches already contain
real specimens of every letter we need, crop the letters out of them
directly. Doom's patch format (see patch_t/column_t in r_defs.h) stores
each column as a list of vertical runs of palette-index bytes, so a
column range x0:x1 out of a wider patch is just a normal sub-image -
cropping is exact, no image processing involved, and it keeps the original
per-pixel palette indices (i.e. the bevel shading), so the result is
pixel-for-pixel the same typeface and color ramp as the rest of the menu.

GLYPHS below maps each character to (source lump name, x0, x1) - the column
range in that patch containing a clean copy of that letter. Ranges were
found by rendering each candidate patch at high zoom with a per-column
ruler and reading off where each letterform starts/stops; see the comments
next to each entry for which word it was read from. Letters in this font
are tightly kerned (adjacent letters' columns can touch or slightly
overlap), so a couple of glyphs carry a faint sliver of their neighbor -
inspect the output preview after adding a new glyph and nudge its range if
that's visible.

Usage (from a checkout with the DOOM IWAD available):
    python gen_thinfont.py path/to/DOOM.WAD [-o ../src/m_thinfont.c]

To add a new character: find a WAD patch containing it, crop its column
range the same way, add an entry to GLYPHS, and rerun.
"""

import argparse
import struct

# (source patch lump, x0, x1) - x1 is exclusive. See the module docstring.
#
# All-caps only. A lowercase set was tried (borrowing "sensitivity"'s s/i/t,
# adjacent letters in that patch, for the ones this set doesn't otherwise
# cover) and reverted: those three glyphs were cropped too close to their
# neighbor and carried a visible sliver of it along, reading as garbled/
# badly-clipped strokes in game. O/N/E/R/G/L/Y/P below are genuinely shorter
# than the capitals in their source patches (this font's actual lowercase
# forms), which is fine to draw as-is; it's specifically S/I/T that need a
# real capital, and those are cropped from confirmed word-initial capitals
# below rather than from "sensitivity". (Confirming which of two similar
# crops is the real capital isn't just "is it round or tall" - round
# letters show similar top-row overshoot either way in this font - the
# reliable tell is a straight-edged feature, a stem or crossbar, reaching
# row 0 like a confirmed capital's does, vs. only row ~3 like a confirmed
# lowercase letter's does.)
GLYPHS = {
    'C': ('M_SKILL',  0,  16),  # "Choose Skill Level:" (capital, word-initial)
    'S': ('M_SCRNSZ', 0,  15),  # "Screen Size" (capital, word-initial)
    'T': ('M_EPI2',   0,  15),  # "The Shores of Hell" (capital, word-initial)
    'I': ('M_EPI3',   0,   6),  # "Inferno" (capital, word-initial)
    ':': ('M_MESSG', 113, 118), # "Messages:"
    'E': ('M_EPI3',  33,  45),  # "Inf-e-rno"
    'G': ('M_MESSG', 71,  86),  # "Messa-g-es"
    'L': ('M_SVOL', 114, 124),  # "Vo-l-ume"
    'N': ('M_EPI3',   6,  21),  # "I-n-ferno"
    'O': ('M_EPI3',  73,  90),  # "Infern-o"
    'P': ('M_EPISOD', 84, 96),  # "E-p-isode"
    'R': ('M_EPI3',  45,  58),  # "Infe-r-no"
    'Y': ('M_MSENS', 191, 206), # "sensitivit-y"
    'M': ('M_MSENS',   0,  16), # "Mouse..." (word-initial, cleanly isolated from 'o')
    'U': ('M_MSENS',  31,  47), # "Mo-u-se"
    # V's right edge was originally cropped one column too wide (99, not 98)
    # and carried a sliver of the following 'o' in - see the M/U/V bug report.
    'V': ('M_SVOL',   83,  98), # "Sound  v-olume" (word-initial, isolated from the space before it)

    # Lowercase letters below are real case-specific glyphs (see find_glyph()
    # in m_thinfont.c: exact-case lookup first, toupper() fallback second),
    # added because C/S/I/T are forced-capital shapes (see the big comment
    # above) that looked wrong rendered mid-word, e.g. "MuSIC" or "SeTTIngs".
    # They're sourced from mid-title lowercase words, not word-initial ones.
    't': ('M_EPI1',  159, 170), # "Knee-Deep in [t]he Dead" - "the" isn't the
                                # title's first word, so it's genuinely lowercase
    'i': ('M_SCRNSZ',106, 112), # "S[i]ze"
    's': ('M_EPI2',  124, 137), # "Shore[s]" (word-final, clean right edge)
    'c': ('M_ULTRA', 170, 180), # "Ultra-Violen[c]e" - the only lowercase c in
                                # any harvestable menu patch; "Ultra-Violence"
                                # is otherwise packed with no full column gap
                                # anywhere, but this letter's own shape (open
                                # on the right at mid-height, closed top/
                                # bottom) is unambiguous even where it touches
                                # its neighbors - see the Music/"MusiC" bug.
    'h': ('M_EPI1',  170, 185), # "T[h]e" - immediately after the lowercase
                                #'t' above in the same word; top rows show
                                # it as two separate strokes (no top crossbar
                                # yet), matching a real 'h', not a capital H.
    'a': ('M_NGAME',  74,  81), # "G[a]me" - genuinely lowercase, cleanly
                                # isolated between G's tail and 'm' (see the
                                # 'm' entry below, same word). Replaces an
                                # earlier crop from "De[a]d" (M_EPI1
                                # 237-247) that carried a ragged, stepped
                                # notch out of its bottom-left corner and
                                # read as a cut-off stroke in game.
    'm': ('M_NGAME',  81,  97), # "Ga[m]e" - genuinely lowercase, with real
                                # diagonal strokes (unlike the earlier
                                # straight-legs-and-bridge synthesized
                                # attempts, none of which read as "M" - see
                                # the iteration-3/4/5 bug reports). Found by
                                # rendering M_NGAME with the real PLAYPAL
                                # palette instead of guessing from raw
                                # pixel dumps.
}

TF_HEIGHT = 15


def read_patch(wad_data, names, lump_name):
    filepos, _size = names[lump_name]
    width, height, _left, _top = struct.unpack('<hhhh', wad_data[filepos:filepos+8])
    colofs = struct.unpack('<%di' % width, wad_data[filepos+8:filepos+8+4*width])
    grid = [[0]*width for _ in range(height)]
    for col in range(width):
        p = filepos + colofs[col]
        while True:
            topdelta = wad_data[p]
            if topdelta == 0xff:
                break
            length = wad_data[p+1]
            src = p + 3
            for i in range(length):
                y = topdelta + i
                if 0 <= y < height:
                    grid[y][col] = wad_data[src+i]
            p = src + length + 1
    return width, height, grid


def load_wad_directory(wad_data):
    ident, numlumps, infotableofs = struct.unpack('<4sii', wad_data[:12])
    if ident not in (b'IWAD', b'PWAD'):
        raise SystemExit("not a WAD file")
    names = {}
    for i in range(numlumps):
        off = infotableofs + i * 16
        filepos, size, name = struct.unpack('<ii8s', wad_data[off:off+16])
        name = name.split(b'\x00')[0].decode('ascii', errors='replace')
        names.setdefault(name, (filepos, size))
    return names


def crop_glyph(wad_data, names, lump_name, x0, x1):
    width, height, grid = read_patch(wad_data, names, lump_name)
    rows = []
    for y in range(TF_HEIGHT):
        row = []
        for x in range(x0, x1):
            v = grid[y][x] if (0 <= x < width and y < height) else 0
            row.append(v)
        rows.append(row)
    return x1 - x0, rows


def format_glyph(ch, width, rows):
    row_strs = ["{" + ",".join(str(v) for v in row) + "}" for row in rows]
    label = 'colon' if ch == ':' else ch
    return "    { %d, %d, { %s } }, // '%s'" % (ord(ch), width, ", ".join(row_strs), label)


def synth_b(rows_by_char):
    """Builds a 'b' glyph by flipping the already-cropped 'P' vertically.

    No supported IWAD has a menu patch containing a cleanly isolated
    lowercase or uppercase 'b' (or 'f') to crop - see the GLYPHS comment
    above. 'P' is a plain stem topped with a bowl; flip it top-to-bottom
    and it's a plain stem with a bowl at the *bottom* instead - exactly a
    'b'. (An earlier attempt spliced 'h''s stem with a mirrored 'c' bowl
    running the full stem height, which read as 'D' instead of 'b' - see
    the iteration-2/3 bug reports.) The flip only reverses the 12 rows of
    actual content (rows 3-14); the 3 blank padding rows stay on top so
    the baseline (row 14) doesn't move.
    """
    p_rows = rows_by_char['P']
    width = len(p_rows[0])
    content = p_rows[3:15]
    out_rows = [[0] * width for _ in range(3)] + content[::-1]
    return width, out_rows


def brighten_left_edge(rows, char_rows=range(3, 15)):
    """Solidifies a glyph's leftmost column to 191 wherever it's already
    nonzero, for glyphs whose source patch anti-aliases that edge dimmer
    than this font's other stems (e.g. 'R' - see the iteration-2 bug
    report: at the very start of a word, with nothing to its left, that
    softer edge reads as a missing stroke instead of shading).
    """
    for r in char_rows:
        if rows[r][0]:
            rows[r][0] = 191
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('wad', help="path to DOOM.WAD (or any IWAD containing the source patches)")
    parser.add_argument('-o', '--output', help="write the generated table to this file instead of stdout")
    args = parser.parse_args()

    with open(args.wad, 'rb') as fh:
        wad_data = fh.read()
    names = load_wad_directory(wad_data)

    max_width = 0
    rows_by_char = {}
    entries = []
    for ch in sorted(GLYPHS):
        lump_name, x0, x1 = GLYPHS[ch]
        width, rows = crop_glyph(wad_data, names, lump_name, x0, x1)
        if ch == 'R':
            rows = brighten_left_edge(rows)
        rows_by_char[ch] = rows
        entries.append((ch, width, rows))

    b_width, b_rows = synth_b(rows_by_char)
    entries.append(('b', b_width, b_rows))
    entries.sort(key=lambda e: ord(e[0]))

    lines = ["const thinglyph_t thinfont_glyphs[] = {"]
    for ch, width, rows in entries:
        max_width = max(max_width, width)
        lines.append(format_glyph(ch, width, rows))
    lines.append("};")
    lines.append("")
    lines.append("const int thinfont_numglyphs = sizeof(thinfont_glyphs) / sizeof(thinfont_glyphs[0]);")

    print("// TF_MAXWIDTH must be >= %d (see m_thinfont.h)" % max_width)
    output = "\n".join(lines)
    if args.output:
        with open(args.output, 'w') as fh:
            fh.write(output + "\n")
        print("wrote %s" % args.output)
    else:
        print(output)


if __name__ == '__main__':
    main()
