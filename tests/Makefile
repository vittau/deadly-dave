CC = gcc

# SDL3 headers need a C99 compiler, and SDL3 ships no sdl3-config.
CFLAGS = -g -std=gnu99 -Wall `pkg-config --cflags sdl3` -DHAVE_SDL3
LDFLAGS += `pkg-config --libs sdl3`
# display.c pulls in the CRT filters, which use sin/cos/pow/exp.
LDFLAGS += -lm
INCS = -I ../include/

all: test_config test_invfreq test_monster test_display test_filter

test_config: test_config.c ../config.c
	$(CC) test_config.c ../config.c $(INCS) $(CFLAGS) -o test_config $(LDFLAGS)

test_invfreq: test_invfreq.c ../invfreq.c
	$(CC) test_invfreq.c ../invfreq.c $(INCS) $(CFLAGS) -o test_invfreq $(LDFLAGS)

test_monster: test_monster.c ../tile.c ../plasma.c ../monster.c 
	$(CC) test_monster.c ../tile.c ../plasma.c ../monster.c $(INCS) $(CFLAGS) -o test_monster $(LDFLAGS)

test_display: test_display.c ../display.c ../filter.c ../composite.c ../ntsc.c
	$(CC) test_display.c ../display.c ../filter.c ../composite.c ../ntsc.c $(INCS) $(CFLAGS) -o test_display $(LDFLAGS)

test_filter: test_filter.c ../filter.c ../composite.c ../ntsc.c
	$(CC) test_filter.c ../filter.c ../composite.c ../ntsc.c $(INCS) $(CFLAGS) -o test_filter $(LDFLAGS)

clean:
	rm -f test_config test_invfreq test_monster test_display test_filter
