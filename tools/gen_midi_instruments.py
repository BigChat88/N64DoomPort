#!/usr/bin/env python3
"""
Converts a General MIDI SoundFont (.sf2) into the "MIDI_Instruments" binary
blob this port's MUS sequencer (src/i_sound.c) loads at runtime.

Background: the N64 has no MIDI hardware and this port doesn't emulate a
sound card. Instead it plays back a fixed set of raw 8-bit PCM samples - one
per General MIDI instrument, in the same style as the Gravis Ultrasound
patch sets DOS DOOM used for GUS output - and pitch-shifts them per note.
"MIDI_Instruments" is that sample set. Swapping in a soundfont built from
real Sound Canvas samples (e.g. an SC-55 soundfont) instead of a low-quality
patch set is how you change the game's music timbre without touching a
single note of the actual songs, which come from the WAD unchanged.

File layout produced (all multi-byte fields big-endian, matching the N64's
MIPS byte order - this file is read into memory as-is, with no byte-swap on
load):

    uint32_t pointers[182];   // byte offset of each instrument's block
                              // below, or 0 if that slot is unused
    <padding to 8-byte alignment>
    per instrument, 8-byte aligned:
        uint32_t rate;        // unused by the engine, kept for documentation
        uint32_t loop;        // loop start position, 16.16 fixed point;
                              // 0 = no loop (one-shot)
        uint32_t length;      // sample length, 16.16 fixed point
        uint16_t base;        // MIDI root note the sample plays natively at
        int8_t   sample[...]; // signed 8-bit PCM at 11025Hz, mono

Instrument slot layout (182 = 128 melodic + 54 percussion):
    0..127   GM melodic program number
    128..181 percussion note 28..81 (i_sound.c does "inst = note + 100"
             for the percussion MIDI channel, so slot 128 is note 28 and
             slot 181 is note 81). Percussion notes 0..27 alias slots
             100..127 - i.e. whichever melodic program happens to sit
             there - because the original 182-slot table has no room for
             them separately; real DOOM tracks don't use notes below 28
             so this is never audible in practice.

Percussion samples always play at their recorded pitch (the engine sets
step=0x10000 for channel 15, no pitch-shift), so their `base` field is
cosmetic; melodic samples are pitch-shifted relative to `base` using a
semitone step table, so `base` must be the sample's true recorded root key.

Usage:
    python gen_midi_instruments.py path/to/soundfont.sf2 [-o OUTPUT]

Requires: numpy, sf2utils (`pip install numpy sf2utils`). On Python 3.13+,
sf2utils needs the `audioop` module removed from the standard library -
install the backport with `pip install audioop-lts`.
"""

import argparse
import struct
import sys

import numpy as np
from sf2utils.sf2parse import Sf2File

NUM_INSTRUMENTS = 182
NUM_MELODIC = 128
TARGET_RATE = 11025
MAX_ONESHOT_SECONDS = 3.0
HEADER_SIZE = 14  # rate(4) + loop(4) + length(4) + base(2)
INSTRUMENT_ALIGN = 8

# Percussion instrument slots are 128..181 (54 slots = NUM_INSTRUMENTS -
# NUM_MELODIC). Via the engine's "inst = note + 100" mapping that's notes
# 28..81 - see the module docstring for why notes 0..27 are skipped.
PERCUSSION_NOTES = range(28, 82)

# Probe key order used to pick a representative sample for a melodic
# program: prefer the zone covering middle C, then fan outward.
MELODIC_PROBE_KEYS = (60, 48, 72, 36, 84, 24, 96, 12, 108, 0, 127)


def find_zone(preset, key):
    """The instrument-level Sf2Bag that sounds for `key` in `preset`, or None."""
    for pbag in preset.key_bags(key):
        inst = pbag.instrument
        if inst is None:
            continue
        for ibag in inst.bags:
            if ibag.sample is None:
                continue
            key_range = ibag.key_range
            if key_range is None or (key_range[0] <= key <= key_range[1]):
                return ibag
    return None


def extract_pcm(zone):
    """Resamples a zone's sample to TARGET_RATE and packs it for our format."""
    sample = zone.sample

    loop_on = getattr(zone, 'sample_loop', False) or False

    raw = sample.raw_sample_data  # little-endian 16-bit mono PCM bytes
    pcm16 = np.frombuffer(raw, dtype='<i2').astype(np.float64)
    n_src = len(pcm16)
    if n_src == 0:
        return None

    ratio = TARGET_RATE / float(sample.sample_rate)
    n_dst = max(1, int(round(n_src * ratio)))
    resampled = np.interp(np.linspace(0, n_src - 1, n_dst), np.arange(n_src), pcm16)

    loop_start_dst = 0
    if loop_on:
        loop_start_dst = int(round(zone.cooked_loop_start * ratio))
        loop_end_dst = int(round(zone.cooked_loop_end * ratio))
        loop_end_dst = max(loop_start_dst + 1, min(loop_end_dst, n_dst))
        # Our format has no separate loop-end: the sample always loops back
        # to loop_start once playback reaches the physical end of the data,
        # so truncate any release tail past the loop's end here.
        resampled = resampled[:loop_end_dst]
    else:
        max_len = int(TARGET_RATE * MAX_ONESHOT_SECONDS)
        resampled = resampled[:max_len]

    # length/loop are packed as 16.16 fixed point (a 32-bit word - see
    # pack_slots), so the integer sample count must fit in 16 bits no
    # matter which path above produced it. Most soundfonts never get close,
    # but a few have surprisingly long looped samples (multi-second pads)
    # that would otherwise silently overflow the pack and corrupt the file.
    MAX_SAMPLES = 0xFFFF
    if len(resampled) > MAX_SAMPLES:
        resampled = resampled[:MAX_SAMPLES]
    loop_start_dst = min(loop_start_dst, max(0, len(resampled) - 1))

    pcm8 = np.clip(np.round(resampled / 256.0), -128, 127).astype(np.int8)

    root = getattr(zone, 'base_note', None)
    if root is None:
        root = sample.original_pitch

    return {
        'data': pcm8.tobytes(),
        'length': len(pcm8),
        'loop_start': loop_start_dst,
        'base': int(root) & 0xFFFF,
    }


def build_slots(sf2):
    presets = [p for p in sf2.presets if p.name != 'EOP']
    bank0 = {p.preset: p for p in presets if p.bank == 0}
    bank128 = {p.preset: p for p in presets if p.bank == 128}
    perc_preset = bank128.get(0)
    if perc_preset is None:
        sys.exit("error: soundfont has no percussion preset (bank 128, preset 0)")

    slots = [None] * NUM_INSTRUMENTS

    melodic_ok = 0
    for program in range(NUM_MELODIC):
        preset = bank0.get(program)
        zone = None
        if preset is not None:
            for probe in MELODIC_PROBE_KEYS:
                zone = find_zone(preset, probe)
                if zone is not None:
                    break
        if zone is None:
            print("  warning: no usable zone for melodic program %d" % program, file=sys.stderr)
            continue
        info = extract_pcm(zone)
        if info is None:
            print("  warning: empty sample for melodic program %d" % program, file=sys.stderr)
            continue
        slots[program] = info
        melodic_ok += 1

    perc_ok = 0
    for note in PERCUSSION_NOTES:
        zone = find_zone(perc_preset, note)
        if zone is None:
            print("  warning: no usable zone for percussion note %d" % note, file=sys.stderr)
            continue
        info = extract_pcm(zone)
        if info is None:
            print("  warning: empty sample for percussion note %d" % note, file=sys.stderr)
            continue
        slots[note + 100] = info
        perc_ok += 1

    print("melodic instruments resolved: %d/%d" % (melodic_ok, NUM_MELODIC))
    print("percussion instruments resolved: %d/%d" % (perc_ok, len(PERCUSSION_NOTES)))

    return slots


def pack_slots(slots):
    ptr_table_bytes = 4 * NUM_INSTRUMENTS
    data_start = ((ptr_table_bytes + INSTRUMENT_ALIGN - 1) // INSTRUMENT_ALIGN) * INSTRUMENT_ALIGN

    pointer_table = [0] * NUM_INSTRUMENTS
    blob = bytearray(data_start)

    for i, info in enumerate(slots):
        if info is None:
            continue

        while (len(blob) % INSTRUMENT_ALIGN) != 0:
            blob += b'\x00'
        pointer_table[i] = len(blob)

        loop_fixed = info['loop_start'] << 16
        length_fixed = info['length'] << 16
        blob += struct.pack('>IIIH', TARGET_RATE, loop_fixed, length_fixed, info['base'])
        blob += info['data']

    blob[0:ptr_table_bytes] = struct.pack('>%dI' % NUM_INSTRUMENTS, *pointer_table)
    return bytes(blob)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('soundfont', help="path to a General MIDI .sf2 file")
    parser.add_argument('-o', '--output', default='MIDI_Instruments',
                         help="output path (default: ./MIDI_Instruments)")
    args = parser.parse_args()

    with open(args.soundfont, 'rb') as fh:
        # Sf2File lazily reads sample data by seeking back into this file
        # object for the whole run, so it must stay open past this point.
        sf2 = Sf2File(fh)
        slots = build_slots(sf2)
        out = pack_slots(slots)

    with open(args.output, 'wb') as fh:
        fh.write(out)

    print("wrote %s (%.2f MB)" % (args.output, len(out) / 1024 / 1024))


if __name__ == '__main__':
    main()
