CC = gcc
BIN = ddave

H_FILES := include/bullet.h
H_FILES += include/dave.h
H_FILES += include/display.h
H_FILES += include/game.h
H_FILES += include/input.h
H_FILES += include/invfreq.h
H_FILES += include/monster.h
H_FILES += include/plasma.h
H_FILES += include/soundfx.h
H_FILES += include/tile.h

C_FILES := main.c
C_FILES += game.c
C_FILES += display.c
C_FILES += tile.c
C_FILES += dave.c
C_FILES += bullet.c
C_FILES += monster.c
C_FILES += plasma.c
C_FILES += invfreq.c
C_FILES += soundfx.c



CFLAGS = -rdynamic -std=c99 -Wall
CFLAGS += $(shell pkg-config --cflags sdl3)
LIBS := $(shell pkg-config --libs sdl3)

all: $(BIN)

$(BIN): $(C_FILES) $(H_FILES)
	$(CC) $(C_FILES) $(CFLAGS) -Iinclude $(LIBS) -o $(BIN)

# macOS only: wraps the game in a double-clickable .app, so it opens as a window
# on its own, without a terminal.
app: $(BIN)
	sh scripts/package-macos-app.sh $(BIN) "Deadly Dave.app" 1.0

# Rebuilds assets/icon.* (icns, ico, png) from the sprite used as game icon.
icon:
	python3 scripts/make-icon.py

clean:
	rm -f $(BIN)
	rm -rf "Deadly Dave.app"
