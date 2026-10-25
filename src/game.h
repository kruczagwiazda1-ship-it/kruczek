#pragma once
#include <stdint.h>
#include "gfx.h"

#define TVW 1280
#define TVH 720
#define PADW 854
#define PADH 480

enum {
    BTN_A = 1, BTN_B = 2, BTN_X = 4, BTN_Y = 8, BTN_L = 16, BTN_R = 32, BTN_ZL = 64, BTN_ZR = 128,
    BTN_PLUS = 256, BTN_MINUS = 512, BTN_UP = 1024, BTN_DOWN = 2048, BTN_LEFT = 4096, BTN_RIGHT = 8192
};

typedef struct {
    float lx, ly, rx, ry;      /* sticks, -1..1, y positive = down */
    uint32_t hold;             /* BTN_* held */
    int touching;              /* GamePad touch screen */
    float tx, ty;              /* touch position in GamePad pixels (854x480) */
} Input;

/* implemented by the platform layer */
int plat_load(void *buf, int size);
int plat_save(const void *buf, int size);

void game_init(void);
void game_update(const Input *in, float dt);
void game_render(Fb *tv, Fb *pad);
int  game_quit(void);
void game_save(void);
#ifdef KR_BOT
void game_bot(Input *in);
#endif
