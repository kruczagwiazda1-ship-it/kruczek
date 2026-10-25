/* Wii U platform layer (devkitPPC + wut).
 *  - TV and GamePad get *different* images: we render both with the software renderer and push the
 *    frames to the OSScreen buffers (SCREEN_TV / SCREEN_DRC).
 *  - Input: VPAD (GamePad sticks, buttons, touch screen).
 *  - Audio: SDL2 audio only (no SDL video).
 *
 * NOTE: OSScreen does not document where its work buffer lives, so at start-up we probe it by writing two
 * marker pixels with OSScreenPutPixelEx and finding them in the buffer. If probing fails we fall back to the
 * (slow) OSScreenPutPixelEx path. */
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <coreinit/cache.h>
#include <coreinit/memdefaultheap.h>
#include <coreinit/screen.h>
#include <coreinit/time.h>
#include <vpad/input.h>
#include <whb/proc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "game.h"
#include "audio.h"

/* Calibrated GamePad touch coordinates are reported in this space (scaled down to 854x480 below).
 * If touches land in the wrong place on real hardware, change these two numbers. */
#define TOUCH_SRC_W 1280.f
#define TOUCH_SRC_H 720.f

/* ---------------- save file ---------------- */
static const char *save_paths[] = {
    "fs:/vol/external01/wiiu/apps/kruczek/kruczek_save.dat",
    "/vol/external01/wiiu/apps/kruczek/kruczek_save.dat",
};
int plat_load(void *buf, int size) {
    for (unsigned i = 0; i < sizeof save_paths / sizeof *save_paths; i++) {
        FILE *f = fopen(save_paths[i], "rb"); if (!f) continue;
        int n = (int)fread(buf, 1, size, f); fclose(f); return n;
    }
    return 0;
}
int plat_save(const void *buf, int size) {
    mkdir("fs:/vol/external01/wiiu", 0777); mkdir("fs:/vol/external01/wiiu/apps", 0777); mkdir("fs:/vol/external01/wiiu/apps/kruczek", 0777);
    for (unsigned i = 0; i < sizeof save_paths / sizeof *save_paths; i++) {
        FILE *f = fopen(save_paths[i], "wb"); if (!f) continue;
        int n = (int)fwrite(buf, 1, size, f); fclose(f); return n;
    }
    return 0;
}

/* ---------------- screens ---------------- */
typedef struct {
    OSScreenID id; void *buf; uint32_t size;
    uint32_t *base[2]; int pitch, w, h, cur, ok;
} Scr;

static int probe_pass(Scr *s, uint32_t **base, int *pitch) {
    uint32_t *b = (uint32_t *)s->buf; uint32_t words = s->size / 4;
    memset(b, 0, s->size); DCFlushRange(b, s->size);
    OSScreenClearBufferEx(s->id, 0);
    OSScreenPutPixelEx(s->id, 5, 0, 0x11223300);
    OSScreenPutPixelEx(s->id, 0, 1, 0x44556600);
    int i1 = -1, i2 = -1;
    for (uint32_t i = 0; i < words; i++) {
        uint32_t v = b[i] >> 8;
        if (v == 0x112233 && i1 < 0) i1 = (int)i; else if (v == 0x445566 && i2 < 0) i2 = (int)i;
        if (i1 >= 0 && i2 >= 0) break;
    }
    if (i1 < 5 || i2 < 0) return 0;
    int b0 = i1 - 5, p = i2 - b0;
    if (p < 800 || p > 4096) return 0;
    *base = b + b0; *pitch = p; return 1;
}

static void screen_init(Scr *s, OSScreenID id, int w, int h) {
    memset(s, 0, sizeof *s); s->id = id;
    s->size = OSScreenGetBufferSizeEx(id);
    s->buf = MEMAllocFromDefaultHeapEx(s->size, 0x100);
    OSScreenSetBufferEx(id, s->buf);
    OSScreenEnableEx(id, 1);
    int p0 = 0, p1 = 0; uint32_t *b0 = NULL, *b1 = NULL;
    int ok0 = probe_pass(s, &b0, &p0);
    OSScreenFlipBuffersEx(id);
    int ok1 = probe_pass(s, &b1, &p1);
    OSScreenFlipBuffersEx(id);
    s->w = w; s->h = h;
    if (ok0 && ok1 && b0 != b1 && p0 == p1) {
        s->ok = 1; s->base[0] = b0; s->base[1] = b1; s->pitch = p0; s->cur = 0;
        if (id == SCREEN_TV && p0 >= 1900) { s->w = 1920; s->h = 1080; }
    }
    OSScreenClearBufferEx(id, 0); OSScreenFlipBuffersEx(id); OSScreenClearBufferEx(id, 0); OSScreenFlipBuffersEx(id);
}

/* Fast present: no per-pixel division. Column map is built once; rows that map to the same source row
 * are copied from the previous output row with memcpy. */
static int *xmap_for(const Scr *s, const Fb *fb, int *cache_w, int *cache_fw, int **cache) {
    if (*cache && *cache_w == s->w && *cache_fw == fb->w) return *cache;
    free(*cache); *cache = (int *)malloc(sizeof(int) * s->w);
    for (int x = 0; x < s->w; x++) (*cache)[x] = x * fb->w / s->w;
    *cache_w = s->w; *cache_fw = fb->w; return *cache;
}
static void screen_present(Scr *s, const Fb *fb) {
    if (s->ok) {
        static int *xm[2]; static int xw[2], xf[2];
        int k = (s->id == SCREEN_TV) ? 0 : 1;
        int *xmap = xmap_for(s, fb, &xw[k], &xf[k], &xm[k]);
        uint32_t *dst = s->base[s->cur];
        int prev = -1; uint32_t *pd = NULL;
        for (int y = 0; y < s->h; y++) {
            int sy = (y * fb->h) / s->h;
            uint32_t *d = dst + (size_t)y * s->pitch;
            if (sy == prev && pd) { memcpy(d, pd, (size_t)s->w * 4); }
            else {
                const uint32_t *row = fb->px + (size_t)sy * fb->w;
                if (s->w == fb->w) memcpy(d, row, (size_t)fb->w * 4);
                else for (int x = 0; x < s->w; x++) d[x] = row[xmap[x]];
            }
            prev = sy; pd = d;
        }
        DCFlushRange(dst, (uint32_t)s->pitch * s->h * 4);
        OSScreenFlipBuffersEx(s->id);
        s->cur ^= 1;
    } else { /* slow but safe fallback */
        for (int y = 0; y < fb->h; y++) for (int x = 0; x < fb->w; x++) OSScreenPutPixelEx(s->id, x, y, fb->px[y * fb->w + x]);
        OSScreenFlipBuffersEx(s->id);
    }
}

static void audio_cb(void *ud, Uint8 *stream, int len) { (void)ud; audio_mix((int16_t *)stream, len / 4); }

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    WHBProcInit();
    VPADInit();
    OSScreenInit();
    static Scr tvs, pads;
    screen_init(&tvs, SCREEN_TV, TVW, TVH);
    screen_init(&pads, SCREEN_DRC, PADW, PADH);

    SDL_Init(SDL_INIT_AUDIO);
    SDL_AudioSpec want, have; SDL_zero(want);
    want.freq = AUDIO_RATE; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 2048; want.callback = audio_cb;
    SDL_AudioDeviceID ad = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (ad) SDL_PauseAudioDevice(ad, 0);

    static Fb tv, pad;
    tv.w = TVW; tv.h = TVH; tv.px = (uint32_t *)malloc((size_t)TVW * TVH * 4);
    pad.w = PADW; pad.h = PADH; pad.px = (uint32_t *)malloc((size_t)PADW * PADH * 4);
    game_init();

    OSTime last = OSGetSystemTime();
    while (WHBProcIsRunning() && !game_quit()) {
        Input in; memset(&in, 0, sizeof in);
        VPADStatus st; VPADReadError err;
        VPADRead(VPAD_CHAN_0, &st, 1, &err);
        if (err == VPAD_READ_SUCCESS) {
            in.lx = st.leftStick.x; in.ly = -st.leftStick.y; in.rx = st.rightStick.x; in.ry = -st.rightStick.y;
            uint32_t h = st.hold;
            if (h & VPAD_BUTTON_A) in.hold |= BTN_A;
            if (h & VPAD_BUTTON_B) in.hold |= BTN_B;
            if (h & VPAD_BUTTON_X) in.hold |= BTN_X;
            if (h & VPAD_BUTTON_Y) in.hold |= BTN_Y;
            if (h & VPAD_BUTTON_L) in.hold |= BTN_L;
            if (h & VPAD_BUTTON_R) in.hold |= BTN_R;
            if (h & VPAD_BUTTON_ZL) in.hold |= BTN_ZL;
            if (h & VPAD_BUTTON_ZR) in.hold |= BTN_ZR;
            if (h & VPAD_BUTTON_PLUS) in.hold |= BTN_PLUS;
            if (h & VPAD_BUTTON_MINUS) in.hold |= BTN_MINUS;
            if (h & VPAD_BUTTON_UP) in.hold |= BTN_UP;
            if (h & VPAD_BUTTON_DOWN) in.hold |= BTN_DOWN;
            if (h & VPAD_BUTTON_LEFT) in.hold |= BTN_LEFT;
            if (h & VPAD_BUTTON_RIGHT) in.hold |= BTN_RIGHT;
            VPADTouchData calib;
            VPADGetTPCalibratedPoint(VPAD_CHAN_0, &calib, &st.tpNormal);
            if (calib.touched && calib.validity == VPAD_VALID) {
                in.touching = 1; in.tx = calib.x * (float)PADW / TOUCH_SRC_W; in.ty = calib.y * (float)PADH / TOUCH_SRC_H;
            }
        }
        OSTime now = OSGetSystemTime();
        float dt = (float)(now - last) / (float)OSTimerClockSpeed; last = now;
        game_update(&in, dt);
        /* TV every frame, GamePad every other frame (the pad image is smaller and changes less) */
        static unsigned frameNo = 0; frameNo++;
        game_render_tv(&tv);
        screen_present(&tvs, &tv);
        if ((frameNo & 1) == 0) { game_render_pad(&pad); screen_present(&pads, &pad); }
    }
    game_save();
    if (ad) SDL_CloseAudioDevice(ad);
    SDL_Quit();
    OSScreenShutdown();
    VPADShutdown();
    WHBProcShutdown();
    return 0;
}
