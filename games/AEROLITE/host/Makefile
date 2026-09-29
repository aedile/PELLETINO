CC ?= cc
CFLAGS ?= -O2 -Wall -Wno-unused-function -Wno-unused-variable -I../core -I../main/roms -I../main -I../../../host/hiscore -I../components/medalboot/include
SRCS = harness.c ../components/medalboot/src/hiscore.c ../../../host/hiscore/store_file.c ../core/asteroids.c
harness: $(SRCS) ../core/*.h
	$(CC) $(CFLAGS) -o $@ $(SRCS) -lm
clean:
	rm -f harness
