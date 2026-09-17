#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#define SDL_MAIN_HANDLED

#if defined LINUX || __linux__ || _LINUX
#include <execinfo.h>
#include <unistd.h>
#endif

#include "game.h"

#if defined LINUX || __linux__ || _LINUX
void sigseg_handler(int sig) {
    void *array[10];
    size_t size;

    // get void*'s for all entries on the stack
    size = backtrace(array, 10);

    // print out all the frames to stderr
    fprintf(stderr, "Error: signal %d:\n", sig);
    backtrace_symbols_fd(array, size, STDERR_FILENO);
    exit(1);
}
#endif

void usage() {
    printf("-w             start windowed \n");
    printf("-l <level>     start in level \n");
    printf("\n");
    printf("Move with WASD (W jumps, S climbs down), the arrow keys work too. \n");
    printf("Space shoots, J is the jetpack. \n");
    printf("On a controller: stick or D-pad moves, A jumps, B is the jetpack, X shoots, Start is Escape. \n");
    printf("In game, F5 switches between pixel perfect scaling and filling the screen. \n");
#if defined(__APPLE__)
    printf("Cmd+Enter switches between full screen and windowed. The game starts in full screen. \n");
#else
    printf("Alt+Enter switches between full screen and windowed. The game starts in full screen. \n");
#endif
}

int main(int argc, char **argv) {
    int is_windowed = 0;
    int level = 1;

#if defined LINUX || __linux__ || _LINUX
    signal(SIGSEGV, sigseg_handler);
#endif

    argc--;
    argv++;
    while (argc > 0) {
        if (strcmp(argv[0], "-w") == 0) {
            argc--;
            argv++;
            is_windowed = 1;

        } else if (strcmp(argv[0], "-l") == 0) {
            argc--;
            argv++;
            if (argc < 1) {
                usage();
                exit(-1);
            }
            level = atoi(argv[0]);
            argc--;
            argv++;

        } else {
            usage();
            return 0;
        }
    }

    return game_main(is_windowed, level);
}
