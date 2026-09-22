# N64 Doom

A DOOM port for the Nintendo 64, built on [libdragon](https://github.com/DragonMinded/libdragon)
(the open-source N64 homebrew SDK), plus a tool that turns your own IWAD into
a runnable `.z64`.

No copyrighted game data is in this repository. You supply your own IWAD; the
build packs it into the ROM.

This is an update of [jnmartin84/64doom](https://github.com/jnmartin84/64doom)
(see Acknowledgements below), not a from-scratch port: control (analog
movement/look, configurable sensitivity, rumble) and audio both got
reworked on top of it.

## What you need to play

* An **[Expansion Pak](https://en.wikipedia.org/wiki/Nintendo_64_accessories#Expansion_Pak)**
  (4MB RAM upgrade) — on real hardware, or the equivalent option in your
  emulator. The game needs it; it won't boot without one.
* A **Controller Pak** (memory card) in controller port 1 if you want to save
  your game — without one, you can still play, just not save.

## The IWAD you need

A **legally obtained IWAD** — one of the original DOOM game data files. You
must own it; it is not distributed here.

The build accepts, dropped into `input/`:

* **`DOOM1.WAD`** — Shareware Doom
* **`DOOM.WAD`** — Registered Doom
* **`DOOMU.WAD`** — The Ultimate Doom
* **`DOOM2.WAD`** — Doom II: Hell on Earth
* **`PLUTONIA.WAD`** — Final Doom: The Plutonia Experiment
* **`TNT.WAD`** — Final Doom: TNT Evilution

## What you need

* **[Python 3](https://www.python.org/downloads/)** on your `PATH`
  (`python --version`).
* **[FluidSynth](https://www.fluidsynth.org/) and [ffmpeg](https://ffmpeg.org/)**
  on your `PATH` — used once per IWAD to render its music. See "Installing
  FluidSynth/ffmpeg" below if you don't already have them.
* **Your own IWAD** — see the section above.

## How to build the ROM

1. Grab the latest [Release](../../releases) and unzip it
2. Put your IWAD (e.g. `DOOM2.WAD`) in `input/`.
3. Build it:
   * **Windows:** double-click `build.cmd` (or run it from a terminal).
   * **Any platform:** `python tools/build_rom.py`
4. Wait for `OK -> output/DOOM2.z64`. That file is your ROM — run it in an
   emulator (e.g. [Ares](https://ares-emu.net/), [simple64](https://simple64.github.io/))
   or flash it to a flashcart (e.g. EverDrive-64).

The first build renders the IWAD's music (needs FluidSynth/ffmpeg — see
below); later builds of the same IWAD skip straight to packing. If you ever
want to redo it (say, after installing a different SoundFont), pass
`--force-music`.

If `input/` has more than one IWAD, point at the one you want:

```sh
python tools/build_rom.py --wad input/DOOM2.WAD
```

### Installing FluidSynth on Windows

* Easiest, with a package manager (run in PowerShell/Terminal):
  * `winget install FluidSynth.FluidSynth` &nbsp;— or —&nbsp; `choco install fluidsynth`
  * Open a **new** terminal afterwards so the updated `PATH` takes effect.
* Manual: download the latest `fluidsynth-*-win10-x64.zip` from
  [FluidSynth releases](https://github.com/FluidSynth/fluidsynth/releases),
  unzip it somewhere permanent (e.g. `C:\fluidsynth`), then add its `bin`
  folder (`C:\fluidsynth\bin`, the one containing `fluidsynth.exe` and the
  `libfluidsynth-*.dll`) to your `PATH`: *Start → "Edit the system environment
  variables" → Environment Variables → select `Path` → Edit → New*.
* Verify: open a new terminal and run `fluidsynth --version`.

### Installing FFmpeg on Windows

* Easiest, with a package manager (run in PowerShell/Terminal):
  * `winget install Gyan.FFmpeg` &nbsp;— or —&nbsp; `choco install ffmpeg-full`
  * Open a **new** terminal afterwards so the updated `PATH` takes effect.
* Manual: download a build from
  [gyan.dev](https://www.gyan.dev/ffmpeg/builds/) (get "ffmpeg-release-full")
  or [BtbN/FFmpeg-Builds](https://github.com/BtbN/FFmpeg-Builds/releases),
  unzip it somewhere permanent (e.g. `C:\ffmpeg`), then add the `bin` folder
  (`C:\ffmpeg\bin`, the one containing `ffmpeg.exe` and `ffprobe.exe`) to your
  `PATH`, the same way as above.
* Verify: open a new terminal and run `ffmpeg -version` and `ffprobe -version`.

On macOS/Linux, install both from your package manager instead (e.g.
`brew install fluidsynth ffmpeg` or `apt install fluidsynth ffmpeg`).

## Building from source

The above is enough for everyday use — building a ROM from your own IWAD.
Working on the engine itself (the C code in `src/`) needs the full N64
toolchain, which runs in Docker via the `libdragon` CLI:

* **[Docker](https://www.docker.com/)**, used by the `libdragon` CLI to run
  the N64 toolchain in a container.
* **The `libdragon` CLI** (`npm install -g libdragon`, already used to
  scaffold this repo — see `.libdragon/config.json`).

```sh
libdragon exec env IWAD_PREFIX=DOOM2 make
```

builds `output/DOOM2.z64` straight from source (`IWAD_PREFIX` must match your
IWAD's filename, uppercase, without the extension). `./build_all.sh` does
this for every `*.WAD` in `input/` in one pass — see its own comments for
details, including its music-rendering and unsupported-IWAD handling.

`make engine` builds just the WAD-independent engine binary
(`src/ENGINE.elf.stripped` + `.sym` + `.msym`) — the same one CI publishes in
each Release. Copy those three files into `tools/vendor/engine/` (renamed to
`doom.elf.stripped`/`doom.sym`/`doom.msym` — see its README) to use
`tools/pack_rom.py`/`tools/build_rom.py` against a locally-built engine
instead of a downloaded Release. `tools/vendor/bin/build-tools.sh` similarly
rebuilds the small native packing tools (`mkdfs`, `n64tool`, `audioconv64`)
from the `libdragon` submodule with any native C compiler, no Docker needed
for those.

## Acknowledgements

Thanks to **[jnmartin84](https://github.com/jnmartin84)** for
**[64doom](https://github.com/jnmartin84/64doom)**
(the `rdpq` branch specifically), the libdragon port of id Software's
released DOOM source that this project is built on top of — the engine,
renderer and N64 platform layer here all started as that code. Thanks also
to the **[libdragon](https://github.com/DragonMinded/libdragon)** team for
the open-source N64 SDK. 

## AI Note

The application was developed using AI. I'm just an enthusiast who wanted to create interesting projects. In this case, how it was achieved is not relevant to me.
