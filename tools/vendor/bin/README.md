# tools/vendor/bin

The host tools `tools/pack_rom.py` shells out to:

| tool | job |
|---|---|
| `mkdfs` | pack the DragonFS filesystem image |
| `n64tool` | assemble ELF + sym + msym + DFS into the `.z64` |
| `audioconv64` | pre-rendered music WAV -> `.wav64` (Opus, looping) |

`pack_rom.py` finds tools in this order: `--tools-dir`, `$N64_INST/bin`, this
folder, then `PATH`. Drop platform-native binaries here if they are not
already on your system.

## Getting them

* **Download a [Release](../../../../releases)** - each one ships Windows
  `.exe`s built the way described below, plus the matching prebuilt engine
  (see `../engine/README.md`). This is the no-Docker, no-toolchain path.
* **Build them yourself.** They are small single-file C programs; the
  libdragon Docker toolchain is not required, just the `libdragon` submodule
  checked out and any native C compiler:

  ```sh
  ./build-tools.sh                 # gcc on PATH
  CC=/c/Users/you/scoop/apps/gcc/current/bin/gcc ./build-tools.sh
  ```

  The release `.exe`s are made this way with mingw-w64 gcc on Windows x64 in
  CI. They are not portable across OS/arch - rebuild for yours, or delete
  them and rely on `$N64_INST/bin` (i.e. a full `libdragon`/Docker install).
