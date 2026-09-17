#ifndef DAVE_H
#define DAVE_H

#include "tile.h"
#include "soundfx.h"

#define DAVE_WALKING_STATE_STANDING  0
#define DAVE_WALKING_STATE_COOLDOWN1_RIGHT  1
#define DAVE_WALKING_STATE_COOLDOWN1_LEFT  2
#define DAVE_WALKING_STATE_COOLDOWN2_RIGHT  3
#define DAVE_WALKING_STATE_COOLDOWN2_LEFT  4

#define DAVE_CLIMBING_STATE_READY 0
#define DAVE_CLIMBING_STATE_COOLDOWN 1
#define DAVE_CLIMBING_STATE_JUMP_RIGHT 2
#define DAVE_CLIMBING_STATE_JUMP_LEFT 3


#define DAVE_STATE_STANDING    0
#define DAVE_STATE_WALKING     1
#define DAVE_STATE_JUMPING     2
#define DAVE_STATE_CLIMBING    3
#define DAVE_STATE_FREEFALLING 4
#define DAVE_STATE_JETPACKING  5
#define DAVE_STATE_BURNING     6
#define DAVE_STATE_DEAD        7

/*
 * Facing is a bit strange because original code has a few nuances. One of them being a front
 * posture (dave looking at the player), but when jumping dave has a direction.
 *
 * FRONT - Dave looks to player, has no inclination, this changes on first <- or -> keystroke
 * FRONTR - Dave looks to player, has inclination to the right
 * FRONTL - Dave looks to player, has inclination to the left
 * LEFT - Dave looks left
 * RIGHT - Dave looks right
 */
#define DAVE_DIRECTION_FRONT   0
#define DAVE_DIRECTION_FRONTR  1
#define DAVE_DIRECTION_FRONTL  2
#define DAVE_DIRECTION_LEFT    3
#define DAVE_DIRECTION_RIGHT   4

typedef struct dave_struct {
    tile_t *tile;

    soundfx_t *sfx;
    int state;
    int face_direction;
    int has_trophy;
    int has_gun;
    int jetpack_bars;
    int on_fire;
    int on_tree;
    int ticks_in_state;
    int mute;

    // sub-states & helpers specific to states
    int walk_state;
    int step_count;
    int jump_state;
    int jump_cooldown_count;
    int climb_state;
    /*
     * Jump is edge triggered: it only starts on the frame the key goes from up
     * to down while Dave is on the ground. A press while in the air is dropped
     * instead of being remembered for the landing. key_up_prev is the held
     * state of the key on the previous tick.
     */
    int jump_pressed;
    int key_up_prev;
    int default_x;
    int default_y;

    void (*tick)(struct dave_struct *dave, tile_t map[TILEMAP_WIDTH * TILEMAP_HEIGHT],
        int left, int right, int up, int climb_up, int down, int jetpack);
    int (*is_dead)(struct dave_struct *dave);
} dave_t;

void dave_destroy(dave_t *dave);
dave_t* dave_create(soundfx_t *sfx, int x, int y);

#endif
