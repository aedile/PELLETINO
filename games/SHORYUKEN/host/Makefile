# SPDX-License-Identifier: 0BSD
# make                    the harness, knobs at their defaults
# make KNOBS="-DKNOB_SOUND=FM -DSOUND_RATE=22050 -DKNOB_CPU_CORE=OWN"
CC ?= cc
KNOBS ?=
OPT ?= -O2
CFLAGS ?= $(OPT) -g -Wall -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable -DLSB_FIRST -DEXECZ80 $(KNOBS) \
	-I../core -I../core/m68k -I../core/z80 -I../main
CORE = ../core/cps1.c ../core/cps1_video.c ../core/cps1_sound.c ../core/cps1_idle.c ../core/ym2151.c ../core/okim6295.c \
	../core/m68kown.c ../core/m68k/m68kcpu.c ../core/m68k/m68kops.c
# the Z80 wants its bus, and with the sound off there is none to give it
ifneq (,$(findstring KNOB_SOUND=FM,$(KNOBS)))
CORE += ../core/z80/Z80.c
endif
harness: harness.c $(CORE) ../core/*.h ../core/m68k/*.h ../main/knobs.h
	$(CC) $(CFLAGS) -o $@ harness.c $(CORE) -lm
clean:
	rm -f harness
