//-----------------------------------------------------------------------------
//
// DESCRIPTION:
//    Minimal DeHackEd patch loader - see d_deh.c.
//
//-----------------------------------------------------------------------------

#ifndef __D_DEH__
#define __D_DEH__

// Applies the DeHackEd patch at the given DFS path, if there is one there.
// Must run after the WAD is loaded and before R_Init/S_Init, since a patch
// can rename sprites and sounds.
void DEH_LoadFile(const char *path);

// Returns the patch's replacement for one of the engine's built-in strings
// (pickup messages, menu prompts, level names, finale text...), or the
// string itself if the patch doesn't replace it.
char *DEH_String(char *s);

#endif
