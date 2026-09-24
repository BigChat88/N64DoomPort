// Emacs style mode select   -*- C++ -*- 
//-----------------------------------------------------------------------------
//
// $Id:$
//
// Copyright (C) 1993-1996 by id Software, Inc.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// DESCRIPTION:
//	System specific interface stuff.
//
//-----------------------------------------------------------------------------


#ifndef __I_VIDEO__
#define __I_VIDEO__


#include "doomtype.h"

#ifdef __GNUG__
#pragma interface
#endif

#include <stdint.h>

// Called by D_DoomMain,
// determines the hardware configuration
// and sets up the video mode
void I_InitGraphics (void);

// True once I_InitGraphics has run display_init(). I_Error (i_system.c)
// checks this before drawing its on-screen error box: libdragon's
// display_get() asserts if the display was never initialized, and
// I_Error can fire earlier than this (e.g. a bad WAD file, checked well
// before I_Init() runs in D_DoomMain) - the printf a few lines above that
// call is I_Error's only diagnostic in that case.
extern int display_ready;


void I_ShutdownGraphics(void);

// Takes full 8 bit values.
void I_SetPalette (byte* palette);

void I_UpdateNoBlit (void);
void I_FinishUpdate (void);

// Waits for the RDP to finish reading the previous frame out of screens[0],
// if it may still be doing so (see blit_async in i_video.c). Must run before
// anything draws into screens[0] again - D_Display calls it first thing.
void I_WaitBlit (void);

// Performance overlay: FPS and per-phase frame times drawn in the top-left
// corner, plus in-game switches for the two speedups in i_video.c
// (L+D-Left: cached framebuffer on/off, L+D-Right: async blit on/off) so
// their effect can be compared on real hardware. Keep at 0 for normal
// builds - those button combos replace the D-Pad's normal action.
#define PERF_DEBUG 0
#if PERF_DEBUG
void I_PerfFrame (uint32_t tic_ticks, uint32_t sound_ticks, uint32_t display_ticks);
void I_PerfDraw (void);
void I_PerfToggleFramebuffer (void);
void I_PerfToggleBlit (void);
#endif

// Wait for vertical retrace or pause a bit.
void I_WaitVBL(int count);

void I_ReadScreen (uint8_t* scr);

void I_BeginRead (void);
void I_EndRead (void);



#endif
//-----------------------------------------------------------------------------
//
// $Log:$
//
//-----------------------------------------------------------------------------
