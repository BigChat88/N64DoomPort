ifeq ($(IWAD_DIRECTORY),)
  $(error IWAD_DIRECTORY is not set)
endif
ifeq ($(IWAD_PREFIX),)
  $(error IWAD_PREFIX is not set)
endif

ROOTDIR			= $(N64_INST)
GCCN64PREFIX = $(ROOTDIR)/bin/mips64-elf-
STRIP = $(GCCN64PREFIX)strip
MKDFSPATH = $(ROOTDIR)/bin/mkdfs
MKSPRITEPATH = $(ROOTDIR)/bin/mksprite
N64TOOL = $(ROOTDIR)/bin/n64tool
N64_SYM = $(ROOTDIR)/bin/n64sym
N64_DSO = $(ROOTDIR)/bin/n64dso
N64_DSOEXTERN = $(ROOTDIR)/bin/n64dso-extern
N64_DSOMSYM = $(ROOTDIR)/bin/n64dso-msym
N64_ELFCOMPRESS = $(ROOTDIR)/bin/n64elfcompress
AUDIOCONV64 = $(ROOTDIR)/bin/audioconv64
# Pre-rendered music WAVs for this IWAD - see tools/render_music.py and the
# music step below. Rendering needs fluidsynth+ffmpeg, which aren't in this
# Docker toolchain image, so it's a separate host-side step, not part of
# this Makefile.
MUSIC_WAV_DIR = $(CURDIR)/../music_wav/$(IWAD_PREFIX)
LIBS = -ldragon -lc -lm -ldragonsys
PROG_NAME = $(IWAD_PREFIX)
OPTFLAG = -O3



# function/data sections + gc-sections saves about ~200kb from output binary
#CFLAGS = -falign-functions=16 -ffile-prefix-map=$(CURDIR)= -DN64 -fdiagnostics-color=always -Wno-error=switch -Wall -g -mabi=o64 -mno-shared -mno-abicalls -mno-branch-likely -mno-llsc -mno-check-zero-division -mfix4300 -std=gnu99 -march=vr4300 -mtune=vr4300 -mips3 -G4 $(OPTFLAG) -I$(ROOTDIR)/mips64-elf/include -DRANGECHECK
#LINK_FLAGS = -G4 -g -L$(ROOTDIR)/mips64-elf/lib $(LIBS) -Tn64.ld
# LIBDRAGON_PREVIEW=2: i_sound.c uses the mixer/mid64/sf64 preview APIs
# (mid64player, sf64_synth, mixer_try_play, mixer_ch_set_gain) as permanent,
# load-bearing functionality, not experimentally - see preview.h.
CFLAGS = -ffunction-sections -fdata-sections -falign-functions=32 -ffile-prefix-map=$(CURDIR)= -DN64 -DLIBDRAGON_PREVIEW=2 -fdiagnostics-color=always -Wno-error=switch -Wall -g -mabi=o64 -mno-shared -mno-abicalls -mno-branch-likely -mno-llsc -mno-check-zero-division -mfix4300 -std=gnu99 -march=vr4300 -mtune=vr4300 -mips3 -G4 $(OPTFLAG) -I$(ROOTDIR)/mips64-elf/include
# -DRANGECHECK
# --allow-multiple-definition works around libdragonsys.a and libc.a both
# defining the same newlib lock-retargeting shims (__retarget_lock_* etc.) in
# some libdragon toolchain builds - both are the same trivial shim, so it's
# safe to let the linker keep whichever one it sees first instead of erroring.
#
# libdragon.a's mid64.c (see i_sound.c's new music playback) uses __int128
# for its tick timing math, which the compiler lowers to calls to
# __divti3/__modti3 - normally supplied by libgcc.a, which n64.mk's own
# link step gets for free because it links through the gcc driver (which
# auto-selects and adds the multilib-matched libgcc for whatever -march/
# -mabi/-mips flags it was given). This project links with the raw
# linker instead, so libgcc is never added implicitly; naming this exact
# file (queried from gcc itself, for the same flags CFLAGS compiles
# with, rather than hardcoded, so it still resolves correctly if the
# toolchain image's GCC version ever changes) restores that.
LIBGCC = $(shell $(GCCN64PREFIX)gcc -march=vr4300 -mtune=vr4300 -mabi=o64 -mips3 -print-libgcc-file-name)
LINK_FLAGS = --gc-sections -G4 -g -L$(ROOTDIR)/mips64-elf/lib $(LIBS) $(LIBGCC) -Tn64.ld --allow-multiple-definition

CC = $(GCCN64PREFIX)gcc
AS = $(GCCN64PREFIX)as
LD = $(GCCN64PREFIX)ld
OBJCOPY = $(GCCN64PREFIX)objcopy
OBJDUMP = $(GCCN64PREFIX)objdump



O=obj

OBJS=						\
		$(O)/r_draw.o			\
		$(O)/i_sound.o			\
		$(O)/endoom.o			\
		$(O)/dll.o			\
		$(O)/hash.o			\
		$(O)/lzfx.o			\
		$(O)/i_video.o			\
		$(O)/i_input.o			\
		$(O)/i_intro.o			\
		$(O)/doomdef.o			\
		$(O)/doomstat.o			\
		$(O)/dstrings.o			\
		$(O)/i_system.o			\
		$(O)/i_net.o			\
		$(O)/v_video.o			\
		$(O)/f_finale.o			\
		$(O)/f_wipe.o 			\
		$(O)/d_main.o			\
		$(O)/d_net.o			\
		$(O)/d_items.o			\
		$(O)/g_game.o			\
		$(O)/p_saveg.o			\
		$(O)/m_menu.o			\
		$(O)/m_thinfont.o		\
		$(O)/m_misc.o			\
		$(O)/m_argv.o  			\
		$(O)/m_bbox.o			\
		$(O)/m_cheat.o			\
		$(O)/m_random.o			\
		$(O)/am_map.o			\
		$(O)/p_ceilng.o			\
		$(O)/p_doors.o			\
		$(O)/p_enemy.o			\
		$(O)/p_floor.o			\
		$(O)/p_inter.o			\
		$(O)/p_lights.o			\
		$(O)/p_map.o			\
		$(O)/p_maputl.o			\
		$(O)/p_plats.o			\
		$(O)/p_pspr.o			\
		$(O)/nodereorder.o		\
		$(O)/p_setup.o			\
		$(O)/p_sight.o			\
		$(O)/p_spec.o			\
		$(O)/p_switch.o			\
		$(O)/p_mobj.o			\
		$(O)/p_telept.o			\
		$(O)/p_tick.o			\
		$(O)/p_user.o			\
		$(O)/r_bsp.o			\
		$(O)/r_data.o			\
		$(O)/r_main.o			\
		$(O)/r_plane.o			\
		$(O)/r_segs.o			\
		$(O)/r_sky.o			\
		$(O)/r_things.o			\
		$(O)/w_wad.o			\
		$(O)/wi_stuff.o			\
		$(O)/z_zone.o			\
		$(O)/st_lib.o			\
		$(O)/st_stuff.o			\
		$(O)/hu_stuff.o			\
		$(O)/hu_lib.o			\
		$(O)/s_sound.o			\
		$(O)/info.o			\
		$(O)/sounds.o			\
		$(O)/i_main.o

# The compiled game engine (code only - no WAD, no music) is identical for
# every IWAD_PREFIX: nothing here is conditioned on its value except output
# filenames, so this only needs building once and can be reused to pack a
# ROM for any of the six supported IWADs (see tools/pack_rom.py). Split out
# of the $(PROG_NAME).z64 recipe below so CI can build just this much
# without a copyrighted WAD on hand.
engine: $(PROG_NAME).elf.stripped

$(PROG_NAME).elf.stripped: $(PROG_NAME).elf
	$(N64_SYM) $(PROG_NAME).elf $(PROG_NAME).sym
	$(N64_DSOMSYM) $(PROG_NAME).elf $(PROG_NAME).msym
	cp $(PROG_NAME).elf $(PROG_NAME).elf.stripped
	$(STRIP) -s $(PROG_NAME).elf.stripped
	$(N64_ELFCOMPRESS) -o . -c 1 $(PROG_NAME).elf.stripped

$(PROG_NAME).z64: $(PROG_NAME).elf.stripped
	rm -f $(PROG_NAME).z64
	rm -f rom.dfs
	# Remove any other IWAD left in filesystem/ from a previous build with a
	# different IWAD_PREFIX (e.g. DOOM.WAD from a prior DOOM build) - without
	# this, building DOOM2 after DOOM would silently pack both WADs into the
	# DOOM2 ROM instead of just DOOM2.WAD.
	rm -f filesystem/*.WAD
	cp $(IWAD_DIRECTORY)/$(IWAD_PREFIX).WAD filesystem
	echo $(IWAD_PREFIX).WAD > filesystem/identifier
	# Music: pack the pre-rendered WAVs for this IWAD (see
	# tools/render_music.py) as looping, Opus-compressed wav64 files.
	# These aren't rendered here - see MUSIC_WAV_DIR's comment - so fail
	# clearly if that step hasn't been run yet, rather than silently
	# shipping a ROM with no music (or, worse, another IWAD's).
	@if [ ! -d "$(MUSIC_WAV_DIR)" ] || [ -z "$$(ls -A "$(MUSIC_WAV_DIR)"/*.wav 2>/dev/null)" ]; then \
		echo "error: no rendered music in $(MUSIC_WAV_DIR)"; \
		echo "  run this first (needs fluidsynth + ffmpeg on PATH):"; \
		echo "  python3 ../tools/render_music.py $(IWAD_DIRECTORY)/$(IWAD_PREFIX).WAD -o $(MUSIC_WAV_DIR)"; \
		exit 1; \
	fi
	rm -rf filesystem/mus
	mkdir -p filesystem/mus
	# Matches N64DoomRPGPort's exact audioconv64 invocation for its own
	# looping music (see tools/render_music.py's OUTPUT_RATE comment for
	# why the WAV is pre-resampled to 48000 Hz rather than passing flags
	# here to affect the encode).
	$(AUDIOCONV64) --wav-compress 3 --wav-mono --wav-loop true -o filesystem/mus $(MUSIC_WAV_DIR)/*.wav
	$(MKDFSPATH) rom.dfs ./filesystem/
	$(N64TOOL) \
        --title $(PROG_NAME) --toc --output $(PROG_NAME).z64 --align 256 $(PROG_NAME).elf.stripped --align 8 $(PROG_NAME).sym --align 8 $(PROG_NAME).msym --align 16 rom.dfs
	chmod ugo=rw $(PROG_NAME).z64

$(PROG_NAME).elf : $(OBJS)
	$(LD) -o $(PROG_NAME).elf $(OBJS) $(LINK_FLAGS)

copy: $(PROG_NAME).z64
	cp $(PROG_NAME).z64 ~/

all: $(PROG_NAME).z64

clean:
	rm -f *.z64 *.elf *.elf.stripped *.msym *.sym *.bin *.dfs
	rm -f $(O)/*
	# Leftovers from the old sf64-synth music system (see i_sound.c) -
	# harmless to leave, but dead weight in the ROM if they do.
	rm -f filesystem/soundfont.sf64

.PHONY: engine copy all clean

$(O):
	mkdir -p $(O)

$(O)/%.o:	%.c | $(O)
	$(CC) $(CFLAGS) -c $< -o $@

#############################################################
#
#############################################################
