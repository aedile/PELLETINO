CC ?= cc
CFLAGS ?= -O2 -Wall -Wno-unused-function -Wno-unused-variable -I../core -I../main/roms -I../main -I../../../host/hiscore -I../components/medalboot/include
SRCS = harness.c ../components/medalboot/src/hiscore.c ../../../host/hiscore/store_file.c ../core/joust.c ../core/joust_video.c ../core/joust_sound.c
harness: $(SRCS) ../core/*.h ../core/e6809.c ../main/roms/joust_roms.h
	$(CC) $(CFLAGS) -o $@ $(SRCS) -lm
clean:
	rm -f harness
