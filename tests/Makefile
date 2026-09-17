CC = gcc

# SDL3 headers need a C99 compiler, and SDL3 ships no sdl3-config.
CFLAGS = -g -std=gnu99 -Wall `pkg-config --cflags sdl3` -DHAVE_SDL3
LDFLAGS += `pkg-config --libs sdl3`
INCS = -I ../include/

all: test_invfreq test_monster test_display

test_invfreq: test_invfreq.c ../invfreq.c
	$(CC) test_invfreq.c ../invfreq.c $(INCS) $(CFLAGS) -o test_invfreq $(LDFLAGS)

test_monster: test_monster.c ../tile.c ../plasma.c ../monster.c 
	$(CC) test_monster.c ../tile.c ../plasma.c ../monster.c $(INCS) $(CFLAGS) -o test_monster $(LDFLAGS)

test_display: test_display.c ../display.c
	$(CC) test_display.c ../display.c $(INCS) $(CFLAGS) -o test_display $(LDFLAGS)

clean:
	rm -f test_invfreq test_monster test_display
