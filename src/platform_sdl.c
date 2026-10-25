/* Desktop simulator: two windows (TV + GamePad). Mouse on the GamePad window = touch screen.
 * Keys: WASD move, J/Space = A (dash), K = B (caw), Esc/P = + (menu), Left/Right or Q/E = prev/next building,
 *       Up/Down = menu, R = demolish, mouse on pad window = touch.  Build with:  make -f Makefile.desktop               */
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "audio.h"

#define SAVE_FILE "kruczek_save.dat"

int plat_load(void *buf, int size) {
    FILE *f = fopen(SAVE_FILE, "rb"); if (!f) return 0;
    int n = (int)fread(buf, 1, size, f); fclose(f); return n;
}
int plat_save(const void *buf, int size) {
    FILE *f = fopen(SAVE_FILE, "wb"); if (!f) return 0;
    int n = (int)fwrite(buf, 1, size, f); fclose(f); return n;
}

static void audio_cb(void *ud, Uint8 *stream, int len) { (void)ud; audio_mix((int16_t *)stream, len / 4); }

typedef struct { SDL_Window *w; SDL_Renderer *r; SDL_Texture *t; } Win;
static Win mkwin(const char *title, int w, int h, int x, int y, int iw, int ih, int headless) {
    Win o; o.w = SDL_CreateWindow(title, x, y, w, h, SDL_WINDOW_SHOWN);
    o.r = SDL_CreateRenderer(o.w, -1, headless ? SDL_RENDERER_SOFTWARE : 0);
    SDL_RenderSetLogicalSize(o.r, iw, ih);
    o.t = SDL_CreateTexture(o.r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, iw, ih);
    return o;
}
static void shot(const Fb *f, const char *path) {
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormatFrom(f->px, f->w, f->h, 32, f->w * 4, SDL_PIXELFORMAT_RGBA8888);
    SDL_SaveBMP(s, path); SDL_FreeSurface(s);
}

int main(int argc, char **argv) {
    int bot = 0, fast = 0, maxframes = 0; const char *shotdir = NULL; int shots[64], nshots = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--bot")) bot = 1;
        else if (!strcmp(argv[i], "--fast")) fast = 1;
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) maxframes = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--shotdir") && i + 1 < argc) shotdir = argv[++i];
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc && nshots < 64) shots[nshots++] = atoi(argv[++i]);
    }
    int headless = fast || getenv("SDL_VIDEODRIVER");
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    SDL_AudioSpec want, have; SDL_zero(want); want.freq = AUDIO_RATE; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 1024; want.callback = audio_cb;
    SDL_AudioDeviceID ad = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!fast) {
        if (ad) SDL_PauseAudioDevice(ad, 0);
    }
    Win tv = mkwin("Kruczek Survival - TV", 960, 540, 40, 60, TVW, TVH, headless);
    Win pad = mkwin("Kruczek Survival - GamePad (dotyk = mysz)", PADW, PADH, 1020, 100, PADW, PADH, headless);
    Uint32 padId = SDL_GetWindowID(pad.w);
    static Fb ftv, fpad;
    ftv.w = TVW; ftv.h = TVH; ftv.px = malloc(TVW * TVH * 4); fpad.w = PADW; fpad.h = PADH; fpad.px = malloc(PADW * PADH * 4);

    game_init();
    SDL_GameController *gc = NULL;
    for (int i = 0; i < SDL_NumJoysticks(); i++) if (SDL_IsGameController(i)) { gc = SDL_GameControllerOpen(i); break; }

    int running = 1, frame = 0; Uint64 last = SDL_GetPerformanceCounter();
    float mx = 0, my = 0; int mdown = 0;
    while (running && !game_quit()) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            if (e.type == SDL_MOUSEMOTION && e.motion.windowID == padId) { mx = (float)e.motion.x; my = (float)e.motion.y; }
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.windowID == padId) { mdown = 1; mx = (float)e.button.x; my = (float)e.button.y; }
            if (e.type == SDL_MOUSEBUTTONUP) mdown = 0;
        }
        Input in; memset(&in, 0, sizeof in);
        const Uint8 *k = SDL_GetKeyboardState(NULL);
        if (k[SDL_SCANCODE_A]) in.lx -= 1;
        if (k[SDL_SCANCODE_D]) in.lx += 1;
        if (k[SDL_SCANCODE_W]) in.ly -= 1;
        if (k[SDL_SCANCODE_S]) in.ly += 1;
        if (k[SDL_SCANCODE_J] || k[SDL_SCANCODE_SPACE]) in.hold |= BTN_A;
        if (k[SDL_SCANCODE_K]) in.hold |= BTN_B;
        if (k[SDL_SCANCODE_ESCAPE] || k[SDL_SCANCODE_P]) in.hold |= BTN_PLUS;
        if (k[SDL_SCANCODE_LEFT] || k[SDL_SCANCODE_Q]) in.hold |= BTN_LEFT;
        if (k[SDL_SCANCODE_RIGHT] || k[SDL_SCANCODE_E]) in.hold |= BTN_RIGHT;
        if (k[SDL_SCANCODE_R]) in.hold |= BTN_Y;
        if (k[SDL_SCANCODE_RETURN]) in.hold |= BTN_A;
        if (gc) {
            in.lx += SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX) / 32767.f; in.ly += SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY) / 32767.f;
            in.rx = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_RIGHTX) / 32767.f; in.ry = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_RIGHTY) / 32767.f;
            if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_A)) in.hold |= BTN_A;
            if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_B)) in.hold |= BTN_B;
            if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_START)) in.hold |= BTN_PLUS;
            if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_UP)) in.hold |= BTN_UP;
            if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_DOWN)) in.hold |= BTN_DOWN;
            if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_LEFT)) in.hold |= BTN_LEFT;
            if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) in.hold |= BTN_RIGHT;
            if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) in.hold |= BTN_R;
        }
        if (in.lx > 1) in.lx = 1;
        if (in.lx < -1) in.lx = -1;
        if (in.ly > 1) in.ly = 1;
        if (in.ly < -1) in.ly = -1;
        in.touching = mdown; in.tx = mx; in.ty = my;
        /* menu navigation with keys (arrows already map to move; use dedicated keys for menu) */
        if (k[SDL_SCANCODE_UP]) in.hold |= BTN_UP;
        if (k[SDL_SCANCODE_DOWN]) in.hold |= BTN_DOWN;
#ifdef KR_BOT
        if (bot) game_bot(&in);
#endif
        float dt;
        if (fast) dt = 1.f / 60.f;
        else { Uint64 now = SDL_GetPerformanceCounter(); dt = (float)(now - last) / SDL_GetPerformanceFrequency(); last = now; }
        if (fast) { /* feed audio mixer so the synth state advances (and to test for crashes) */
            static int16_t tmp[2048]; for (int i = 0; i < AUDIO_RATE / 60; i += 512) audio_mix(tmp, 512);
        }
        game_update(&in, dt);
        game_render(&ftv, &fpad);
        if (!fast || (frame % 20) == 0) {
            SDL_UpdateTexture(tv.t, NULL, ftv.px, TVW * 4); SDL_RenderClear(tv.r); SDL_RenderCopy(tv.r, tv.t, NULL, NULL); SDL_RenderPresent(tv.r);
            SDL_UpdateTexture(pad.t, NULL, fpad.px, PADW * 4); SDL_RenderClear(pad.r); SDL_RenderCopy(pad.r, pad.t, NULL, NULL); SDL_RenderPresent(pad.r);
        }
        for (int i = 0; i < nshots; i++) if (shots[i] == frame && shotdir) {
            char p[256]; snprintf(p, sizeof p, "%s/tv_%05d.bmp", shotdir, frame); shot(&ftv, p);
            snprintf(p, sizeof p, "%s/pad_%05d.bmp", shotdir, frame); shot(&fpad, p);
        }
        frame++;
        if (maxframes && frame >= maxframes) break;
    }
    game_save();
    SDL_Quit();
    return 0;
}
