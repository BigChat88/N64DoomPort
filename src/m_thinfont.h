// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// DESCRIPTION:
//    A readable bitmap font used to draw freeform menu text (e.g. the
//    Control Settings screen) in the game's own menu typeface.
//
//    hu_font (the small chat/message font) is optimized for tiny HUD text
//    and has thick strokes relative to its height; scaling it up just makes
//    an even chunkier font that doesn't match the rest of the menu. The big
//    red "M_" menu graphics aren't a reusable letter-by-letter font either -
//    they're pre-rendered whole-word images (e.g. "OPTIONS", "MOUSE
//    SENSITIVITY") with no glyph set behind them, so free-form text can't be
//    composed from them directly.
//
//    This font's glyphs are instead cropped directly out of those existing
//    WAD patches, one letter at a time (e.g. the 'C' comes from "CHOOSE
//    SKILL LEVEL:", the 'O' from "INFERNO", ...), keeping their original
//    per-pixel palette indices (including the bevel shading), so text drawn
//    with this font is pixel-for-pixel the same typeface and color ramp as
//    the rest of the menu. See tools/gen_thinfont.py for how the table in
//    m_thinfont.c was produced and how to add more glyphs.
//
//-----------------------------------------------------------------------------

#ifndef __M_THINFONT__
#define __M_THINFONT__

#include "doomdef.h"

// number of bitmap rows per glyph
#define TF_HEIGHT 15
// widest glyph currently defined (the 'O')
#define TF_MAXWIDTH 17

typedef struct
{
    int     ch;                            // ASCII code this glyph represents
    int     width;                         // glyph width in pixels (<= TF_MAXWIDTH)
    uint8_t pixels[TF_HEIGHT][TF_MAXWIDTH]; // palette index per pixel, 0 = transparent
} thinglyph_t;

extern const thinglyph_t thinfont_glyphs[];
extern const int thinfont_numglyphs;

// Draws string with the harvested WAD-glyph font. All-caps: lowercase
// input is upper-cased first. Only the letters/symbols actually harvested
// so far are defined (see m_thinfont.c); anything else falls back to
// blank space width.
void M_WriteThinText(int x, int y, char *string);
int  M_ThinStringWidth(char *string);

#endif
