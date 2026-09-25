# Top-level forwarder for the 64Doom N64 port (see src/Makefile for the real build).
#
# Usage (from repo root, inside `libdragon exec`/`libdragon make`, or any shell
# with the N64 toolchain on PATH):
#
#   make IWAD_PREFIX=DOOM2
#
# Drop your IWAD(s) into input/ first (see input/README.md) - IWAD_DIRECTORY
# defaults to input/, but can still be pointed elsewhere:
#
#   make IWAD_DIRECTORY=/path/to/wads IWAD_PREFIX=DOOM2
#
# IWAD_PREFIX must be one of: DOOM1, DOOM, DOOMU, DOOM2, PLUTONIA, TNT, CHEX, CHEX2
# (uppercase, matching your IWAD filename, e.g. input/DOOM2.WAD).
#
# The resulting .z64 is written to output/ (see output/README.md) - ready to
# grab for a flashcart (e.g. EverDrive/ED64) or emulator.

IWAD_DIRECTORY ?= $(CURDIR)/input
# Only used by the `engine` target below, which doesn't need a real IWAD -
# see src/Makefile's own comment on it. Override if you like; the compiled
# bytes are the same either way.
ENGINE_PREFIX ?= ENGINE

.PHONY: all clean copy engine

all:
	$(MAKE) -C src all IWAD_DIRECTORY=$(IWAD_DIRECTORY)
	mkdir -p output
	cp src/$(IWAD_PREFIX).z64 output/

clean:
	$(MAKE) -C src clean
	rm -f output/$(IWAD_PREFIX).z64

copy:
	$(MAKE) -C src copy

# Builds just the WAD-independent engine (<prefix>.elf.stripped + .sym +
# .msym) - no IWAD required. Used by CI (see .github/workflows/release.yml)
# to publish a prebuilt engine that tools/pack_rom.py can then pack into a
# ROM for any IWAD without needing the N64 toolchain locally.
engine:
	$(MAKE) -C src engine IWAD_DIRECTORY=$(IWAD_DIRECTORY) IWAD_PREFIX=$(ENGINE_PREFIX)
