# input/

Put your own IWAD(s) here, then build from the repo root.

Accepted (uppercase, matching the filename you pass as `IWAD_PREFIX`):

* `DOOM1.WAD` — Shareware Doom
* `DOOM.WAD` — Registered Doom
* `DOOMU.WAD` — The Ultimate Doom
* `DOOM2.WAD` — Doom II: Hell on Earth
* `PLUTONIA.WAD` — Final Doom: The Plutonia Experiment
* `TNT.WAD` — Final Doom: TNT Evilution
* `CHEX.WAD` — Chex Quest, **plus `chex.deh`** in this same folder (its
  DeHackEd patch, from
  https://www.doomworld.com/idgames/themes/chex/chexdeh - the build stops
  if it's missing). Any `<NAME>.deh` next to `<NAME>.WAD` is packed and
  applied the same way.

You can drop more than one in here at the same time (e.g. both `DOOM.WAD` and
`DOOM2.WAD`) - `IWAD_PREFIX` picks which one a given build uses. See the repo
README for the full build command, or run `./build_all.sh` from the repo
root to build every WAD dropped in here in one go.

The Ultimate Doom in particular is often distributed as `UDOOM.WAD` instead
of `DOOMU.WAD` - either name is fine, `./build_all.sh` checks the file's own
contents and corrects for this automatically (see
`tools/identify_iwad.py`). Building it by hand instead (the manual steps in
the repo README) still needs the exact `DOOMU.WAD` name, since the engine's
own `IdentifyVersion()` (`src/d_main.c`) only ever checks the filename.

Nothing here is committed (copyrighted game data) - bring your own, legally
obtained copy of each IWAD.
