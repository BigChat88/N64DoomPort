# soundfont/

`SC-55 Deemster [GZDoom].sf2` is what `../render_music.py` plays the WAD's
music through (via FluidSynth, on the host, at build time - see the
repo root README's "How to Build") to produce the mastered WAV files that
get packed into the ROM. It's a third-party General MIDI SoundFont built
from Roland SC-55 samples - the sound module DOOM's original PC MIDI
soundtrack was composed for - sourced from the GZDoom community.

To render with a different SoundFont, pass `--soundfont <file>` to
`render_music.py`.

## History

Earlier revisions of this project synthesized music live on the N64
instead of pre-rendering it, first with a small hand-picked instrument
sample bank (approximated from a SoundFont, not the SoundFont itself),
later with libdragon's real SoundFont synthesizer. Both were dropped -
see the big comment at the top of `src/i_sound.c` for why - in favor of
rendering once, offline, with the same real FluidSynth + SoundFont this
file still describes, and just playing the result back as ordinary audio.

`GM.sf2`, `8MbGM_Enhanced18.sf2`, `SCC1T2.sf2`, `Nintendo_Soundfont.sf2`,
and `Doom Snes.sf2` were the other instrument-bank sources from that
sample-bank era (a "Music: Type I..IV" menu switched between them). They're
unused now but still here in case a multi-SoundFont selector is worth
rebuilding on top of the current, pre-rendering approach.
