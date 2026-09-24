// Emacs style mode select   -*- C++ -*- h
//-----------------------------------------------------------------------------
//
// $Id:$
//
// Copyright (C) 1993-1996 by id Software, Inc.
//
// This source is available for distribution and/or modification
// only under the terms of the DOOM Source Code License as
// published by id Software. All rights reserved.
//
// The source is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// FITNESS FOR A PARTICULAR PURPOSE. See the DOOM Source Code License
// for more details.
//
// $Log:$
//
// DESCRIPTION:
//	DOOM graphics stuff for Nintendo 64, libdragon
//
//-----------------------------------------------------------------------------

#include <libdragon.h>
#include <stdlib.h>
#include <math.h>

#include "doomstat.h"
#include "i_system.h"
#include "v_video.h"
#include "m_argv.h"
#include "d_main.h"
#include "i_video.h"

#include "z_zone.h"
#include "w_wad.h"

#include "doomdef.h"

// externs
extern void* bufptr;
extern uint8_t *screens[2];

// function prototypes
void I_SetPalette(byte* palette);
void I_FinishUpdate(void);

// locals
static surface_t* disp;
static uint16_t __attribute__((aligned(64))) current_palarray[256];
// uncached view of current_palarray
static uint16_t *palarray;

// N64 Video Interface (VI) registers - fixed physical address on all N64
// hardware. libdragon's own copy of these lives in a private header
// (src/vi.h) not exposed by <libdragon.h>, so we address them directly
// here rather than depend on vendored library internals.
#define VI_REG(index) ((volatile uint32_t*)0xA4400000 + (index))
#define VI_H_VIDEO_REG VI_REG(9)
#define VI_V_VIDEO_REG VI_REG(10)
#define VI_X_SCALE_REG VI_REG(12)
#define VI_Y_SCALE_REG VI_REG(13)

// functions

// See i_video.h - true once I_InitGraphics has called display_init().
int display_ready = 0;

// Which view of screens[0]/[1] Doom's software renderer draws through.
// Uncached (0xA0000000, the default) is the faster one, measured on real
// hardware with PERF_DEBUG: the cached view was clearly slower. Doom draws
// walls and sprites as vertical columns, one byte every 320, and the
// VR4300's 8KB direct-mapped D-cache allocates a line on every store miss -
// so nearly each pixel of a column cost a 16-byte line *read* from RDRAM
// (plus its later writeback), and the framebuffer evicted the textures and
// colormaps the renderer was reading. Uncached stores just go out through
// the write buffer. The cached path (a writeback of the whole frame before
// the blit) is kept only so PERF_DEBUG can still compare the two.
static int fb_cached = 0;

// rdpq_detach_show() only queues the blit; the RDP then reads screens[0]
// while the CPU carries on. Waiting for it right there (rspq_wait) idled the
// CPU for the whole blit every frame. With blit_async the wait moves to the
// start of the next D_Display instead (I_WaitBlit), so the blit overlaps
// the game tics and audio that run in between.
static int blit_async = 1;
static int blit_pending = 0;

#if PERF_DEBUG
static uint32_t perf_wait_ticks;
#endif

static void *I_CachedView(void *p)
{
    return (void *)(((uintptr_t)p & 0x1FFFFFFF) | 0x80000000);
}

static void *I_UncachedView(void *p)
{
    return (void *)(((uintptr_t)p & 0x1FFFFFFF) | 0xA0000000);
}

// Points screens[]/bufptr at the cached or uncached view per fb_cached.
// Writing back and invalidating first matters when switching away from the
// cached view: dirty lines still in the cache would otherwise be written
// back later, on top of newer uncached writes.
static void I_ApplyFramebufferMode(void)
{
    data_cache_hit_writeback_invalidate(I_CachedView(screens[0]), SCREENWIDTH*SCREENHEIGHT);
    data_cache_hit_writeback_invalidate(I_CachedView(screens[1]), SCREENWIDTH*SCREENHEIGHT);

    if (fb_cached)
    {
        screens[0] = I_CachedView(screens[0]);
        screens[1] = I_CachedView(screens[1]);
    }
    else
    {
        screens[0] = I_UncachedView(screens[0]);
        screens[1] = I_UncachedView(screens[1]);
    }
    bufptr = screens[0];
}

void I_WaitBlit(void)
{
    if (!blit_pending)
    {
        return;
    }
#if PERF_DEBUG
    uint32_t t = get_ticks();
#endif
    rspq_wait();
#if PERF_DEBUG
    perf_wait_ticks += get_ticks() - t;
#endif
    blit_pending = 0;
}

surface_t *lockVideo(int wait)
{
    if (wait)
    {
        return display_get();
    }
    else
    {
        return display_try_get();
    }
}

void unlockVideo(surface_t *dc)
{
    if (dc)
    {
        display_show(dc);
    }
}


void I_StartFrame(void)
{
}

void I_ShutdownGraphics(void)
{
}


void I_UpdateNoBlit(void)
{
}


void I_FinishUpdate(void)
{
#if 1
    //while( !(disp = display_get()) );
    disp = display_get();
    // Attach the RDP to the display buffer
    rdpq_attach_clear(disp, NULL);

    // Doom's renderer assumes a single persistent framebuffer and only
    // redraws the parts of the screen that changed (status bar digits,
    // menu text, HUD messages, etc). Ping-ponging the *source* buffer
    // here between two software buffers (like the display's hardware
    // double buffering does) meant those partial redraws only ever
    // landed on one of the two buffers, so whichever buffer didn't get
    // the update would flash its stale (or, on the very first frames,
    // zeroed/black) contents every other frame - most visible as a
    // flickering black bar under the status bar. Always blit the same
    // single software buffer instead; the hardware double buffering
    // from display_get()/rdpq_detach_show() still gives us tear-free
    // presentation.
    // The frame may still be sitting in the data cache (see fb_cached):
    // the RDP reads RDRAM directly, so flush it there first.
    if (fb_cached)
    {
        data_cache_hit_writeback(screens[0], SCREENWIDTH*SCREENHEIGHT);
    }

    surface_t src = surface_make(screens[0], FMT_CI8, SCREENWIDTH, SCREENHEIGHT, SCREENWIDTH);
    rdpq_tex_blit(&src, 0, 0, NULL);

    // Detach from the surface and display it once done
    rdpq_detach_show();

    // screens[0] is also the RDP's *source* texture for the blit above, and
    // Doom's software renderer starts scribbling into it again for the next
    // tic as soon as this function returns. rdpq_detach_show() is fire-and-
    // forget (by design, so the CPU can get a head start on the next frame
    // while the RDP is still busy) which is fine when the next frame goes to
    // a different buffer, but here it's the same one: without waiting, the
    // RDP can still be asynchronously reading screens[0] while the CPU is
    // already overwriting it, corrupting whatever the RDP reads mid-write.
    // That race is intermittent and tends to hit whatever was drawn last
    // (e.g. the status bar face/ammo widgets), which is what made them
    // flicker/disappear. Block until the RDP has fully consumed screens[0]
    // before anything draws into it again - with blit_async that wait is
    // deferred to I_WaitBlit at the start of the next D_Display.
    blit_pending = 1;
    if (!blit_async)
    {
        I_WaitBlit();
    }
    return;
#endif
}

//
// I_ReadScreen
//
void I_ReadScreen(uint8_t* scr)
{
    memcpy(scr,bufptr,320*200);
}

//
// Palette stuff.
//


void I_ForcePaletteUpdate(void)
{
}


//
// I_SetPalette
//
void I_SetPalette(byte* palette)
{
    const byte *gammaptr = gammatable[usegamma];

    unsigned int i;

    for (i = 0; i < 256; i++)
    {
        int r = *palette++;
        int g = *palette++;
        int b = *palette++;

        r = gammaptr[r];
        g = gammaptr[g];
        b = gammaptr[b];

        uint16_t col = ((r >> 3) << 11) | ((g >> 3) << 6) | ((b >> 3) << 1);
        /*current_*/palarray[i] = col;
    }

    // do the writeback here so we don't have to every time we submit the screen
//    data_cache_hit_writeback(current_palarray, 256*2);
    // Load the palette
    rdpq_tex_upload_tlut(/*current_*/palarray, 0, 256);
}

void I_SetDefaultPalette(void)
{
    I_SetPalette(W_CacheLumpName ("PLAYPAL",PU_CACHE));
}

void I_InitGraphics(void)
{
    console_clear();
    display_init((resolution_t) {
            .width = SCREENWIDTH,
            .height = SCREENHEIGHT,
            .interlaced = false,
        }, DEPTH_16_BPP, 3, GAMMA_NONE, ANTIALIAS_RESAMPLE);

    // display_init() computes VI_X_SCALE/VI_Y_SCALE assuming progressive
    // NTSC/PAL is always 640x240 active pixels (see libdragon's
    // VI_X_SCALE_SET/VI_Y_SCALE_SET), and rounds each to the nearest
    // fixed-point value. Two problems for a 320x200 framebuffer like
    // Doom's, on both axes: the active window libdragon actually
    // configures for this mode is a few lines/columns short of that
    // 640x240 assumption, so scaling against the assumed reference
    // stretches the image very slightly less than the real active area
    // needs (previously visible as missing rows at the bottom); and
    // because the VI's scaler is bilinear (FILTERS_RESAMPLE), the *last*
    // output row/column blends the real edge with the row/column after it,
    // which doesn't exist, so stray RDRAM contents show up as static right
    // at the edge. Fix both by reading back the real active width/height
    // and solving for the largest scale where the last output row/column's
    // sample position still stays at or under the real last source
    // row/column.
    //
    // (An inset "safe margin" - shrinking this active window and
    // recentering it, so overscan would crop blank border instead of
    // content - was tried here and reverted: it changed nothing, which
    // means the missing content on the right isn't a display/overscan
    // crop at all - it's not being drawn into the framebuffer in the
    // first place. That's a separate bug to chase down in the renderer,
    // not a VI timing fix.)
    {
        uint32_t h_video = *VI_H_VIDEO_REG;
        uint32_t active_width = (h_video & 0xFFFF) - (h_video >> 16);
        *VI_X_SCALE_REG = (1024 * (SCREENWIDTH-1)) / (active_width-1);

        uint32_t v_video = *VI_V_VIDEO_REG;
        uint32_t active_halflines = (v_video & 0xFFFF) - (v_video >> 16);
        uint32_t active_lines = active_halflines / 2;
        *VI_Y_SCALE_REG = (1024 * (SCREENHEIGHT-1)) / (active_lines-1);
    }

    // Cap presentation at 30 FPS: exactly every other refresh on a 60 Hz TV,
    // so frames reach the screen at an even pace instead of the irregular
    // 1-or-2-refresh cadence of anything between 30 and 60 (Doom itself
    // tops out at 35). display_get() in I_FinishUpdate waits when needed.
    // Game logic still runs at Doom's 35 tics/s (TryRunTics catches up by
    // running two tics in some frames), so game speed is unchanged.
    display_set_fps_limit(30);

    rdpq_init();
    // Set copy render mode, with palette lookup
    rdpq_set_mode_copy(false);
    rdpq_mode_tlut(TLUT_RGBA16);

    // The palette stays uncached: it's tiny and rarely written, and the RDP
    // reads it directly (rdpq_tex_upload_tlut).
    palarray = (uint16_t *)((uintptr_t)current_palarray | 0xA0000000);

    I_SetDefaultPalette();

    I_ApplyFramebufferMode();

    display_ready = 1;

    printf("I_InitGraphics: Initialized display and RDPQ.\n");
}

#if PERF_DEBUG
//
// Performance overlay (see PERF_DEBUG in i_video.h). Times are averaged over
// one second and shown in milliseconds per frame:
//   TIC  - game tics, input and the wait for the next tic (TryRunTics): at
//          the 35 FPS cap most of this is idle waiting, below it it's ~0
//   SND  - positional sound updates and the mixer pump (I_UpdateSound)
//   REN  - D_Display: drawing the frame and submitting it, WAIT included
//   WAIT - time the CPU sat idle for the RDP to finish a blit
//
extern void M_WriteText(int x, int y, char *string);

static char perf_line[3][48];
static uint32_t perf_frames, perf_start;
static uint64_t perf_sum[3], perf_wait_sum;

static int perf_hundredths(uint64_t ticks, uint32_t frames)
{
    // ticks per frame -> hundredths of a millisecond
    return (int)(TICKS_TO_US(ticks / frames) / 10);
}

void I_PerfFrame(uint32_t tic_ticks, uint32_t sound_ticks, uint32_t display_ticks)
{
    uint32_t now = get_ticks();

    perf_sum[0] += tic_ticks;
    perf_sum[1] += sound_ticks;
    perf_sum[2] += display_ticks;
    perf_wait_sum += perf_wait_ticks;
    perf_wait_ticks = 0;
    perf_frames++;

    if (perf_start == 0)
    {
        perf_start = now;
        return;
    }
    if (TICKS_DISTANCE(perf_start, now) < TICKS_PER_SECOND)
    {
        return;
    }

    int fps10 = (int)((uint64_t)perf_frames * 10 * TICKS_PER_SECOND / TICKS_DISTANCE(perf_start, now));
    int t = perf_hundredths(perf_sum[0], perf_frames);
    int s = perf_hundredths(perf_sum[1], perf_frames);
    int r = perf_hundredths(perf_sum[2], perf_frames);
    int w = perf_hundredths(perf_wait_sum, perf_frames);

    heap_stats_t heap;
    sys_get_heap_stats(&heap);

    snprintf(perf_line[0], sizeof(perf_line[0]), "FPS %d.%d  FB %s  BLIT %s",
             fps10 / 10, fps10 % 10,
             fb_cached ? "CACHED" : "UNCACHED", blit_async ? "ASYNC" : "SYNC");
    snprintf(perf_line[1], sizeof(perf_line[1]), "TIC %d.%02d SND %d.%02d REN %d.%02d",
             t / 100, t % 100, s / 100, s % 100, r / 100, r % 100);
    snprintf(perf_line[2], sizeof(perf_line[2]), "WAIT %d.%02d MS  FREE %dK",
             w / 100, w % 100, (heap.total - heap.used) / 1024);

    perf_frames = 0;
    perf_sum[0] = perf_sum[1] = perf_sum[2] = 0;
    perf_wait_sum = 0;
    perf_start = now;
}

void I_PerfDraw(void)
{
    for (int i = 0; i < 3; i++)
    {
        if (perf_line[i][0])
        {
            M_WriteText(2, 2 + i * 9, perf_line[i]);
        }
    }
}

void I_PerfToggleFramebuffer(void)
{
    I_WaitBlit();
    fb_cached = !fb_cached;
    I_ApplyFramebufferMode();
}

void I_PerfToggleBlit(void)
{
    I_WaitBlit();
    blit_async = !blit_async;
}
#endif
