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
// $Log:$
//
// DESCRIPTION:
//      System interface for sound.
//
//-----------------------------------------------------------------------------

#include <libdragon.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>

#include <unistd.h>

#include "z_zone.h"

#include "i_system.h"
#include "i_sound.h"
#include "m_argv.h"
#include "m_misc.h"
#include "w_wad.h"
#include "m_swap.h"

#include "doomdef.h"
#include "m_fixed.h"

#include <errno.h>

/**********************************************************************/
//
// This engine has been through two earlier music systems, both replaced
// here for the same underlying reason:
//
//   1. The original: a hand-rolled MUS interpreter driving a hand-picked,
//      11025 Hz GM sample bank, mixed sample-by-sample on the CPU and
//      pushed to the AI hardware through a raw, manually-registered
//      interrupt handler - a simplified approximation of the real
//      instrument samples, and entirely separate from libdragon's mixer.
//   2. libdragon's real SoundFont synthesizer (sf64_synth + mid64player),
//      playing the WAD's MUS data (converted to MIDI - see tools/
//      mus2mid.py) live against the real SoundFont. Much better sounding
//      than #1, but turned out fragile in a way that took most of a long
//      debugging session to fully characterize: the song's own per-channel
//      mix data (MIDI CC7, preserved faithfully by the MUS->MIDI
//      conversion) kept fighting whatever tried to impose a master volume
//      on top of it - including every time the music looped, since looping
//      just re-runs the whole MIDI sequence, mix data included - and
//      safe headroom against clipping had to account for worst-case
//      simultaneous polyphony (measured up to 15 notes at once in one
//      track) instead of a single, well-understood source.
//
// This - #3 - sidesteps that whole family of problems the way the sibling
// N64DoomRPGPort project does it: render the music to audio *once*,
// offline, with the same real FluidSynth + SoundFont (see
// tools/render_music.py) - so the audio quality is identical, nothing here
// changes what actually plays, only when - then play the result back as a
// single, plain, already-mastered PCM stream (wav64) through libdragon's
// mixer, exactly like the SFX below. A single channel has a simple,
// well-understood volume control (mixer_ch_set_vol, with nothing else ever
// touching it - no envelope, no per-channel mix data, no races) and a
// simple, well-understood loop (wav64's own native sample loop - just
// repeats the same finished audio; no MIDI events re-firing). Clipping
// safety is handled once at render time (ffmpeg's alimiter) plus a fixed
// mixer headroom here, not continuously at runtime against however many
// synthesizer voices happen to be sounding.
//
/**********************************************************************/

// number of channels available for sound effects
extern int numChannels;

// SFX voices: one per Doom sound channel, each backed by a *pair* of mixer
// channels (2*voice and 2*voice+1). Doom constantly restarts a channel with
// a new sound while the old one is still mid-waveform (every shot of a
// repeating weapon, a monster's sounds replacing each other). Starting the
// new sound on the same mixer channel cuts the old one off wherever it
// happens to be - up to ~80% of full scale - and that step is a loud click
// on every such restart, which is what kept sounding like clipping. With a
// pair, the old sound fades out on one channel while the new one starts on
// the other (see I_StartSound).
#define SFX_VOICES      8
#define SFX_MIXER_CHANNELS (SFX_VOICES * 2)
// Music: a single channel playing one pre-rendered, already-looping wav64
// stream - see the big comment above.
#define MUSIC_CH        SFX_MIXER_CHANNELS
#define NUM_MIXER_CHANNELS (SFX_MIXER_CHANNELS + 1)

// Length (in output samples, ~2.9ms at 44100 Hz) of every SFX fade: in at
// start, out on stop/replace, and gain changes. Same length libdragon uses
// to declick its own mixer_ch_set_vol changes (MIXER_DECLICK_SAMPLES).
#define SFX_DECLICK 128

// Fade-out applied to the last samples of every SFX's own data (at 11025 Hz,
// ~5.8ms), plus zeroed padding after it: many of the WAD's sounds end on a
// non-zero sample (DSPOPAIN on 48, DSSAWUP on -34), which clicks when the
// sound ends, and the resampler reads a few samples past the end, which
// would otherwise be whatever zone memory follows.
#define SFX_TAIL_FADE 64
#define SFX_TAIL_PAD  16

// Which of its two mixer channels each voice is currently using, and whether
// Doom still considers it playing (cleared by I_StopSound, while the fade-out
// is still running on the mixer channel).
static int voice_sub[SFX_VOICES];
static int voice_live[SFX_VOICES];

static int I_VoiceCh(int voice)
{
    return voice * 2 + voice_sub[voice];
}

static int I_VoiceActive(int voice)
{
    return voice_live[voice] && mixer_ch_playing(I_VoiceCh(voice));
}

#if AUDIO_DEBUG
static waveform_t sfx_wave[NUMSFX];  // defined below, in the SFX section
#include "doomstat.h"
extern boolean message_dontfuckwithme;

// Test modes, cycled in-game with L+Start (see i_input.c). Earlier runs
// already showed: no noise at all with SFX muted, crackle with one 8-bit
// voice, clean with one 16-bit voice. Mode 3 of that build also moved the
// data to a 16-byte-aligned buffer, so this round separates the two:
//  0 NORMAL             - unchanged
//  1 1 VOICE 8BIT       - one SFX at a time, WAD data as-is (unaligned)
//  2 1 VOICE 8BIT ALIGN - same 8-bit data, copied to a 16-byte-aligned
//                         buffer: clean -> alignment is the cause
//  3 1 VOICE 16BIT      - widened to 16-bit in an aligned buffer (known clean)
enum { DBG_NORMAL, DBG_ONE, DBG_ONE8A, DBG_ONE16, DBG_NUM_MODES };
static const char *dbg_names[DBG_NUM_MODES] =
{
    "NORMAL", "1 VOICE 8BIT", "1 VOICE 8BIT ALIGN", "1 VOICE 16BIT",
};
static int dbg_mode = DBG_NORMAL;
static char dbg_msg[80];
static uint32_t dbg_last_ms, dbg_report_ms, dbg_max_gap, dbg_starved;

// Modes 2/3's two aligned copies (ping-ponged like a voice's mixer channels,
// so a sound fading out never has its data overwritten under it).
static int16_t *dbg16[2];
static waveform_t dbg_wave16[2];
static int dbg16_sel;

static void I_AudioDebugReport(const char *what)
{
    snprintf(dbg_msg, sizeof(dbg_msg), "%s: %s GAP%lu STARVE%lu",
             what, dbg_names[dbg_mode], dbg_max_gap, dbg_starved);
    players[consoleplayer].message = dbg_msg;
    message_dontfuckwithme = true;
}

void I_AudioDebugCycle(void)
{
    int ch;

    dbg_mode = (dbg_mode + 1) % DBG_NUM_MODES;
    for (ch = 0; ch < SFX_MIXER_CHANNELS; ch++)
    {
        mixer_ch_stop(ch);
    }
    memset(voice_live, 0, sizeof(voice_live));
    dbg_max_gap = 0;
    dbg_starved = 0;
    I_AudioDebugReport("MODE");
}

// Called at the top of I_UpdateSound: the longest time between two mixer
// pumps (ms) and how often the audio queue was found empty - i.e. whether
// the AI was about to run, or had already run, out of audio (a gap in the
// output, heard as a crackle whenever something is audible).
static void I_AudioDebugTick(void)
{
    uint32_t now = get_ticks_ms();

    if (dbg_last_ms)
    {
        uint32_t gap = now - dbg_last_ms;
        if (gap > dbg_max_gap)
        {
            dbg_max_gap = gap;
        }
    }
    dbg_last_ms = now;

    if (audio_get_queued_buffers() == 0)
    {
        dbg_starved++;
    }

    if (now - dbg_report_ms >= 1000)
    {
        dbg_report_ms = now;
        if (gamestate == GS_LEVEL)
        {
            I_AudioDebugReport("AUD");
        }
        dbg_max_gap = 0;
    }
}

// Modes 2/3: copy an SFX into a 16-byte-aligned buffer, keeping it 8-bit
// or widening it to 16-bit, and return a waveform for the copy.
static waveform_t *I_AudioDebugCopy(int id, int bits)
{
    const waveform_t *src = &sfx_wave[id];
    int i;

    dbg16_sel ^= 1;
    if (!dbg16[dbg16_sel])
    {
        // Longest SFX in any supported IWAD is DOOM2's DSBOSSIT, 57064 samples.
        dbg16[dbg16_sel] = malloc_uncached(65536 * sizeof(int16_t));
    }
    int len = src->len < 65536 ? src->len : 65536;
    if (bits == 16)
    {
        for (i = 0; i < len; i++)
        {
            dbg16[dbg16_sel][i] = (int16_t)(((const int8_t *)src->mem)[i]) << 8;
        }
    }
    else
    {
        memcpy(dbg16[dbg16_sel], src->mem, len);
    }

    dbg_wave16[dbg16_sel] = *src;
    dbg_wave16[dbg16_sel].bits = bits;
    dbg_wave16[dbg16_sel].len = len;
    dbg_wave16[dbg16_sel].mem = dbg16[dbg16_sel];
    dbg_wave16[dbg16_sel].__uuid = 0;
    return &dbg_wave16[dbg16_sel];
}
#endif

// Native sample rate of the WAD's DMX PCM sound effects. The mixer output
// rate (see audio_init below) is independent of this - each channel is
// resampled to it automatically.
#define SFX_SAMPLERATE 11025

// Peak (out of the 8-bit sample range) every SFX is normalized to - see
// getsfx() for why it can't be any higher.
#define SFX_PEAK 100

static int changepitch;

// Legacy flag some other files (g_game.c's save/load, endoom.c's shutdown
// path) read or set directly as an "is music currently playing" signal from
// the days this file drove the AI interrupt itself. Nothing in this file
// branches on it any more - the mixer only advances when I_UpdateSound
// calls mixer_try_play(), never from an interrupt handler - but the extern
// linkage is kept so those call sites don't need to change.
int mus_playing = 0;

extern int snd_SfxVolume;
extern int snd_MusicVolume;

static int audio_shut_down = 0;

/**********************************************************************/
//
//  SFX
//
// One waveform_t per sound effect id, built once in I_InitSound from the
// WAD's own (already zone-allocated, already unsigned->signed converted)
// sample data - see getsfx(). waveform_t::mem points directly at that
// buffer, so the mixer reads it with no extra copy.
static waveform_t sfx_wave[NUMSFX];
static int sfx_bytelen[NUMSFX];

/**********************************************************************/
//
//  Music
//
// Only one song is ever registered/playing at a time in this engine (see
// S_ChangeMusic in s_sound.c: it always fully stops and unregisters the
// old song before registering the new one), so a single static wav64_t
// is enough - no need to track multiple handles.
static wav64_t music_wav;
static int     music_wav_open = 0;
static void    I_UpdateMusicEnd(void);
static float   music_gain = 1.0f;
static int     music_paused = 0;

// The mixer can only attenuate a channel relative to its source (both
// mixer_ch_set_vol and mixer_ch_set_gain cap at 1.0 = unchanged), never
// amplify it - so there's no way to make a quiet source louder, only a
// loud one quieter. render_music.py's ffmpeg step masters music close to
// full scale (deliberately, to use the format's full dynamic range), while
// the WAD's original DMX SFX samples are naturally quieter than that - so
// even at both sliders' max, a single music voice at "full" and a single
// SFX voice at "full" aren't comparably loud: music dominates and SFX
// reads as "quiet even at max" (see the iteration-2 bug report on this
// architecture). This caps how loud music is allowed to get, independent
// of and on top of the slider, to close that gap. Tune (and re-test
// against SFX at their own max) if the balance is still off.
//
// Lowered from an initial 0.5 - confirmed no clipping at that headroom
// (see mixer_set_vol in I_InitSound, raised alongside this), just still
// too much music relative to SFX (see the iteration-3 bug report).
//
// Lowered again, substantially, from 0.35: that value was tuned against
// render_music.py's old output, before it applied dynaudnorm (see its
// render_track comment) to flatten each track's own loudness swing over
// its full length. dynaudnorm's normalization raises a track's overall
// level a lot (measured ~2.5x average RMS on D_E2M2), so the same 0.35
// ceiling against the new, louder master read as "music too loud, SFX too
// quiet again" (see the iteration-9 bug report) even though nothing here
// changed - only the source got louder out from under this fraction.
//
// Back to 1.0 (no extra cut): every one of those "SFX too quiet" reports
// was really I_SetSfxVolPan capping SFX at 12% of their level. With that
// fixed, music no longer needs holding down to match them; the overall
// level is set once, for both, by mixer_set_vol in I_InitSound.
#define MUSIC_LEVEL_FRAC 1.0f

/**********************************************************************/
//
// Loads the sound data for sfxname from the WAD lump, and returns a ptr to
// the data in zone memory and its length in len. The DMX header (8 bytes:
// format, sample rate, sample count) stays at the front - Sfx_Start-era
// callers used to skip it manually; I_InitSound now does that once while
// building each sound's waveform_t.
//
void *getsfx (char *sfxname, int *len)
{
    uint8_t*    sfx;
    uint8_t*    cnvsfx;
    int         i;
    int         size;
    char        name[32];
    int         sfxlump;

    // Get the sound data from the WAD, allocate lump
    //  in zone memory.
    sprintf(name, "ds%s", sfxname);

    // Now, there is a severe problem with the
    //  sound handling, in it is not (yet/anymore)
    //  gamemode aware. That means, sounds from
    //  DOOM II will be requested even with DOOM
    //  shareware.
    // The sound list is wired into sounds.c,
    //  which sets the external variable.
    // I do not do runtime patches to that
    //  variable. Instead, we will use a
    //  default sound for replacement.
    if (W_CheckNumForName(name) == -1)
    {
        sfxlump = W_GetNumForName("dspistol");
    }
    else
    {
        sfxlump = W_GetNumForName(name);
    }

    size = W_LumpLength(sfxlump);
    sfx = (uint8_t*)W_CacheLumpNum(sfxlump, PU_STATIC);

    // Allocate from zone memory, with SFX_TAIL_PAD bytes of silence after
    // the sound for the resampler to read past its end.
    //
    // The sample data (right after the 8-byte DMX header) must start on a
    // 16-byte boundary and must not share a 16-byte D-cache line with
    // anything else. Zone blocks land at arbitrary 4-byte-aligned addresses,
    // and 8-bit samples read from there came out crackling ("clipping") on
    // every sound, at any volume. It was narrowed down in-game with the
    // AUDIO_DEBUG test modes: one voice playing the WAD data where it sat
    // crackled; the *same* 8-bit bytes copied to a 16-byte-aligned buffer
    // were clean, just like a 16-bit copy. The block is over-allocated by 32
    // so the data can be moved up to the next boundary and its tail padded
    // out to the next one, both inside this block.
    int blocksize = size + SFX_TAIL_PAD + 32;
    uint8_t *block = (uint8_t*)Z_Malloc(blocksize, PU_SOUND, 0);
    cnvsfx = (uint8_t*)((((uintptr_t)block + 8 + 15) & ~(uintptr_t)15) - 8);
    // Now copy and convert offset to signed.
    for (i = 0; i < size; i++)
    {
        cnvsfx[i] = sfx[i] ^ 0x80;
    }
    // Silence from the end of the sound to the end of the block: covers
    // SFX_TAIL_PAD and the rest of the sound's last cache line.
    memset(cnvsfx + size, 0, (block + blocksize) - (cnvsfx + size));

    // Remove the cached lump.
    Z_Free(sfx);

    // Per-sound normalization. The mixer can only attenuate (both
    // mixer_ch_set_vol and mixer_ch_set_gain cap at 1.0 = unchanged from
    // source), never amplify, so a quiet sound stays quiet no matter what
    // the SFX volume slider is set to - and the WAD's original DMX
    // samples vary hugely in how much of their own 8-bit range they
    // actually use: DSITEMUP peaks at 19/127 (15%) while DSPISTOL and
    // several others already reach 127/128 (measured directly from the
    // WAD - see the iteration-4 bug report, "SFX quiet even at max").
    // Scaling every sound by the same fixed amount either does nothing
    // for the already-loud ones or clips them if it's large enough to
    // help the quiet ones. Instead, scale *each sound* by however much
    // headroom *that* sound's own peak has, so everything ends up using
    // close to its full available range - already-loud sounds are left
    // alone, quiet ones get boosted as far as they safely can be.
    if (size > 8)
    {
        int peak = 0;
        for (i = 8; i < size; i++)
        {
            int a = (int8_t)cnvsfx[i];
            if (a < 0) a = -a;
            if (a > peak) peak = a;
        }
        // The target peak has to leave room for the mixer's resampler, not
        // for any volume: libdragon's RSP mixer upsamples every channel
        // (11025 Hz here -> 44100 Hz output) with 4-tap Catmull-Rom/Hermite
        // interpolation, which overshoots between samples by up to 25% on
        // sharp waveforms (e.g. taps -1,1,1,-1 interpolate to 1.25 at the
        // midpoint) - and it saturates that interpolated value to int16
        // *before* multiplying by the channel volume (see rsp_mixer.S's
        // MonoLoop: the vmadm chain into v_res, then vmacf by v_xvol). So a
        // sound peaking near full scale hard-clips inside the resampler no
        // matter how low the channel gain, poly scaling or master volume
        // are set - which is why lowering all of those (earlier iterations)
        // never removed the crackle, even for one sound with music off.
        // 8-bit samples are widened as s<<8, so |s| * 256 * 1.25 must stay
        // under 32767: |s| <= 102. SFX_PEAK sits just under that.
        //
        // Applied to every sound, both ways: quiet ones are boosted up to it
        // (DSITEMUP peaks at only 19/127 in the WAD) and already-loud ones
        // (DSPISTOL and others reach 127/128) are brought *down* to it -
        // skipping those, as before, left exactly the loudest sounds
        // clipping. Silence (peak == 0) is left alone.
        if (peak > 0 && peak != SFX_PEAK)
        {
            for (i = 8; i < size; i++)
            {
                int32_t s = ((int32_t)(int8_t)cnvsfx[i] * SFX_PEAK) / peak;
                if (s > 127) s = 127;
                else if (s < -128) s = -128;
                cnvsfx[i] = (uint8_t)(int8_t)s;
            }
        }

        // Fade the last SFX_TAIL_FADE samples to zero (see its comment).
        int fade = size - 8;
        if (fade > SFX_TAIL_FADE)
        {
            fade = SFX_TAIL_FADE;
        }
        for (i = 0; i < fade; i++)
        {
            int idx = size - fade + i;
            cnvsfx[idx] = (uint8_t)(int8_t)(((int)(int8_t)cnvsfx[idx] * (fade - 1 - i)) / fade);
        }
    }

    // The mixer's RSP DMA reads this buffer directly out of physical RAM,
    // bypassing the CPU cache entirely - without this writeback, the bytes
    // just written above could still be sitting in D-cache and never make
    // it to RAM before the RSP reads them.
    data_cache_hit_writeback(block, blocksize);

    // return length.
    *len = size;

    // Return normal (cached) pointer - the old raw interrupt-driven mixer
    // needed the manually-uncached alias to read this safely from CPU code;
    // the RSP-driven mixer doesn't go through the CPU cache at all, so it
    // just wants a normal pointer to the physical memory.
    return (void *)cnvsfx;
}

/**********************************************************************/
// SFX polyphony headroom. Several SFX summing at full scale clip the N64
// DAC, most audible when a cluster of enemies dies on the same frame.
// Scale each live SFX voice by roughly 1/sqrt(n): a lone sound stays at
// (almost) full level, several playing at once each give up some so the
// summed peak stays roughly bounded. Uses mixer_ch_set_gain, not
// mixer_ch_set_vol (I_SetSfxVolPan's own per-sound left/right level) - a
// separate multiplier layered on top of it (see mixer.h), so this doesn't
// fight that per-sound level the way overriding it directly would.
//
// This used to carry a flat 0.85 on top of the 1/sqrt(n) shape to fight
// clipping on a single voice - that clipping actually happened inside the
// mixer's resampler, before any gain is applied (see getsfx()), so no gain
// here could ever fix it. getsfx()'s SFX_PEAK is what does; the plain
// 1/sqrt(n) shape is back.
static const float sfx_poly_scale[SFX_VOICES + 1] =
{
    1.00f, 1.00f, 0.71f, 0.58f, 0.50f, 0.45f, 0.41f, 0.38f, 0.35f,
};
static int sfx_active_voices = -1;  // -1 forces the first I_SfxRebalance to apply

// fresh_ch: a mixer channel that has just started a new sound (or -1). It
// gets the new gain immediately; every voice that was already sounding
// ramps to it over SFX_DECLICK instead. mixer_ch_set_gain is an instant
// step, and applying one to every playing voice each time any sound started
// or ended was itself a click on all of them.
static void I_SfxRebalance(int fresh_ch)
{
    int v, n;
    float g;

    n = 0;
    for (v = 0; v < SFX_VOICES; v++)
    {
        if (I_VoiceActive(v))
        {
            n++;
        }
    }
    if (n == sfx_active_voices && fresh_ch < 0)
    {
        return;
    }
    sfx_active_voices = n;

    g = (n > 0) ? sfx_poly_scale[n] : 1.0f;
    for (v = 0; v < SFX_VOICES; v++)
    {
        if (I_VoiceActive(v))
        {
            int ch = I_VoiceCh(v);
            if (ch == fresh_ch)
            {
                mixer_ch_set_gain(ch, g);
            }
            else
            {
                mixer_ch_set_gain_ramp(ch, g, SFX_DECLICK, mixer_ramp_linear, 0);
            }
        }
    }
}

/**********************************************************************/
// ... update sound buffer and audio device at runtime...
// Called once per iteration of D_DoomLoop (see d_main.c) - this is what
// actually pumps audio; nothing else calls into the mixer.
void I_UpdateSound (void)
{
#if AUDIO_DEBUG
    I_AudioDebugTick();
#endif
    mixer_try_play();

    // Re-spread SFX headroom across however many are playing right now -
    // see I_SfxRebalance's comment.
    I_SfxRebalance(-1);

    // Mute (not stop) a play-once song as it ends - see its comment.
    I_UpdateMusicEnd();
}

void I_SubmitSound (void)
{
    // no-op: mixer_try_play() (see I_UpdateSound) already submits whatever
    // it mixes straight to the AI hardware via audio_write_begin/end.
}

/**********************************************************************/
// Init at program start...
void I_InitSound (void)
{
    int i;

    audio_init(44100, 4);
    mixer_init(NUM_MIXER_CHANNELS);
    // The music wav64 files are Opus-compressed (see the Makefile's
    // "--wav-compress 3" for the music step) - the decoder must be
    // registered before the first wav64_open() of one, or it hits an
    // assert ("compression level 3 not initialized") instead of failing
    // gracefully. N64DoomRPGPort's pd_sound.c does the same for the same
    // reason.
    wav64_init_compression(3);
    // Opus only natively supports a handful of fixed rates (8k/12k/16k/
    // 24k/48k) - audioconv64 resamples anything else (our 22050 Hz render)
    // up to 48000 Hz internally to encode it, so this channel actually
    // decodes at 48000 Hz even though the source file was rendered at
    // 22050. The mixer's default per-channel limit is the output rate
    // (44100), so without raising it here, playing that channel is an
    // assert ("frequency 48000 exceeds configured limit 44100"), not
    // silent clamping. N64DoomRPGPort's pd_sound.c raises this for every
    // channel for the same reason (see its SND_MAX_SRCFREQ).
    mixer_ch_set_limits(MUSIC_CH, 0, 48000.0f, 0);
    // Headroom: the mix hard-clips when several channels sum past full
    // scale. Every level before this was tuned while SFX were stuck at 12%
    // of their volume (see I_SetSfxVolPan), so the whole mix ran ~24 dB
    // under full scale. With that fixed, 0.5 puts a single SFX peak at
    // ~0.31 and music at ~0.23 of full scale with the menu's default
    // sliders (SFX 12, music 9) - SFX: SFX_PEAK 100/128 x 12/15 x 0.5,
    // music: render_music.py's 0.75 limiter x 9/15 x 0.5 - which leaves room
    // for a busy fight's several SFX plus music to sum without clipping.
    mixer_set_vol(0.5f);

    changepitch = M_CheckParm("-changepitch");

    // Initialize external data (all sounds) at start, keep static.
    for (i = 1; i < NUMSFX; i++)
    {
        // Alias? Example is the chaingun sound linked to pistol.
        if (!S_sfx[i].link)
        {
            // Load data from WAD file.
            S_sfx[i].data = getsfx(S_sfx[i].name, &sfx_bytelen[i]);
        }
        else
        {
            // Previously loaded already?
            S_sfx[i].data = S_sfx[i].link->data;
            // Pointer subtraction between two sfxinfo_t* already yields an
            // element index - see the chaingun/pistol bug this fixed
            // earlier in this project's history.
            sfx_bytelen[i] = sfx_bytelen[S_sfx[i].link - S_sfx];
        }

        // Skip the 8-byte DMX header (format, sample rate, sample count) -
        // the actual PCM data starts right after it.
        if (sfx_bytelen[i] > 8)
        {
            sfx_wave[i] = (waveform_t){
                .name = S_sfx[i].name,
                .bits = 8,
                .channels = 1,
                .frequency = SFX_SAMPLERATE,
                .len = sfx_bytelen[i] - 8,
                .mem = (const int8_t *)S_sfx[i].data + 8,
            };
        }
    }

    printf ("I_InitSound: Pre-cached all sound data.\n");

    numChannels = SFX_VOICES;

    // Finished initialization.
    printf("I_InitSound: Sound module ready.\n");
}

/**********************************************************************/
// ... shut down and relase at program termination.
void I_ShutdownSound (void)
{
    if (audio_shut_down)
    {
        return;
    }
    audio_shut_down = 1;

    if (music_wav_open)
    {
        wav64_close(&music_wav);
        music_wav_open = 0;
    }

    mixer_close();
    audio_close();
}

/**********************************************************************/
/**********************************************************************/
//
//  SFX I/O
//

/**********************************************************************/
// Initialize number of channels
void I_SetChannels (void)
{
}

/**********************************************************************/
// Get raw data lump index for sound descriptor.
int I_GetSfxLumpNum (sfxinfo_t *sfx)
{
    char namebuf[9];
    sprintf(namebuf, "ds%s", sfx->name);
    return W_GetNumForName(namebuf);
}

// Doom's classic stereo-separation curve (sep: 0-256, 128 = center),
// applied identically to left and right, just in float instead of the
// original fixed-point.
//
// vol is 0-15: s_sound.c works in snd_SfxVolume's own units (the menu's
// 0-15 slider - see S_Init/M_SfxVol, whose "*8" to vanilla's 0-127 scale is
// commented out), distance attenuation included. This used to divide by
// 127 as if it got vanilla's scale, so every SFX played at 15/127 = 12% of
// its level at most, even with the slider at max - the long-standing
// "SFX too quiet" that music's level kept being lowered to balance against.
static void I_SetSfxVolPan(int ch, int vol, int sep)
{
    float volf, lf, rf;
    int s;

    if (vol < 0) vol = 0;
    else if (vol > 15) vol = 15;
    volf = vol / 15.0f;

    s = sep;
    lf = (127.0f - ((127.0f * s * s) / 65536.0f)) / 127.0f;
    s -= 256;
    rf = (127.0f - ((127.0f * s * s) / 65536.0f)) / 127.0f;
    if (lf < 0.0f) lf = 0.0f;
    if (rf < 0.0f) rf = 0.0f;

    mixer_ch_set_vol(ch, volf * lf, volf * rf);
}

static float I_SfxFreq(int pitch)
{
    return changepitch ? (float)(2765 + (((pitch << 2) + pitch) << 5)) : (float)SFX_SAMPLERATE;
}

/**********************************************************************/
// Starts a sound in a particular sound channel.
int I_StartSound (
  int id,
  int cnum,
  int vol,
  int sep,
  int pitch,
  int priority )
{
    if (cnum < 0 || cnum >= SFX_VOICES || sfx_wave[id].len <= 0)
    {
        return cnum;
    }

    waveform_t *wave = &sfx_wave[id];
#if AUDIO_DEBUG
    if (dbg_mode != DBG_NORMAL)
    {
        // Everything through voice 0, so only one sound is ever audible.
        cnum = 0;
    }
    if (dbg_mode == DBG_ONE8A)
    {
        wave = I_AudioDebugCopy(id, 8);
    }
    else if (dbg_mode == DBG_ONE16)
    {
        wave = I_AudioDebugCopy(id, 16);
    }
#endif

    int ch = I_VoiceCh(cnum);

    // Fade out whatever this voice was playing instead of cutting it off
    // (see SFX_VOICES), and start the new sound on the voice's other mixer
    // channel. The faded one keeps running silently until its data ends.
    if (mixer_ch_playing(ch))
    {
        mixer_ch_set_vol_ramp(ch, 0.0f, 0.0f, SFX_DECLICK);
    }
    voice_sub[cnum] ^= 1;
    ch = I_VoiceCh(cnum);
    voice_live[cnum] = 1;

    mixer_ch_play(ch, wave);
    mixer_ch_set_freq(ch, I_SfxFreq(pitch));
    // Start from silence: mixer_ch_set_vol (in I_SetSfxVolPan) walks the
    // volume from its current value over MIXER_DECLICK_SAMPLES, so zeroing
    // it first turns that into a short fade-in - many of the WAD's sounds
    // start on a non-zero sample (DSRXPLOD on -43), which clicks otherwise.
    mixer_ch_set_vol_ramp(ch, 0.0f, 0.0f, 0);
    I_SetSfxVolPan(ch, vol, sep);
    // The new sound needs the current polyphony gain right away, not
    // whatever the channel was last left at; the others ramp to it.
    I_SfxRebalance(ch);
    return cnum;
}

/**********************************************************************/
// Stops a sound channel - with a short fade-out rather than a hard stop,
// which would click for the same reason a cut-off restart does (see
// SFX_VOICES). Doom sees the voice as stopped immediately.
void I_StopSound(int handle)
{
    if (handle >= 0 && handle < SFX_VOICES)
    {
        int ch = I_VoiceCh(handle);
        if (mixer_ch_playing(ch))
        {
            mixer_ch_set_vol_ramp(ch, 0.0f, 0.0f, SFX_DECLICK);
        }
        voice_live[handle] = 0;
    }
}

/**********************************************************************/
// Called by S_*() functions
//  to see if a channel is still playing.
// Returns 0 if no longer playing, 1 if playing.
int I_SoundIsPlaying(int handle)
{
    if (handle < 0 || handle >= SFX_VOICES)
    {
        return 0;
    }
    return I_VoiceActive(handle);
}

/**********************************************************************/
// Updates the volume, separation,
//  and pitch of a sound channel.
void
I_UpdateSoundParams
( int        handle,
  int        vol,
  int        sep,
  int        pitch )
{
    if (handle < 0 || handle >= SFX_VOICES || !I_VoiceActive(handle))
    {
        return;
    }
    int ch = I_VoiceCh(handle);
    mixer_ch_set_freq(ch, I_SfxFreq(pitch));
    I_SetSfxVolPan(ch, vol, sep);
}

/**********************************************************************/
/**********************************************************************/
//
// MUSIC API.
//
/**********************************************************************/
//
//  MUSIC I/O
//
void I_InitMusic(void)
{
    printf("I_InitMusic: ready (pre-rendered music, see tools/render_music.py).\n");
}

/**********************************************************************/
void I_ShutdownMusic(void)
{
    I_ShutdownSound();
}

// music_gain (0-1, from the slider) scaled by the fixed balance ceiling -
// see MUSIC_LEVEL_FRAC.
// A song started without looping (the title screen's) is really played
// looping, and muted just before it ends instead of being left to stop -
// see I_UpdateMusicEnd. music_once marks such a song, music_done that it
// has reached its end and stays silent from then on.
static int    music_once = 0;
static int    music_done = 0;
static double music_last_pos = 0;

static float I_MusicVol(void)
{
    if (music_done)
    {
        return 0.0f;
    }
    return music_gain * MUSIC_LEVEL_FRAC;
}

//
// Keeps the music channel running once a non-looping song ends: muted,
// not stopped. With the music channel stopped, and only SFX left in the
// mixer, SFX came out crackling ("clipping") - found on the title screen,
// the one place a song ever ends (its music doesn't loop; every level's
// does), and only once it had finished: while it played, or in game with
// the music slider at 0 (channel still running, just silent), SFX were
// clean. Same family of libdragon mixer bug as the unaligned 8-bit SFX
// one (see getsfx), so rather than depend on which mix of channels
// triggers it, the mixer is simply never left without the music channel.
//
// Muted a little before the end rather than on the wrap: the track's own
// last moments are already a fade to silence (render_music.py), so nothing
// is lost, and the restart of the loop is never heard. The wrap check is a
// backstop for a frame long enough to skip past that margin.
//
static void I_UpdateMusicEnd(void)
{
    if (!music_once || music_done || !music_wav_open || !mixer_ch_playing(MUSIC_CH))
    {
        return;
    }

    double pos = mixer_ch_get_pos(MUSIC_CH);
    double end = music_wav.wave.len - music_wav.wave.frequency * 0.25;

    if (pos >= end || pos < music_last_pos)
    {
        music_done = 1;
        mixer_ch_set_vol(MUSIC_CH, 0.0f, 0.0f);
    }
    music_last_pos = pos;
}

/**********************************************************************/
// Volume. Applied directly and immediately - unlike the CC7-based
// approach the previous (real-time synth) music system needed, nothing
// else ever touches this channel's volume, so there's no race to avoid
// and no need to defer or reapply this.
void I_SetMusicVolume(int volume)
{
    snd_MusicVolume = volume;

    if (volume < 0) volume = 0;
    else if (volume > 15) volume = 15;
    music_gain = volume / 15.0f;

    if (!music_paused)
    {
        mixer_ch_set_vol(MUSIC_CH, I_MusicVol(), I_MusicVol());
    }
}

/**********************************************************************/
// PAUSE game handling. A plain wav64 channel can just be muted and
// unmuted in place - no restart, no discontinuity, unlike the previous
// (mid64player-based) music system, whose only "stop" primitive rewound
// to the beginning.
void I_PauseSong(int handle)
{
    (void)handle;
    music_paused = 1;
    mixer_ch_set_vol(MUSIC_CH, 0.0f, 0.0f);
}

/**********************************************************************/
void I_ResumeSong(int handle)
{
    (void)handle;
    music_paused = 0;
    mixer_ch_set_vol(MUSIC_CH, I_MusicVol(), I_MusicVol());
}

/**********************************************************************/
// Registers a song handle to song data.
//
// `name` is the music entry's short name (musicinfo_t::name, e.g. "e1m1")
// as passed by s_sound.c - not raw MUS data. The WAD's own D_<NAME> lump
// is never read by this engine at runtime: tools/render_music.py renders
// it to rom:/mus/D_<NAME>.wav64 offline (see the big comment at the top of
// this file), and this just opens that.
//
// Only one song is ever registered at a time in practice (S_ChangeMusic
// always calls I_UnRegisterSong on the old one first), so the returned
// "handle" is just a fixed non-zero sentinel, not a real per-song object -
// see music_wav.
int I_RegisterSong(void *name)
{
    char dfspath[40];
    char rompath[48];
    char upper[16];
    int i, fd;
    const char *n = (const char *)name;

    if (!n)
    {
        return 0;
    }

    for (i = 0; n[i] && i < (int)sizeof(upper) - 1; i++)
    {
        upper[i] = toupper((unsigned char)n[i]);
    }
    upper[i] = '\0';

    snprintf(dfspath, sizeof(dfspath), "/mus/D_%s.wav64", upper);

    // wav64_open asserts on a missing file rather than failing gracefully,
    // so check first - a WAD whose music lump name this didn't handle
    // right should silence music, not crash the game.
    fd = dfs_open(dfspath);
    if (fd < 0)
    {
        printf("I_RegisterSong: no %s\n", dfspath);
        return 0;
    }
    dfs_close(fd);

    if (music_wav_open)
    {
        mixer_ch_stop(MUSIC_CH);
        wav64_close(&music_wav);
        music_wav_open = 0;
    }

    snprintf(rompath, sizeof(rompath), "rom:%s", dfspath);
    wav64_open(&music_wav, rompath);
    music_wav_open = 1;

    return 1;
}

/**********************************************************************/
// Called by anything that wishes to start music.
//  plays a song, and when the song is done,
//  starts playing it again in an endless loop.
// Horrible thing to do, considering.
void
I_PlaySong
( int        handle,
  int        looping )
{
    if (!handle || !music_wav_open)
    {
        return;
    }

    // Always looping on the mixer side, even for a song meant to play once:
    // that one is muted as it ends instead (see I_UpdateMusicEnd).
    wav64_set_loop(&music_wav, true);
    music_once = (looping == 0);
    music_done = 0;
    music_last_pos = 0;
    // Force the mixer to re-read the waveform config (loop length): it
    // skips that when the same wave object is replayed on the channel, so
    // a shared handle whose loop flag just changed would keep the
    // previous setting - matches N64DoomRPGPort's pd_sound.c, which
    // documents hitting exactly this.
    mixer_ch_stop(MUSIC_CH);
    mixer_ch_play(MUSIC_CH, &music_wav.wave);
    music_paused = 0;
    mixer_ch_set_vol(MUSIC_CH, I_MusicVol(), I_MusicVol());

    mus_playing = 1;
}

/**********************************************************************/
// Stops a song over 3 seconds.
void I_StopSong(int handle)
{
    (void)handle;
    mixer_ch_stop(MUSIC_CH);
    mus_playing = 0;
}

/**********************************************************************/
// See above (register), then think backwards
void I_UnRegisterSong(int handle)
{
    (void)handle;
    if (music_wav_open)
    {
        mixer_ch_stop(MUSIC_CH);
        wav64_close(&music_wav);
        music_wav_open = 0;
    }
}
