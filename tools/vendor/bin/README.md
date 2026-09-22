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
* **Build them yourself.** The libdragon Docker toolchain is not required,
  just the `libdragon` submodule checked out and a native C **and C++**
  compiler (`libdragon/tools/Makefile`, which `build-tools.sh` delegates to,
  always links through `$CXX` even for the C-only tools here):

  ```sh
  ./build-tools.sh                                        # gcc/g++ on PATH
  CC=x86_64-w64-mingw32-gcc CXX=x86_64-w64-mingw32-g++ ./build-tools.sh   # cross to Windows
  ```

  The release `.exe`s are made this way with mingw-w64 gcc/g++ on Linux in
  CI. They are not portable across OS/arch - rebuild for yours, or delete
  them and rely on `$N64_INST/bin` (i.e. a full `libdragon`/Docker install).
