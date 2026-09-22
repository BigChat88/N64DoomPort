# tools/vendor/engine

Put `doom.elf.stripped` + `doom.sym` + `doom.msym` here for a Docker-free
local build with `tools/pack_rom.py`.

These three files are the compiled DOOM engine - code only, no WAD, no
music - and are **identical for every supported IWAD** (nothing in
`src/Makefile`'s `engine` target is conditioned on `IWAD_PREFIX` except
output filenames; see its comment). That's what makes it safe to prebuild
once and reuse for any of DOOM1/DOOM/DOOMU/DOOM2/PLUTONIA/TNT.

Get them from:

* **A [Release](../../../../releases)** - CI builds these from source and
  attaches them (renamed from `ENGINE.elf.stripped` etc. to the generic
  names above) to every push to `main`.
* **`make engine`** at the repo root, run inside `libdragon exec` (needs
  Docker + the `libdragon` CLI - see the top-level README). Copy the
  resulting `src/ENGINE.elf.stripped`, `src/ENGINE.sym` and
  `src/ENGINE.msym` here as `doom.elf.stripped`, `doom.sym` and
  `doom.msym`.
