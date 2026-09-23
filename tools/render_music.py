#!/usr/bin/env python3
"""Renders every D_* MUS track in an IWAD to a mastered, loop-safe WAV file.

Background: this project first tried playing DOOM's music by synthesizing
it live on the N64, first with a hand-rolled MUS interpreter and a crude
sample bank (the original engine), then with libdragon's real SoundFont
synthesizer (sf64_synth + mid64player) playing the WAD's MUS data converted
to MIDI. Both real-time approaches ran into the same family of problems:
a shared, fragile "who owns the volume" state (the song's own per-channel
mix data - preserved faithfully by mus2mid.py's conversion - kept fighting
whatever tried to impose a master volume on top of it, including across
every music loop restart, since looping just re-runs the whole MIDI
sequence, mix data included) and clipping headroom that had to account for
worst-case simultaneous polyphony (measured up to 15 notes at once) instead
of a single, fixed, well-understood source.

This sidesteps all of that the way the sibling N64DoomRPGPort project
does: render the music to audio *once*, offline, with the same real
FluidSynth + SoundFont (so the audio quality is identical - nothing here
changes what actually plays, only when), then play that back as a single,
plain, already-mastered PCM stream. A single wav64 channel has a simple,
well-understood volume control (mixer_ch_set_vol, with nothing else ever
touching it) and a simple, well-understood loop (wav64's own native sample
loop - just repeats the same finished audio, no MIDI events re-firing).
Clipping-safety is handled once at render time (ffmpeg's alimiter, exactly
like N64DoomRPGPort's own render step), not continuously at runtime against
however many voices happen to be sounding.

Requires `fluidsynth` and `ffmpeg` on PATH (this runs on the host - neither
is present in the libdragon Docker toolchain image, which is why this is a
separate step from `make`, not folded into the Makefile like the MID64
conversion it replaces).

Usage:
    python render_music.py path/to/DOOM2.WAD -o music_wav/DOOM2
    python render_music.py path/to/DOOM2.WAD -o music_wav/DOOM2 --soundfont "tools/soundfont/SC-55 Deemster [GZDoom].sf2"
"""

import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mus2mid

RENDER_RATE = 44100      # fluidsynth's own render rate
# audioconv64's Opus encoder only natively supports a handful of fixed
# rates (8k/12k/16k/24k/48k) and silently resamples anything else up to
# 48000 Hz internally before encoding - which turned out to have a real bug
# around the loop point specifically (an Opus decode failure the moment a
# track first looped - see the iteration-6 bug report), that
# N64DoomRPGPort's own render step (see its ost_setup.sh) avoids by
# resampling to 48000 Hz itself first, the same way this now does, so
# audioconv64 never has to. Confirmed against N64DoomRPGPort's exact
# audioconv64 invocation (--wav-compress 3 --wav-mono --wav-loop true - no
# --wav-seek, which doesn't apply anyway: wav64_opus.c's own loop-restart
# path seeks position 0 directly, not through a seekpoint).
OUTPUT_RATE = 48000
# Safety net only, not a real trim - see trim_trailing_silence() for what
# actually shapes the loop point. This used to be 120s, copied from
# N64DoomRPGPort's own MUS_MAXLEN without checking whether it fit *this*
# game's music: DOOM's tracks are full-length compositions, not short RPG
# jingles, and several genuinely run past two minutes before their own
# natural loop point (measured directly from the MUS event ticks: DOOM.WAD's
# longest, D_E1M3, is 272s; DOOM2.WAD's longest, D_THE_DA, is ~419s). At
# 120s, every one of those was getting hard-cut mid-note, then immediately
# jumping back to its own start - which is what was actually behind the
# "loop takes forever to come back" report: most tracks never even reached
# their real loop point, and the ones that did still had several seconds of
# receding reverb tail after the last note before truly going silent (see
# trim_trailing_silence). Set generously past the longest known track so
# it's purely a guard against a runaway/corrupt MIDI, not a real limit.
MAX_LEN_SEC = 600
FADE_SEC = 0.25          # fade-out length if the MAX_LEN_SEC safety net ever actually fires


def find_tool(name):
    path = shutil.which(name)
    if not path:
        raise SystemExit(f"'{name}' not found on PATH - required on the host to render music")
    return path


# audioconv64 (see its conv_wav64.cpp) encodes Opus in fixed frames of
# sampleRate/50 samples (20 ms - 960 samples at our 48000 Hz output). A
# wav64 loop always covers the *whole* file (that's all render_music.py
# ever produces), so the loop point is the file's own end - and
# waveform_opus_read (wav64_opus.c) decodes in whole frames at a time,
# with no handling for the file ending mid-frame: it was hitting "opus
# decode error: invalid argument" the moment a track first looped (see the
# iteration-6/7 bug reports), because ffmpeg's fade/trim almost never
# lands on an exact multiple of 960 samples on its own. Truncating the
# rendered WAV to a whole number of Opus frames here removes that partial
# frame before audioconv64 ever sees it.
OPUS_FRAME_SAMPLES = OUTPUT_RATE // 50


def _parse_wav_chunks(data, wav_path):
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise SystemExit(f"{wav_path}: not a RIFF/WAVE file")

    pos = 12
    fmt_channels = fmt_bits = None
    data_off = data_len = None
    while pos + 8 <= len(data):
        chunk_id = bytes(data[pos:pos + 4])
        chunk_len = int.from_bytes(data[pos + 4:pos + 8], "little")
        body_off = pos + 8
        if chunk_id == b"fmt ":
            fmt_channels = int.from_bytes(data[body_off + 2:body_off + 4], "little")
            fmt_bits = int.from_bytes(data[body_off + 14:body_off + 16], "little")
        elif chunk_id == b"data":
            data_off = body_off
            data_len = chunk_len
        pos = body_off + chunk_len + (chunk_len & 1)  # chunks are word-aligned

    if data_off is None or fmt_channels is None or fmt_bits is None:
        raise SystemExit(f"{wav_path}: missing fmt/data chunk")

    return data_off, data_len, fmt_channels, fmt_bits


def _truncate_wav_data(data, data_off, new_data_len):
    """Shrinks the data chunk in place (bytearray) to new_data_len bytes and
    fixes up both its own size field and the overall RIFF size field -
    shared by trim_trailing_silence and truncate_to_frame_boundary below."""
    del data[data_off + new_data_len:]
    data[data_off - 4:data_off] = new_data_len.to_bytes(4, "little")
    riff_size = len(data) - 8
    data[4:8] = riff_size.to_bytes(4, "little")


# fluidsynth's render includes each track's natural reverb/release tail
# after its last real note - inaudible on the N64's small speaker within a
# second or so, but not digital silence, so it was riding along for several
# more seconds (measured 3-6s on most tracks, see the iteration-8 bug
# report) before the file actually ended and the wav64 loop wrapped back to
# the start. That whole quiet tail read as a dead-air pause at every loop
# restart. Scans backward from the very end of the rendered PCM (not a
# forward scan for "any quiet spot", which could misfire on a legitimately
# quiet passage in the middle of a track) for the last sample that's still
# clearly audible, trims shortly after it, and fades out over that short
# remainder so the cut itself isn't audible as a click.
SILENCE_THRESHOLD = 1000     # ~3% of full scale (16-bit) - see iteration-8 measurements
SILENCE_PAD_SEC = 0.15       # kept audible after the last loud sample, before fading
SILENCE_FADE_SEC = 0.15


def trim_trailing_silence(wav_path):
    with open(wav_path, "rb") as fh:
        data = bytearray(fh.read())

    data_off, data_len, fmt_channels, fmt_bits = _parse_wav_chunks(data, wav_path)
    if fmt_bits != 16:
        raise SystemExit(f"{wav_path}: expected 16-bit PCM, got {fmt_bits}-bit")

    fmt_pos = data.find(b"fmt ")
    sample_rate = struct.unpack_from("<I", data, fmt_pos + 8 + 4)[0]

    n_samples = data_len // 2
    samples = struct.unpack_from(f"<{n_samples}h", data, data_off)

    last_loud = -1
    for i in range(n_samples - 1, -1, -1):
        if abs(samples[i]) > SILENCE_THRESHOLD:
            last_loud = i
            break

    if last_loud < 0:
        return  # entirely silent (or below threshold) - nothing sensible to trim

    pad_samples = int(SILENCE_PAD_SEC * sample_rate) * fmt_channels
    fade_samples = int(SILENCE_FADE_SEC * sample_rate) * fmt_channels
    cut_at = min(n_samples, last_loud + 1 + pad_samples)
    cut_at -= cut_at % fmt_channels  # keep stereo frames intact if ever non-mono
    if cut_at >= n_samples:
        return  # last loud sample is already right at (or past) the end

    faded = list(samples[:cut_at])
    fade_start = max(0, cut_at - fade_samples)
    span = cut_at - fade_start
    for i in range(fade_start, cut_at):
        idx = i - fade_start
        gain = 1.0 - (idx / span)
        faded[i] = int(faded[i] * gain)

    new_data_len = cut_at * 2
    data[data_off:data_off + new_data_len] = struct.pack(f"<{cut_at}h", *faded)
    _truncate_wav_data(data, data_off, new_data_len)

    with open(wav_path, "wb") as fh:
        fh.write(data)


def truncate_to_frame_boundary(wav_path):
    with open(wav_path, "rb") as fh:
        data = bytearray(fh.read())

    data_off, data_len, fmt_channels, fmt_bits = _parse_wav_chunks(data, wav_path)

    bytes_per_sample_frame = fmt_channels * (fmt_bits // 8)
    frame_bytes = OPUS_FRAME_SAMPLES * bytes_per_sample_frame
    aligned_len = (data_len // frame_bytes) * frame_bytes
    if aligned_len == data_len:
        return

    # Trailing bytes beyond the new data chunk (there shouldn't be any real
    # chunks after "data", but just in case) are dropped too.
    _truncate_wav_data(data, data_off, aligned_len)

    with open(wav_path, "wb") as fh:
        fh.write(data)


def render_track(fluidsynth, ffmpeg, soundfont, midi_path, out_wav, gain):
    with tempfile.TemporaryDirectory() as tmp:
        raw_wav = os.path.join(tmp, "raw.wav")
        subprocess.run(
            [fluidsynth, "-nli", "-q", "-g", str(gain), "-r", str(RENDER_RATE),
             "-O", "s16", "-T", "wav", "-F", raw_wav, soundfont, midi_path],
            check=True,
        )
        fade_start = MAX_LEN_SEC - FADE_SEC
        # DOOM tracks build in intensity over their full length (measured
        # directly: D_E2M2's RMS loudness roughly quadruples from its quiet
        # intro to its sustained climax - see the iteration-9 bug report).
        # With MAX_LEN_SEC previously cutting most tracks off at 120s (see
        # its own comment), that whole swing rarely mattered - most tracks
        # never got far past their intro before looping back to it. Now
        # that they play their real, full length, players spend most of
        # their time in each track's loud, sustained back half - which
        # made the *same* MUSIC_LEVEL_FRAC ceiling (i_sound.c) that used to
        # read as balanced now read as "music too loud", and let those
        # climax sections push closer to the alimiter's ceiling, leaving
        # less room for simultaneous SFX before summing past full scale.
        # dynaudnorm evens out that swing at render time - the quiet intro
        # comes up, the climax gets gently reined in - so the whole track
        # sits close to one consistent loudness for the mixer to balance
        # against, instead of swinging several-fold within the same song.
        #
        # The limiter ceiling is well under full scale: the N64 mixer
        # resamples this channel with Hermite interpolation that overshoots
        # up to 25% between samples and saturates to int16 *before* the
        # channel volume is applied (see getsfx() in src/i_sound.c), so
        # anything above 1/1.25 = 0.8 could hard-clip on playback however
        # low the music volume is set. The ceiling sits a bit under that,
        # at 0.75, because alimiter itself lets peaks land ~3% over its
        # limit (measured 0.825 with limit=0.8).
        af = (f"dynaudnorm=f=500:g=15:p=0.9:m=6,"
              f"afade=t=out:st={fade_start}:d={FADE_SEC},alimiter=limit=0.75:level=disabled")
        subprocess.run(
            [ffmpeg, "-nostdin", "-hide_banner", "-loglevel", "error", "-y",
             "-i", raw_wav, "-t", str(MAX_LEN_SEC), "-ac", "1", "-ar", str(OUTPUT_RATE),
             "-af", af,
             # audioconv64's WAV reader only expects fmt/data chunks and
             # crashes (SIGSEGV) on the LIST/INFO metadata chunk ffmpeg
             # writes by default (its own encoder name/version) - strip it.
             "-map_metadata", "-1", "-fflags", "+bitexact", "-flags:a", "+bitexact",
             out_wav],
            check=True,
        )
        trim_trailing_silence(out_wav)
        truncate_to_frame_boundary(out_wav)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("wad", help="path to an IWAD (DOOM.WAD, DOOM2.WAD, ...)")
    ap.add_argument("-o", "--output", required=True, help="directory to write <LUMPNAME>.wav files into")
    ap.add_argument("--soundfont", default=None,
                     help="path to a .sf2 (default: tools/soundfont/SC-55 Deemster [GZDoom].sf2)")
    ap.add_argument("--gain", type=float, default=0.6, help="fluidsynth -g (default: 0.6)")
    ap.add_argument("lumps", nargs="*", help="specific lump names to render (default: every D_* lump)")
    args = ap.parse_args()

    fluidsynth = find_tool("fluidsynth")
    ffmpeg = find_tool("ffmpeg")

    soundfont = args.soundfont
    if soundfont is None:
        here = os.path.dirname(os.path.abspath(__file__))
        soundfont = os.path.join(here, "soundfont", "SC-55 Deemster [GZDoom].sf2")
    if not os.path.isfile(soundfont):
        raise SystemExit(f"soundfont not found: {soundfont}")

    with open(args.wad, "rb") as fh:
        wad_data = fh.read()
    lumps = mus2mid.load_wad_directory(wad_data)

    lumps_by_name = {}
    for name, filepos, size in lumps:
        lumps_by_name[name.upper()] = (name, filepos, size)

    wanted = set(n.upper() for n in args.lumps) if args.lumps else None
    os.makedirs(args.output, exist_ok=True)

    with tempfile.TemporaryDirectory() as tmp:
        count = 0
        for name, filepos, size in lumps_by_name.values():
            if wanted is not None:
                if name.upper() not in wanted:
                    continue
            elif not name.upper().startswith("D_"):
                continue

            mus_data = wad_data[filepos:filepos + size]
            if mus_data[:4] != mus2mid.MUS_MAGIC:
                if wanted is not None:
                    print(f"skip {name}: not a MUS lump")
                continue

            midi_bytes = mus2mid.mus_to_midi_bytes(mus_data, name)
            midi_path = os.path.join(tmp, f"{name}.mid")
            with open(midi_path, "wb") as fh:
                fh.write(midi_bytes)

            out_wav = os.path.join(args.output, f"{name}.wav")
            render_track(fluidsynth, ffmpeg, soundfont, midi_path, out_wav, args.gain)
            print(f"{name}  -> {out_wav}")
            count += 1

    print(f"rendered {count} track(s) -> {args.output}")


if __name__ == "__main__":
    main()
