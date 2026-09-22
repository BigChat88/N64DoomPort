#!/usr/bin/env python3
"""Converts DOOM's WAD MUS-format music lumps to Standard MIDI files.

Background: this project's music is pre-rendered offline to audio (see
render_music.py and the big comment at the top of src/i_sound.c for why),
through FluidSynth - which plays Standard MIDI, not MUS. This is the first
step of that pipeline: turning the WAD's MUS lumps (D_* - D_E1M1, D_RUNNIN,
etc.) into actual .mid files that FluidSynth (or any other MIDI tool -
render_music.py imports this module directly, but the .mid output is also
just an ordinary Standard MIDI file usable anywhere) can read.

The event-format knowledge here (event types, the 140-tick/second time
base, the channel 9/15 swap) isn't guessed - it's read directly from this
project's own MUS player in src/i_sound.c (the D_DoomLoop code path that
walks score_ptr and dispatches on `(event >> 4) & 7`), which is proven
correct against these exact IWADs. See in particular the "1=140Hz,
2=70Hz, 4=35Hz" comment there and BEATS_PER_PASS=4 at a 35 Hz sound-update
rate: 4 MUS ticks per call * 35 calls/sec = 140 MUS ticks/sec. Given that,
PPQN=140 with a tempo of 1,000,000 microseconds/quarter-note (60 BPM)
makes 1 MUS tick equal exactly 1 MIDI tick, so MUS's own delta-time bytes
can be re-encoded as MIDI delta-times with no rescaling.

MUS always plays percussion on channel 15 (see the `channel != 15` check
in i_sound.c's play-note handler); General MIDI's percussion channel is
9 (0-indexed, "channel 10" in 1-indexed MIDI documentation). A source
MUS channel 9 must not collide with that reserved GM percussion channel,
so - as every other mus2mid implementation does - channels 9 and 15 are
swapped when writing the MIDI file.

Usage:
    python mus2mid.py path/to/DOOM2.WAD -o out_dir/            # every D_* lump
    python mus2mid.py path/to/DOOM2.WAD -o out_dir/ D_RUNNIN    # one lump
"""

import argparse
import struct
import os

MUS_MAGIC = b"MUS\x1a"

# MUS controller number -> MIDI CC number (MUS controller 0 is a program
# change, handled separately, not a CC).
CONTROLLER_MAP = {
    1: 0,    # bank select
    2: 1,    # modulation wheel
    3: 7,    # volume
    4: 10,   # pan
    5: 11,   # expression
    6: 91,   # reverb depth
    7: 93,   # chorus depth
    8: 64,   # sustain pedal
    9: 67,   # soft pedal
}

# MUS "system event" data byte -> MIDI CC number.
SYSTEM_CC = {
    10: 120,  # all sounds off
    11: 123,  # all notes off
    12: 126,  # mono mode on
    13: 127,  # poly mode on
    14: 121,  # reset all controllers
}

PPQN = 140
TEMPO_US_PER_QUARTER = 1000000  # 60 BPM; see module docstring


def remap_channel(ch):
    """Swap MUS's percussion channel (15) with GM's (9) - see module docstring."""
    if ch == 9:
        return 15
    if ch == 15:
        return 9
    return ch


def write_vlq(value):
    if value < 0:
        raise ValueError("negative delta time")
    buf = [value & 0x7F]
    value >>= 7
    while value:
        buf.append((value & 0x7F) | 0x80)
        value >>= 7
    return bytes(reversed(buf))


def mus_to_midi_bytes(mus_data, name="<mus>"):
    if mus_data[:4] != MUS_MAGIC:
        raise ValueError(f"{name}: not a MUS lump (bad magic)")

    score_len, score_start = struct.unpack_from("<HH", mus_data, 4)
    n = len(mus_data)

    def read_u8(pos):
        if pos >= n:
            raise ValueError(f"{name}: read past end of lump at offset {pos}")
        return mus_data[pos]

    pos = score_start
    last_velocity = [100] * 16  # MUS "play note" without a volume byte
                                # reuses the channel's last known volume;
                                # 100 is just a sane fallback before any
                                # volume has been set.

    # (delta_ticks, event_bytes) pairs, one per MIDI event (not per MUS
    # group - MIDI needs a delta-time, even a zero one, before *every*
    # event; concatenating a whole group's events under a single leading
    # delta desyncs a standard MIDI reader after enough events pass, since
    # it starts reading each event's first data byte as if it were the
    # next event's delta-time).
    events = []
    # Ticks accumulated since the last emitted event. A MUS group whose
    # events all mapped to nothing (unrecognized system/controller number)
    # would otherwise lose its trailing time delta; carrying it forward
    # here instead means it's simply added to whenever the next real event
    # does appear.
    pending_ticks = 0

    def emit(event_bytes):
        nonlocal pending_ticks
        events.append((pending_ticks, event_bytes))
        pending_ticks = 0

    while True:
        while True:
            event = read_u8(pos); pos += 1
            last = bool(event & 0x80)
            etype = (event >> 4) & 7
            mus_ch = event & 15
            midi_ch = remap_channel(mus_ch)

            if etype == 0:  # release note
                note = read_u8(pos) & 0x7F; pos += 1
                emit(bytes((0x80 | midi_ch, note, 0)))

            elif etype == 1:  # play note
                note = read_u8(pos); pos += 1
                if note & 0x80:
                    note &= 0x7F
                    vel = read_u8(pos); pos += 1
                    if vel > 127:
                        vel = 127
                    last_velocity[mus_ch] = vel
                vel = last_velocity[mus_ch]
                emit(bytes((0x90 | midi_ch, note, vel)))

            elif etype == 2:  # pitch bend
                mus_pitch = read_u8(pos); pos += 1
                bend = mus_pitch * 64
                if bend > 16383:
                    bend = 16383
                emit(bytes((0xE0 | midi_ch, bend & 0x7F, (bend >> 7) & 0x7F)))

            elif etype == 3:  # system event
                data = read_u8(pos); pos += 1
                cc = SYSTEM_CC.get(data)
                if cc is not None:
                    emit(bytes((0xB0 | midi_ch, cc, 0)))

            elif etype == 4:  # controller change
                ctrl = read_u8(pos) & 0x7F; pos += 1
                value = read_u8(pos); pos += 1
                if value > 127:
                    value = 127
                if ctrl == 0:
                    emit(bytes((0xC0 | midi_ch, value)))
                else:
                    cc = CONTROLLER_MAP.get(ctrl)
                    if cc is not None:
                        emit(bytes((0xB0 | midi_ch, cc, value)))

            elif etype == 5:
                pass  # unused/reserved in the MUS format - no data bytes

            elif etype == 6:  # end of score
                return build_midi_file(events)

            else:  # 7: unused
                pass

            if last:
                break

        # trailing variable-length time delta for this group (MSB-first
        # continuation, matching i_sound.c's own reader exactly)
        time_delta = 0
        b = read_u8(pos); pos += 1
        while b & 0x80:
            time_delta |= (b & 0x7F)
            time_delta <<= 7
            b = read_u8(pos); pos += 1
        time_delta |= b

        pending_ticks += time_delta


def build_midi_file(events):
    track = bytearray()
    # tempo meta event at time 0
    track += write_vlq(0)
    track += bytes((0xFF, 0x51, 0x03)) + TEMPO_US_PER_QUARTER.to_bytes(3, "big")

    for delta, data in events:
        track += write_vlq(delta)
        track += data

    track += write_vlq(0) + bytes((0xFF, 0x2F, 0x00))  # end of track

    header = b"MThd" + (6).to_bytes(4, "big") + (0).to_bytes(2, "big") \
        + (1).to_bytes(2, "big") + PPQN.to_bytes(2, "big")
    return header + b"MTrk" + len(track).to_bytes(4, "big") + bytes(track)


def load_wad_directory(wad_data):
    ident, numlumps, infotableofs = struct.unpack("<4sii", wad_data[:12])
    if ident not in (b"IWAD", b"PWAD"):
        raise SystemExit("not a WAD file")
    lumps = []
    for i in range(numlumps):
        off = infotableofs + i * 16
        filepos, size, name = struct.unpack("<ii8s", wad_data[off:off + 16])
        name = name.split(b"\x00")[0].decode("ascii", errors="replace")
        lumps.append((name, filepos, size))
    return lumps


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("wad", help="path to an IWAD (DOOM.WAD, DOOM2.WAD, ...)")
    ap.add_argument("-o", "--output", required=True, help="directory to write <LUMPNAME>.mid files into")
    ap.add_argument("lumps", nargs="*", help="specific lump names to convert (default: every D_* lump)")
    args = ap.parse_args()

    with open(args.wad, "rb") as fh:
        wad_data = fh.read()
    lumps = load_wad_directory(wad_data)

    # IWADs can list the same music lump name twice (DOOM.WAD does, for
    # several D_* tracks); the engine's own lookup (W_CheckNumForName,
    # via its hashtable) resolves that to whichever entry was inserted
    # last, matching every other Doom engine's "later lump overrides
    # earlier one of the same name" convention. A plain dict keyed by
    # name reproduces that: each duplicate simply overwrites the
    # previous entry as the directory is walked in file order.
    lumps_by_name = {}
    for name, filepos, size in lumps:
        lumps_by_name[name.upper()] = (name, filepos, size)

    wanted = set(n.upper() for n in args.lumps) if args.lumps else None
    os.makedirs(args.output, exist_ok=True)

    count = 0
    for name, filepos, size in lumps_by_name.values():
        if wanted is not None:
            if name.upper() not in wanted:
                continue
        elif not name.upper().startswith("D_"):
            continue

        mus_data = wad_data[filepos:filepos + size]
        if mus_data[:4] != MUS_MAGIC:
            if wanted is not None:
                print(f"skip {name}: not a MUS lump")
            continue

        midi_bytes = mus_to_midi_bytes(mus_data, name)
        out_path = os.path.join(args.output, f"{name}.mid")
        with open(out_path, "wb") as fh:
            fh.write(midi_bytes)
        print(f"{name}  ({size} bytes MUS -> {len(midi_bytes)} bytes MIDI)")
        count += 1

    print(f"converted {count} track(s) -> {args.output}")


if __name__ == "__main__":
    main()
