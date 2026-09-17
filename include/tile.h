#ifndef _TILE_H_
#define _TILE_H_

#include <stdio.h>
#include <stdint.h>

/*
 * Quick conversion between grid and pixel basis
 */
#define TILE_SIZE 16
#define TILEMAP_WIDTH 100
#define TILEMAP_HEIGHT 12

/*
 * Modifiers of tiles, sort of logical category/characteristic.
 * So for example dirt block and red brick block will be 'brick', or
 * vines and fire both will be fire as they cause dave to get burned by it
 */
enum {
    BRICK      = 2,
    LOOT       = 3,
    TROPHY     = 4,
    MOSS       = 5,
    CLIMB      = 6,
    FIRE       = 7,
    DOOR       = 8,
    DAVE       = 9,
    GUN        = 10,
    JETPACK    = 11,
    MONSTER    = 12
};


/*
 * Each index represent a sprite loaded from resource images
 */
enum {
    SPRITE_IDX_DIRT_BLOOD            = 1,
    SPRITE_IDX_DOOR                  = 2,
    SPRITE_IDX_METAL_BEAM            = 3,
    SPRITE_IDX_JETPACK               = 4,
    SPRITE_IDX_BLUE_BRICK            = 5,
    SPRITE_IDX_FIRE1                 = 6,
    SPRITE_IDX_FIRE2                 = 7,
    SPRITE_IDX_FIRE3                 = 8,
    SPRITE_IDX_FIRE4                 = 9,
    SPRITE_IDX_TROPHY2               = 10,
    SPRITE_IDX_TROPHY3               = 11,
    SPRITE_IDX_TROPHY4               = 12,
    SPRITE_IDX_TROPHY0               = 13,
    SPRITE_IDX_TROPHY1               = 14,
    SPRITE_IDX_PIPE_RIGHT            = 15,
    SPRITE_IDX_PIPE_DOWN             = 16,
    SPRITE_IDX_RED_BRICK             = 17,
    SPRITE_IDX_DIRT                  = 18,
    SPRITE_IDX_BLUE_COLUMN           = 19,
    SPRITE_IDX_GUN                   = 20,
    SPRITE_IDX_DIRT_BOTTOM_LEFT      = 21,
    SPRITE_IDX_DIRT_TOP_LEFT         = 22,
    SPRITE_IDX_DIRT_TOP_RIGHT        = 23,
    SPRITE_IDX_DIRT_BOTTOM_RIGHT     = 24,
    SPRITE_IDX_VINES1                = 25,
    SPRITE_IDX_VINES2                = 26,
    SPRITE_IDX_VINES3                = 27,
    SPRITE_IDX_VINES4                = 28,
    SPRITE_IDX_PURPLE_COLUMN         = 29,
    SPRITE_IDX_PURPLE_PLATFORM       = 30,
    SPRITE_IDX_PURPLE_FAKE           = 31,
    SPRITE_IDX_MOSS                  = 32,
    SPRITE_IDX_TRUNK                 = 33,
    SPRITE_IDX_TREE1                 = 34,
    SPRITE_IDX_TREE2                 = 35,
    SPRITE_IDX_WATER1                = 36,
    SPRITE_IDX_WATER2                = 37,
    SPRITE_IDX_WATER3                = 38,
    SPRITE_IDX_WATER4                = 39,
    SPRITE_IDX_WATER5                = 40,
    SPRITE_IDX_STARS                 = 41,
    SPRITE_IDX_STARS_MOON            = 42,
    SPRITE_IDX_TREE_CORNER_TOP_LEFT  = 43,
    SPRITE_IDX_TREE_CORNER_TOP_RIGHT = 44,
    SPRITE_IDX_TREE_CORNER_BOT_LEFT  = 45,
    SPRITE_IDX_TREE_CORNER_BOT_RIGHT = 46,
    SPRITE_IDX_TEAL_GEM              = 47,
    SPRITE_IDX_PURPLE_GEM            = 48,
    SPRITE_IDX_RED_GEM               = 49,
    SPRITE_IDX_CROWN                 = 50,
    SPRITE_IDX_RING                  = 51,
    SPRITE_IDX_SCEPTER               = 52,
    SPRITE_IDX_DAVE_RIGHT_HANDSFREE  = 53,
    SPRITE_IDX_DAVE_RIGHT_STAND      = 54,
    SPRITE_IDX_DAVE_RIGHT_SERIOUS    = 55,
    SPRITE_IDX_DAVE_FRONT            = 56,
    SPRITE_IDX_DAVE_LEFT_HANDSFREE   = 57,
    SPRITE_IDX_DAVE_LEFT_STAND       = 58,
    SPRITE_IDX_DAVE_LEFT_SERIOUS     = 59,
    SPRITE_IDX_DAVE_JUMP_RIGHT       = 67,
    SPRITE_IDX_DAVE_JUMP_LEFT        = 68,
    SPRITE_IDX_DAVE_CLIMB_HANDS_UP   = 71,
    SPRITE_IDX_DAVE_CLIMB_HAND_RIGHT = 72,
    SPRITE_IDX_DAVE_CLIMB_HAND_LEFT  = 73,
    SPRITE_IDX_DAVE_JETPACK_RIGHT1   = 77,
    SPRITE_IDX_DAVE_JETPACK_RIGHT2   = 78,
    SPRITE_IDX_DAVE_JETPACK_RIGHT3   = 79,
    SPRITE_IDX_DAVE_JETPACK_LEFT1    = 80,
    SPRITE_IDX_DAVE_JETPACK_LEFT2    = 81,
    SPRITE_IDX_DAVE_JETPACK_LEFT3    = 82,
    SPRITE_IDX_MONSTER_SPIDER1       = 89,
    SPRITE_IDX_MONSTER_SPIDER2       = 90,
    SPRITE_IDX_MONSTER_SPIDER3       = 91,
    SPRITE_IDX_MONSTER_SPIDER4       = 92,
    SPRITE_IDX_MONSTER_SWIRL1        = 93,
    SPRITE_IDX_MONSTER_SWIRL2        = 94,
    SPRITE_IDX_MONSTER_SWIRL3        = 95,
    SPRITE_IDX_MONSTER_SWIRL4        = 96,
    SPRITE_IDX_MONSTER_SUN1          = 97,
    SPRITE_IDX_MONSTER_SUN2          = 98,
    SPRITE_IDX_MONSTER_SUN3          = 99,
    SPRITE_IDX_MONSTER_SUN4          = 100,
    SPRITE_IDX_MONSTER_BONES1        = 101,
    SPRITE_IDX_MONSTER_BONES2        = 102,
    SPRITE_IDX_MONSTER_BONES3        = 103,
    SPRITE_IDX_MONSTER_BONES4        = 104,
    SPRITE_IDX_MONSTER_UFO1          = 105,
    SPRITE_IDX_MONSTER_UFO2          = 106,
    SPRITE_IDX_MONSTER_UFO3          = 107,
    SPRITE_IDX_MONSTER_UFO4          = 108,
    SPRITE_IDX_MONSTER_GUARD1        = 109,
    SPRITE_IDX_MONSTER_GUARD2        = 110,
    SPRITE_IDX_MONSTER_GUARD3        = 111,
    SPRITE_IDX_MONSTER_GUARD4        = 112,
    SPRITE_IDX_MONSTER_GREEN_DISK1   = 113,
    SPRITE_IDX_MONSTER_GREEN_DISK2   = 114,
    SPRITE_IDX_MONSTER_GREEN_DISK3   = 115,
    SPRITE_IDX_MONSTER_GREEN_DISK4   = 116,
    SPRITE_IDX_MONSTER_SILVER_DISK1  = 117,
    SPRITE_IDX_MONSTER_SILVER_DISK2  = 118,
    SPRITE_IDX_MONSTER_SILVER_DISK3  = 119,
    SPRITE_IDX_MONSTER_SILVER_DISK4  = 120,
    SPRITE_IDX_PLASMA_RIGHT1         = 121,
    SPRITE_IDX_PLASMA_RIGHT2         = 122,
    SPRITE_IDX_PLASMA_RIGHT3         = 123,
    SPRITE_IDX_PLASMA_LEFT1          = 124,
    SPRITE_IDX_PLASMA_LEFT2          = 125,
    SPRITE_IDX_PLASMA_LEFT3          = 126,
    SPRITE_IDX_BULLET_RIGHT          = 127,
    SPRITE_IDX_BULLET_LEFT           = 128,
    SPRITE_IDX_EXPLOSION1            = 129,
    SPRITE_IDX_EXPLOSION2            = 130,
    SPRITE_IDX_EXPLOSION3            = 131,
    SPRITE_IDX_EXPLOSION4            = 132,
    SPRITE_IDX_JETPACK_LABEL         = 133,
    SPRITE_IDX_GUN_BANNER            = 134,
    SPRITE_IDX_TROPHY_BANNER         = 138,
    SPRITE_IDX_LABEL_WARP            = 139,
    SPRITE_IDX_LABEL_ZONE            = 140,
    SPRITE_IDX_JETPACK_BAR_FRAME     = 141,
    SPRITE_IDX_JETPACK_BAR           = 142,
    SPRITE_IDX_TITLE_FLAMES1         = 144,
    SPRITE_IDX_TITLE_FLAMES2         = 145,
    SPRITE_IDX_TITLE_FLAMES3         = 146,
    SPRITE_IDX_TITLE_FLAMES4         = 147,
    SPRITE_IDX_POPUP_BOX_T1          = 158,
    SPRITE_IDX_POPUP_BOX_T2          = 159,
    SPRITE_IDX_POPUP_BOX_T3          = 160,
    SPRITE_IDX_POPUP_BOX_M1          = 161,
    SPRITE_IDX_POPUP_BOX_M2          = 162,
    SPRITE_IDX_POPUP_BOX_M3          = 163,
    SPRITE_IDX_POPUP_BOX_B1          = 164,
    SPRITE_IDX_POPUP_BOX_B2          = 165,
    SPRITE_IDX_POPUP_BOX_B3          = 166,
    SPRITE_IDX_CURSOR1               = 167,
    SPRITE_IDX_CURSOR2               = 168,
    SPRITE_IDX_CURSOR3               = 169,
    SPRITE_IDX_CURSOR4               = 170,
    SPRITE_IDX_BOTTOM_BAR            = 171,
    SPRITE_IDX_TOP_BAR               = 172
};

/* The sprite used as the game icon: Dave facing right with the gun. */
#define SPRITE_IDX_ICON SPRITE_IDX_DAVE_RIGHT_SERIOUS


/*
 * The tile is the basic building of tile-based graphics. Each tile will hold the information
 * of one game piece which can be interactive and can be idle, can animate or be idle,
 * can move and change its parameters.
 */
typedef struct tile_struct {
    /* Pointer to the owner of this tile */
    void *context;

    int x;
    int y;

    int width;
    int height;

    /*
     * The delta (difference) in x,y,w,h for the collision box, so for example when
     * all 0, it means collision box is exactly rect x,y,w,h. If we want the collision box 
     * to be bigger in 1 pixel, we will set dx = -1, dy = -1, dw = 2, dh = 2. Note the change in width
     * and height is 2 as we need eventually to add one pixel at each side 
     */
    int collision_dx;
    int collision_dy;
    int collision_dw;
    int collision_dh;

    int mod;
    int sprites[100];
    int sprite_idx;
    int score_value;

    /* A tick is a function owner of a tile calls so the tile can 'do its thing', whatever it might be. */
    void (*tick)(struct tile_struct *tile);

    /* Returns the sprite index to draw for this tile, 0 will indicate no need to draw anything */
    int (*get_sprite)(struct tile_struct *tile);

    /* Returns 1 if x,y is inside the tile's given rect: x,y,w,h */
    int (*is_inside)(struct tile_struct *tile, int x, int y);
} tile_t;

/* Create simple non-interacting block */
void tile_create_block(tile_t* t, int sprite, int x, int y, int width, int height);

/* Intro related */
void tile_create_intro_fire(tile_t* t, int x, int y);
void tile_create_intro_banner(tile_t* t, int x, int y);

void tile_create_plasma_right(tile_t *t, int x, int y, int width, int height);
void tile_create_plasma_left(tile_t *t, int x, int y, int width, int height);
/*
 * Tiles for special purpose
 */
void tile_create_grail_banner(tile_t *t, int x, int y);
void tile_create_gun_banner(tile_t *t, int x, int y);
void tile_create_label_warp(tile_t *t, int x, int y);
void tile_create_label_zone(tile_t *t, int x, int y);

/*
 * Popup box
 */
void tile_create_flashing_cursor(tile_t* t, int x, int y);

/* Game screen top & bottom bars */
void tile_create_bottom_separator(tile_t* t, int x, int y);
void tile_create_top_separator(tile_t* t, int x, int y);

void tile_create(tile_t* t, char tag[4], int x, int y);

#endif
