CC = gcc
CFLAGS = -g -Wall -Wextra
LINKERFLAGS = -Iinclude -Llib -lSDL2 -lSDL2_image -lSDL2_ttf -lSDL2_mixer

all: emu

emu:
	$(CC) main.c $(CFLAGS) -o build/scl $(LINKERFLAGS)

main.o: main.c
	$(CC) $(CFLAGS) -c main.c

clean:
	rm -f emu *.o

