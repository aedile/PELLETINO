CC ?= cc
CFLAGS ?= -O2 -Wall -Wno-unused-function -Wno-unused-variable -DCE_DEBUG -I../core -I../main/roms -I../main -I../../../host/hiscore -I../components/medalboot/include
SRCS = harness.c ../components/medalboot/src/hiscore.c ../../../host/hiscore/store_file.c ../core/centiped.c ../core/centiped_video.c ../core/pokey.c
harness: $(SRCS) ../core/*.h
	$(CC) $(CFLAGS) -o $@ $(SRCS) -lm
clean:
	rm -f harness
