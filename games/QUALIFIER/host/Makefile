CC ?= cc
CFLAGS ?= -O2 -Wall -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable -Wno-c23-extensions -Wno-misleading-indentation -DLSB_FIRST -DPP_DEBUG -I../core -I../core/z80 -I../core/z8000 -I../main/roms -I../main -I../../../host/hiscore -I../components/medalboot/include
SRCS = harness.c ../components/medalboot/src/hiscore.c ../../../host/hiscore/store_file.c ../core/polepos.c ../core/polepos_video.c ../core/polepos_sound.c ../core/z80/Z80.c ../core/z8000/z8002.c
harness: $(SRCS) ../core/*.h ../core/z80/*.h ../core/z8000/*.h
	$(CC) $(CFLAGS) -o $@ $(SRCS) -lm
clean:
	rm -f harness
