CC ?= cc
CFLAGS ?= -O2 -Wall -Wno-unused-function -Wno-unused-variable -I../core -I../main/roms -I../main -I../../../host/hiscore -I../components/medalboot/include
SRCS = harness.c ../components/medalboot/src/hiscore.c ../../../host/hiscore/store_file.c ../core/btime.c ../core/btime_sound.c ../core/btime_video.c ../core/ay8910.c
harness: $(SRCS) ../core/*.h ../main/roms/btime_roms.h
	$(CC) $(CFLAGS) -o $@ $(SRCS) -lm
clean:
	rm -f harness
