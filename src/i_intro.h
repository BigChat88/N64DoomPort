// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// DESCRIPTION:
//    Boot-time "powered by libdragon" dragon logo animation. See i_intro.c
//    for where this comes from.
//
//-----------------------------------------------------------------------------

#ifndef __I_INTRO__
#define __I_INTRO__

// Plays the dragon logo animation. Does its own display_init()/
// display_close() (at a different resolution than Doom's own display), so
// call this before I_InitGraphics runs, e.g. from main() right after
// dfs_init(). Silent - see i_intro.c for why.
void I_DragonIntro(void);

#endif
