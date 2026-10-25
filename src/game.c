/* KRUCZEK SURVIVAL - two-screen survival/base builder for Wii U.
 * TV      = the battlefield (Kruczek fights waves).
 * GamePad = touch map of the same arena + build menu. Whatever you place on the
 *           map appears in the same spot on the TV; loot picked up on the TV
 *           falls onto the GamePad and has to be caught with a tap.            */
#include "game.h"
#include "audio.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PI 3.14159265f

/* ---------------- layout ---------------- */
#define AX 40
#define AY 140
#define AW 1200
#define AH 500
#define GCOLS 12
#define GROWS 5
#define CELLW 100.f
#define MX 17.f
#define MY 56.f
#define MS 0.68333f
#define BAR_Y 406.f
#define BAR_H 66.f

enum { R_STICK, R_STONE, R_SHINY, R_N };
enum { B_HOLE, B_HOUSE, B_PICNIC, B_FIRE, B_TOWER, B_GARDEN, B_N };
enum { E_RAT, E_SNAKE, E_CAT, E_FOX };
enum { MODE_TITLE, MODE_PLAY, MODE_PAUSE, MODE_HELP };
enum { WS_BREAK, WS_FIGHT, WS_CLEAR };

static const char *BNAME[B_N] = { "Hole", "House", "Picnic", "Campfire", "Tower", "Garden" };
static const char *BDESC[B_N] = {
    "Trap: enemies get stuck and take damage. Sometimes digs up loot!",
    "+25 max health for Kruczek",
    "Kruczek heals while standing on the blanket",
    "Attacks faster nearby and burns enemies",
    "Shoots nearby enemies on its own",
    "Grows loot - pick it up on the TV" };
static const int BCOST[B_N][R_N] = { { 4, 0, 0 }, { 12, 6, 0 }, { 6, 0, 3 }, { 5, 8, 0 }, { 10, 12, 3 }, { 8, 2, 2 } };
static const int BREQ[B_N] = { 0, 1, 2, 3, 4, 6 }; /* waves cleared to unlock */
static const uint32_t RCOL[R_N] = { RGB(176, 118, 66), RGB(158, 168, 180), RGB(255, 208, 64) };

/* ---------------- data ---------------- */
typedef struct {
    uint32_t magic; int32_t res[R_N]; int8_t grid[GROWS][GCOLS];
    int32_t wave, cleared, sound, music;
    int8_t soil[GROWS][GCOLS];      /* rich soil cells - drawn only on the TV */
    int32_t tutDone;
} Save;
#define SAVE_MAGIC 0x4B525A34u

typedef struct {
    float x, y, hp, maxhp, face, iframes, dashT, dashCd, cawCd, atkCd, fireBuff, anim, flash, dx, dy;
    int moving;
} Player;
typedef struct {
    int alive, type; float x, y, vx, vy, hp, maxhp, r, flash, t, stuck, holeImm, face, burn;
    int st; float stT, dirx, diry; float seg[7][2]; float emerge;
} Enemy;
typedef struct { int alive, type; float x, y, t, total; } Warn;      /* burrow warning: GamePad shows it early */
typedef struct { int alive; float x, y, prog; } Treasure;            /* buried loot: GamePad shows the X, TV shows digging */
typedef struct { int alive, kind; float x, y, vx, vy, dmg, life; int pierce; } Shot;
typedef struct { int alive, kind; float x, y, life, t; } Drop;
typedef struct { int alive, kind, state; float x, y, vy, ttl, t, fx, fy; } Fall;
typedef struct { int alive, pad; float x, y, vx, vy, life, max, size, grav; uint32_t col; } Part;
typedef struct { int alive, pad, size; float x, y, vy, life, max; uint32_t col; char txt[40]; } FText;

#define NE 90
#define NS 80
#define ND 64
#define NF 40
#define NP 500
#define NT 40
#define NW 24
#define NTR 3

static struct {
    Save sv; int hasSave;
    int mode, msel, quit;
    Player p; Enemy en[NE]; Shot sh[NS]; Drop dr[ND]; Fall fl[NF]; Part pt[NP]; FText ft[NT]; Warn wn[NW]; Treasure tr[NTR];
    float ct[GROWS][GCOLS]; float cpop[GROWS][GCOLS];
    int wstate; float wtimer, spawnT; int toSpawn, bossLeft, enemiesSeen;
    float dead, bannerT, unlockT, infoT, hurtFlash, cawFx, tvTime, padTime, saveT, holeSnd, hintCount;
    char banner[48]; int bannerBoss; int unlockB; int sel, demolish;
    int combo; float comboT;
    float cx, cy, curShow; float lastTouchX, lastTouchY, touchFlash; int lastCellR, lastCellC;
    uint32_t prevHold; int prevTouch; int firstTouchFrame;
    int maxhp;
    int tut; float tutT, tutDist; int tutKills, tutBuilt, tutDug, tutCaught; int helpFrom;
} G;

/* ---------------- helpers ---------------- */
static float frand(void) { return rand() / (float)RAND_MAX; }
static float frange(float a, float b) { return a + (b - a) * frand(); }
static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static float cellcx(int c) { return AX + (c + .5f) * CELLW; }
static float cellcy(int r) { return AY + (r + .5f) * CELLW; }
static float padx(float x) { return MX + (x - AX) * MS; }
static float pady(float y) { return MY + (y - AY) * MS; }
static float easeback(float t) { if (t >= 1.f) return 1.f; float c1 = 1.9f, c3 = c1 + 1.f; float u = t - 1.f; return 1.f + c3 * u * u * u + c1 * u * u; }
static int unlocked(int b) { return G.sv.cleared >= BREQ[b]; }
static int count_b(int b) { int n = 0; for (int r = 0; r < GROWS; r++) for (int c = 0; c < GCOLS; c++) if (G.sv.grid[r][c] == b) n++; return n; }

static void add_part(int pad, float x, float y, float vx, float vy, float life, float size, uint32_t col, float grav) {
    for (int i = 0; i < NP; i++) if (!G.pt[i].alive) { Part *p = &G.pt[i]; p->alive = 1; p->pad = pad; p->x = x; p->y = y; p->vx = vx; p->vy = vy; p->life = p->max = life; p->size = size; p->col = col; p->grav = grav; return; }
}
static void burst(int pad, float x, float y, int n, float spd, uint32_t col, float size, float life) {
    for (int i = 0; i < n; i++) { float a = frand() * 2 * PI, s = frange(.3f, 1.f) * spd; add_part(pad, x, y, cosf(a) * s, sinf(a) * s, life * frange(.6f, 1.f), size * frange(.6f, 1.2f), col, 0); }
}
static void add_ft(int pad, float x, float y, const char *txt, uint32_t col, int size) {
    for (int i = 0; i < NT; i++) if (!G.ft[i].alive) { FText *f = &G.ft[i]; f->alive = 1; f->pad = pad; f->x = x; f->y = y; f->vy = -45; f->life = f->max = 1.2f; f->col = col; f->size = size; snprintf(f->txt, sizeof f->txt, "%s", txt); return; }
}
static void banner(const char *s, int boss) { snprintf(G.banner, sizeof G.banner, "%s", s); G.bannerT = 2.6f; G.bannerBoss = boss; }

/* ---------------- save / load ---------------- */
void game_save(void) {
    G.sv.sound = G.sv.sound ? 1 : 0;
    plat_save(&G.sv, (int)sizeof(Save)); G.hasSave = 1;
}
static void new_game_data(void) {
    int snd = G.sv.sound, mus = G.sv.music;
    memset(&G.sv, 0, sizeof G.sv); G.sv.magic = SAVE_MAGIC;
    memset(G.sv.grid, -1, sizeof G.sv.grid);
    G.sv.res[R_STICK] = 6; G.sv.wave = 1; G.sv.cleared = 0; G.sv.sound = snd; G.sv.music = mus;
    for (int n = 0; n < 9; ) { int r = rand() % GROWS, c = rand() % GCOLS; if (G.sv.soil[r][c]) continue; G.sv.soil[r][c] = 1; n++; }
}
static int rich_at(int r, int c) { return G.sv.soil[r][c] != 0; }

/* ---------------- resources ---------------- */
static int can_afford(int b) { for (int i = 0; i < R_N; i++) if (G.sv.res[i] < BCOST[b][i]) return 0; return 1; }

/* ---------------- spawn ---------------- */
static float hp_scale(void) { return 1.f + 0.11f * (G.sv.wave - 1); }
static void spawn_enemy_at(int type, float x, float y) {
    for (int i = 0; i < NE; i++) if (!G.en[i].alive) {
        Enemy *e = &G.en[i]; memset(e, 0, sizeof *e); e->alive = 1; e->type = type;
        e->x = x; e->y = y; e->face = 1; e->emerge = .6f;
        switch (type) {
            case E_RAT:   e->maxhp = 14; e->r = 15; break;
            case E_SNAKE: e->maxhp = 9;  e->r = 12; break;
            case E_CAT:   e->maxhp = 36; e->r = 21; break;
            default:      e->maxhp = 260; e->r = 40; break;
        }
        e->maxhp *= (type == E_FOX ? 1.f + 0.16f * (G.sv.wave / 5 - 1) : hp_scale()); e->hp = e->maxhp;
        for (int k = 0; k < 7; k++) { e->seg[k][0] = x; e->seg[k][1] = y; }
        e->t = frand() * 10; e->stT = 2.5f;
        return;
    }
}
/* Enemies come out of fixed burrows INSIDE the arena. The burrows are only drawn on the GamePad. */
#define NBURROW 8
static const float BURROW[NBURROW][2] = {
    { AX + 90, AY + 80 }, { AX + AW / 2, AY + 70 }, { AX + AW - 90, AY + 80 }, { AX + 80, AY + AH / 2 },
    { AX + AW - 80, AY + AH / 2 }, { AX + 90, AY + AH - 70 }, { AX + AW / 2, AY + AH - 60 }, { AX + AW - 90, AY + AH - 70 } };
static void queue_spawn(int type, float delay) {
    int best = -1;
    for (int tries = 0; tries < 12; tries++) {
        int k = rand() % NBURROW; float dx = BURROW[k][0] - G.p.x, dy = BURROW[k][1] - G.p.y;
        if (dx * dx + dy * dy > 260.f * 260.f) { best = k; break; }
        if (best < 0) best = k;
    }
    for (int i = 0; i < NW; i++) if (!G.wn[i].alive) {
        Warn *w = &G.wn[i]; w->alive = 1; w->type = type; w->t = w->total = delay;
        w->x = BURROW[best][0] + frange(-18, 18); w->y = BURROW[best][1] + frange(-14, 14); return;
    }
}
static int warns_left(void) { int n = 0; for (int i = 0; i < NW; i++) if (G.wn[i].alive) n++; return n; }
static int treasures_left(void) { int n = 0; for (int i = 0; i < NTR; i++) if (G.tr[i].alive) n++; return n; }
static void spawn_treasures(int want) {
    while (treasures_left() < want) {
        int slot = -1; for (int i = 0; i < NTR; i++) if (!G.tr[i].alive) { slot = i; break; }
        if (slot < 0) return;
        float x = 0, y = 0;
        for (int tries = 0; tries < 30; tries++) {
            x = frange(AX + 110, AX + AW - 110); y = frange(AY + 90, AY + AH - 80);
            float dx = x - G.p.x, dy = y - G.p.y; int ok = dx * dx + dy * dy > 220.f * 220.f;
            for (int j = 0; j < NTR; j++) if (G.tr[j].alive) { float ox = x - G.tr[j].x, oy = y - G.tr[j].y; if (ox * ox + oy * oy < 200.f * 200.f) ok = 0; }
            if (ok) break;
        }
        G.tr[slot].alive = 1; G.tr[slot].x = x; G.tr[slot].y = y; G.tr[slot].prog = 0;
    }
}
static void spawn_drop(int kind, float x, float y, float life) {
    for (int i = 0; i < ND; i++) if (!G.dr[i].alive) { Drop *d = &G.dr[i]; d->alive = 1; d->kind = kind; d->x = x; d->y = y; d->life = life; d->t = frand() * 6; return; }
}
static void spawn_shot(int kind, float x, float y, float tx, float ty, float dmg, float spd) {
    for (int i = 0; i < NS; i++) if (!G.sh[i].alive) {
        Shot *s = &G.sh[i]; float dx = tx - x, dy = ty - y, d = sqrtf(dx * dx + dy * dy) + 1e-4f;
        s->alive = 1; s->kind = kind; s->x = x; s->y = y; s->vx = dx / d * spd; s->vy = dy / d * spd; s->dmg = dmg; s->life = 1.4f; s->pierce = kind == 0 ? 1 : 1; return;
    }
}
static void spawn_fall(int kind, float tvx) {
    for (int i = 0; i < NF; i++) if (!G.fl[i].alive) {
        Fall *f = &G.fl[i]; memset(f, 0, sizeof *f); f->alive = 1; f->kind = kind;
        f->x = clampf(padx(tvx), 34, PADW - 34); f->y = -14; f->vy = 40; f->fy = 372 + frand() * 22; return;
    }
}

/* ---------------- game events ---------------- */
static void drop_loot(Enemy *e) {
    float x = e->x, y = e->y; int n = 0;
    switch (e->type) {
        case E_RAT:   if (frand() < .78f) { spawn_drop(R_STICK, x, y, 14); n++; } break;
        case E_SNAKE: if (frand() < .4f) spawn_drop(R_SHINY, x, y, 14); if (frand() < .3f) spawn_drop(R_STICK, x + 12, y, 14); break;
        case E_CAT:   if (frand() < .75f) spawn_drop(R_STONE, x, y, 14); if (frand() < .15f) spawn_drop(R_SHINY, x + 14, y, 14); break;
        default:
            for (int i = 0; i < 5; i++) spawn_drop(R_STICK, x + frange(-50, 50), y + frange(-40, 40), 18);
            for (int i = 0; i < 5; i++) spawn_drop(R_STONE, x + frange(-50, 50), y + frange(-40, 40), 18);
            for (int i = 0; i < 3; i++) spawn_drop(R_SHINY, x + frange(-50, 50), y + frange(-40, 40), 18);
            break;
    }
    (void)n;
    if (frand() < .06f) spawn_drop(3, x - 10, y + 8, 12);
}
static void kill_enemy(Enemy *e) {
    e->alive = 0; G.tutKills++; sfx_play(S_KILL, 1.f);
    uint32_t col = e->type == E_CAT ? RGB(240, 150, 70) : e->type == E_SNAKE ? RGB(110, 190, 90) : e->type == E_FOX ? RGB(230, 110, 50) : RGB(160, 160, 170);
    burst(0, e->x, e->y, e->type == E_FOX ? 40 : 14, 220, col, 5.5f, .6f);
    add_ft(0, e->x, e->y - e->r, e->type == E_FOX ? "FOX DEFEATED!" : "poof!", RGB(255, 255, 255), e->type == E_FOX ? 30 : 20);
    drop_loot(e);
}
static void damage_enemy(Enemy *e, float dmg, float kx, float ky) {
    e->hp -= dmg; e->flash = .12f; e->vx += kx; e->vy += ky;
    if (e->hp <= 0 && e->alive) kill_enemy(e);
}
static void die(void) {
    G.dead = 3.4f; sfx_play(S_FAIL, 1.f);
    burst(0, G.p.x, G.p.y, 40, 260, RGB(40, 44, 70), 6, .9f);
    for (int i = 0; i < NE; i++) G.en[i].alive = 0;
    for (int i = 0; i < NW; i++) G.wn[i].alive = 0;
    for (int i = 0; i < NS; i++) G.sh[i].alive = 0;
    for (int i = 0; i < ND; i++) G.dr[i].alive = 0;
    for (int i = 0; i < R_N; i++) G.sv.res[i] -= G.sv.res[i] / 4;
    add_ft(0, G.p.x, G.p.y - 40, "Kruczek is down!", RGB(255, 120, 120), 34);
}
static void hurt_player(float dmg) {
    Player *p = &G.p; if (p->iframes > 0 || p->dashT > 0 || G.dead > 0) return;
    p->hp -= dmg; p->iframes = .8f; p->flash = .2f; G.hurtFlash = .35f; sfx_play(S_HURT, 1.f);
    burst(0, p->x, p->y, 8, 160, RGB(255, 80, 80), 4, .4f);
    char b[16]; snprintf(b, sizeof b, "-%d", (int)dmg); add_ft(0, p->x, p->y - 30, b, RGB(255, 110, 110), 26);
    if (p->hp <= 0) { p->hp = 0; die(); }
}

static void start_wave(void) {
    int w = G.sv.wave; G.wstate = WS_FIGHT;
    G.toSpawn = 8 + 4 * w; if (G.toSpawn > 70) G.toSpawn = 70;
    G.bossLeft = (w % 5 == 0) ? 1 : 0; G.spawnT = 1.0f;
    char b[48]; snprintf(b, sizeof b, G.bossLeft ? "WAVE %d - WATCH OUT: FOX!" : "WAVE %d", w);
    banner(b, G.bossLeft); sfx_play(G.bossLeft ? S_BOSS : S_WAVE, 1.f);
}
static void wave_cleared(void) {
    int old = G.sv.cleared;
    if (G.sv.wave > G.sv.cleared) G.sv.cleared = G.sv.wave;
    G.sv.wave++; G.wstate = WS_BREAK; G.wtimer = 9.f;
    G.p.hp = fminf(G.maxhp, G.p.hp + G.maxhp * .35f); sfx_play(S_HEAL, 1.f); spawn_treasures(2);
    for (int b = 0; b < B_N; b++) if (BREQ[b] > old && BREQ[b] <= G.sv.cleared) { G.unlockB = b; G.unlockT = 4.5f; sfx_play(S_UNLOCK, 1.f); }
    game_save();
}

/* ---------------- building ---------------- */
static void try_build(int r, int c) {
    float cx = cellcx(c), cy = cellcy(r);
    if (G.demolish) {
        int b = G.sv.grid[r][c];
        if (b >= 0) {
            for (int i = 0; i < R_N; i++) G.sv.res[i] += BCOST[b][i] / 2;
            G.sv.grid[r][c] = -1; sfx_play(S_TICK, 1.f); burst(0, cx, cy, 14, 160, RGB(190, 160, 120), 5, .5f); burst(1, padx(cx), pady(cy), 10, 90, RGB(190, 160, 120), 3, .5f);
            add_ft(1, padx(cx), pady(cy) - 10, "removed", RGB(255, 255, 255), 16); game_save();
        }
        return;
    }
    if (G.sv.grid[r][c] >= 0) { sfx_play(S_DENY, 1.f); add_ft(1, padx(cx), pady(cy), "occupied", RGB(255, 140, 140), 16); return; }
    int b = G.sel;
    if (!unlocked(b)) { sfx_play(S_DENY, 1.f); add_ft(1, padx(cx), pady(cy), "locked", RGB(255, 140, 140), 16); return; }
    if (!can_afford(b)) { sfx_play(S_DENY, 1.f); add_ft(1, padx(cx), pady(cy), "not enough loot", RGB(255, 140, 140), 16); return; }
    for (int i = 0; i < R_N; i++) G.sv.res[i] -= BCOST[b][i];
    G.sv.grid[r][c] = (int8_t)b; G.cpop[r][c] = 0; G.ct[r][c] = (b == B_HOLE) ? frange(8, 14) : (b == B_GARDEN ? 3.f : 0.4f);
    sfx_play(S_BUILD, 1.f); G.tutBuilt = 1;
    if (rich_at(r, c)) { add_ft(1, padx(cx), pady(cy) - 38, "Rich soil bonus!", RGB(255, 226, 110), 17); add_ft(0, cx, cy - 78, "Rich soil bonus!", RGB(255, 226, 110), 22); }
    burst(0, cx, cy + 10, 18, 190, RGB(210, 180, 130), 6, .6f); burst(1, padx(cx), pady(cy) + 6, 12, 110, RGB(210, 180, 130), 3.5f, .5f);
    add_ft(0, cx, cy - 50, "NEW!", RGB(255, 235, 120), 26); add_ft(1, padx(cx), pady(cy) - 20, "built!", RGB(255, 235, 120), 16);
    game_save();
}

static void catch_fall(Fall *f) {
    int inair = f->state == 0; G.tutCaught = 1;
    int amt = 1;
    G.combo++; G.comboT = 6.f;
    if (G.combo >= 5) { amt++; }
    G.sv.res[f->kind] += amt;
    f->state = 2; f->t = 0; f->fx = f->x; f->fy = f->y;
    if (inair) { sfx_play(S_SPARK, .9f); add_ft(1, f->x, f->y - 24, "CAUGHT IN MID-AIR!", RGB(255, 255, 140), 18); }
    sfx_play(S_COIN, 1.f);
    if (G.combo >= 5) { char b[32]; snprintf(b, sizeof b, "COMBO x%d  +1", G.combo); add_ft(1, f->x, f->y - 46, b, RGB(255, 170, 80), 20); }
    burst(1, f->x, f->y, 8, 100, RCOL[f->kind], 3.5f, .4f);
}

/* ---------------- menus ---------------- */
static int menu_count(void) { return G.mode == MODE_TITLE ? 6 : 5; }
static void menu_label(int i, char *out, int n) {
    if (G.mode == MODE_TITLE) {
        switch (i) {
            case 0: snprintf(out, n, "%s", G.hasSave ? "Continue" : "Play"); break;
            case 1: snprintf(out, n, "New game"); break;
            case 2: snprintf(out, n, "How to play"); break;
            case 3: snprintf(out, n, "Sound: %s", G.sv.sound ? "ON" : "OFF"); break;
            case 4: snprintf(out, n, "Music: %s", G.sv.music ? "ON" : "OFF"); break;
            default: snprintf(out, n, "Quit"); break;
        }
    } else {
        switch (i) {
            case 0: snprintf(out, n, "Resume"); break;
            case 1: snprintf(out, n, "How to play"); break;
            case 2: snprintf(out, n, "Sound: %s", G.sv.sound ? "ON" : "OFF"); break;
            case 3: snprintf(out, n, "Music: %s", G.sv.music ? "ON" : "OFF"); break;
            default: snprintf(out, n, "Save and exit to menu"); break;
        }
    }
}
static void reset_world(void) {
    memset(G.en, 0, sizeof G.en); memset(G.sh, 0, sizeof G.sh); memset(G.dr, 0, sizeof G.dr);
    memset(G.fl, 0, sizeof G.fl); memset(G.pt, 0, sizeof G.pt); memset(G.ft, 0, sizeof G.ft);
    memset(G.ct, 0, sizeof G.ct); memset(G.cpop, 0, sizeof G.cpop); memset(G.wn, 0, sizeof G.wn); memset(G.tr, 0, sizeof G.tr);
    memset(&G.p, 0, sizeof G.p); G.p.x = AX + AW / 2; G.p.y = AY + AH / 2; G.p.face = 1;
    G.maxhp = 100; G.p.hp = 100; G.p.maxhp = 100;
    G.wstate = WS_BREAK; G.wtimer = 8.f; G.dead = 0; G.unlockT = 0; G.combo = 0; G.sel = 0; G.demolish = 0;
    G.cx = PADW / 2; G.cy = 200;
}
static void begin_play(int fresh) {
    reset_world(); G.mode = MODE_PLAY;
    G.tut = G.sv.tutDone ? 6 : 0; G.tutT = 0; G.tutDist = 0; G.tutKills = 0; G.tutBuilt = 0; G.tutDug = 0; G.tutCaught = 0;
    if (G.tut == 6) { spawn_treasures(2); banner(fresh ? "New game - good luck!" : "Welcome back!", 0); }
}
static void menu_activate(int i) {
    sfx_play(S_UI, 1.f);
    if (G.mode == MODE_TITLE) {
        if (i == 0) begin_play(0);
        else if (i == 1) { new_game_data(); game_save(); begin_play(1); }
        else if (i == 2) { G.helpFrom = MODE_TITLE; G.mode = MODE_HELP; }
        else if (i == 3) { G.sv.sound = !G.sv.sound; audio_set_enabled(G.sv.sound, G.sv.music); }
        else if (i == 4) { G.sv.music = !G.sv.music; audio_set_enabled(G.sv.sound, G.sv.music); }
        else { game_save(); G.quit = 1; }
    } else {
        if (i == 0) G.mode = MODE_PLAY;
        else if (i == 1) { G.helpFrom = MODE_PAUSE; G.mode = MODE_HELP; }
        else if (i == 2) { G.sv.sound = !G.sv.sound; audio_set_enabled(G.sv.sound, G.sv.music); }
        else if (i == 3) { G.sv.music = !G.sv.music; audio_set_enabled(G.sv.sound, G.sv.music); }
        else { game_save(); G.mode = MODE_TITLE; G.msel = 0; }
    }
}
#define MENU_X 237.f
#define MENU_W 380.f
#define MENU_H 46.f
static float menu_y(int i) { return 88.f + i * (MENU_H + 9.f); }
#define HELPBTN_X 327.f
#define HELPBTN_Y 408.f
#define HELPBTN_W 200.f
#define HELPBTN_H 46.f

/* ---------------- pad pointer (touch or cursor+R) ---------------- */
#define BTN_W 112.f
#define BTN_GAP 5.f
static float btn_x(int i) { return 12.f + i * (BTN_W + BTN_GAP); }
#define DEMO_X (12.f + 6 * (112.f + 5.f))
#define MENUBTN_X (DEMO_X + 64.f + 5.f)

static void pad_pointer(float x, float y) {
    G.lastTouchX = x; G.lastTouchY = y; G.touchFlash = .35f;
    if (G.mode == MODE_HELP) {
        if (x >= HELPBTN_X && x <= HELPBTN_X + HELPBTN_W && y >= HELPBTN_Y && y <= HELPBTN_Y + HELPBTN_H) { G.mode = G.helpFrom; sfx_play(S_UI, 1.f); }
        return;
    }
    if (G.mode != MODE_PLAY) {
        for (int i = 0; i < menu_count(); i++) if (x >= MENU_X && x <= MENU_X + MENU_W && y >= menu_y(i) && y <= menu_y(i) + MENU_H) { G.msel = i; menu_activate(i); return; }
        return;
    }
    /* 1. catch falling loot (highest priority) */
    int best = -1; float bd = 46 * 46;
    for (int i = 0; i < NF; i++) if (G.fl[i].alive && G.fl[i].state < 2) {
        float dx = G.fl[i].x - x, dy = G.fl[i].y - y, d = dx * dx + dy * dy; if (d < bd) { bd = d; best = i; }
    }
    if (best >= 0) { catch_fall(&G.fl[best]); return; }
    /* 2. bottom bar */
    if (y >= BAR_Y && y <= BAR_Y + BAR_H) {
        for (int i = 0; i < B_N; i++) if (x >= btn_x(i) && x <= btn_x(i) + BTN_W) {
            G.sel = i; G.demolish = 0; G.infoT = 3.5f; sfx_play(S_UI, 1.f);
            return;
        }
        if (x >= DEMO_X && x <= DEMO_X + 64) { G.demolish = !G.demolish; G.infoT = 2.5f; sfx_play(S_UI, 1.f); return; }
        if (x >= MENUBTN_X && x <= MENUBTN_X + 56) { G.mode = MODE_PAUSE; G.msel = 0; sfx_play(S_UI, 1.f); return; }
        return;
    }
    /* 3. map */
    int c = (int)((x - MX) / (CELLW * MS)), r = (int)((y - MY) / (CELLW * MS));
    if (x >= MX && y >= MY && c >= 0 && c < GCOLS && r >= 0 && r < GROWS) { G.lastCellR = r; G.lastCellC = c; try_build(r, c); }
}

/* ---------------- update ---------------- */
static int edge(const Input *in, uint32_t b) { return (in->hold & b) && !(G.prevHold & b); }

static Enemy *nearest_enemy(float x, float y, float range) {
    Enemy *best = NULL; float bd = range * range;
    for (int i = 0; i < NE; i++) if (G.en[i].alive && G.en[i].emerge <= 0) {
        float dx = G.en[i].x - x, dy = G.en[i].y - y, d = dx * dx + dy * dy; if (d < bd) { bd = d; best = &G.en[i]; }
    }
    return best;
}

static void update_buildings(float dt) {
    Player *p = &G.p;
    float heal = 0; G.p.fireBuff -= dt; if (G.p.fireBuff < 0) G.p.fireBuff = 0;
    G.holeSnd -= dt;
    for (int r = 0; r < GROWS; r++) for (int c = 0; c < GCOLS; c++) {
        int b = G.sv.grid[r][c]; if (G.cpop[r][c] < 1.f) G.cpop[r][c] += dt * 3.f;
        if (b < 0) continue;
        float cx = cellcx(c), cy = cellcy(r);
        G.ct[r][c] -= dt;
        switch (b) {
            case B_HOLE:
                for (int i = 0; i < NE; i++) {
                    Enemy *e = &G.en[i]; if (!e->alive) continue;
                    float dx = e->x - cx, dy = e->y - cy;
                    if (dx * dx + dy * dy < 34 * 34 && e->holeImm <= 0 && e->stuck <= 0 && e->type != E_FOX) {
                        e->stuck = 1.3f; e->holeImm = 3.2f;
                        if (G.holeSnd <= 0) { sfx_play(S_TRAP, 1.f); G.holeSnd = .6f; }
                        add_ft(0, cx, cy - 26, "Gotcha!", RGB(200, 255, 150), 20); burst(0, cx, cy, 8, 90, RGB(120, 90, 60), 4, .5f);
                    }
                    if (e->stuck > 0 && dx * dx + dy * dy < 40 * 40) { e->hp -= 5 * dt; if (e->hp <= 0) kill_enemy(e); }
                }
                if (G.ct[r][c] <= 0) {
                    G.ct[r][c] = frange(16, 24) * (rich_at(r, c) ? .5f : 1.f); float q = frand();
                    spawn_drop(q < .55f ? R_STONE : (q < .8f ? R_SHINY : R_STICK), cx, cy - 4, 14);
                    sfx_play(S_DIG, .8f); burst(0, cx, cy, 14, 140, RGB(130, 96, 62), 5, .6f); add_ft(0, cx, cy - 30, "Dug something up!", RGB(255, 235, 150), 18);
                }
                break;
            case B_PICNIC: { float dx = p->x - cx, dy = p->y - cy; if (dx * dx + dy * dy < 70 * 70 && G.dead <= 0) heal += rich_at(r, c) ? 12 : 7; } break;
            case B_FIRE: {
                float dx = p->x - cx, dy = p->y - cy; if (dx * dx + dy * dy < 120 * 120) p->fireBuff = .25f;
                for (int i = 0; i < NE; i++) { Enemy *e = &G.en[i]; if (!e->alive) continue; float ex = e->x - cx, ey = e->y - cy; if (ex * ex + ey * ey < 78 * 78) { e->hp -= (rich_at(r, c) ? 12 : 8) * dt; e->burn = .1f; if (e->hp <= 0) kill_enemy(e); } }
                if (frand() < dt * 6) add_part(0, cx + frange(-8, 8), cy - 4, frange(-10, 10), -frange(40, 90), .7f, 3.5f, RGB(255, 190, 60), 0);
            } break;
            case B_TOWER:
                if (G.ct[r][c] <= 0) {
                    Enemy *e = nearest_enemy(cx, cy - 20, 330);
                    if (e) { G.ct[r][c] = .85f; spawn_shot(1, cx, cy - 30, e->x, e->y, rich_at(r, c) ? 11 : 7, 640); sfx_play(S_SHOOT, .5f); }
                    else G.ct[r][c] = .2f;
                }
                break;
            case B_GARDEN:
                if (G.ct[r][c] <= 0) {
                    G.ct[r][c] = rich_at(r, c) ? 4.f : 8.f; float q = frand();
                    spawn_drop(q < .5f ? R_STICK : (q < .8f ? R_STONE : R_SHINY), cx + frange(-14, 14), cy + 6, 14);
                    sfx_play(S_TICK, 1.f); burst(0, cx, cy, 8, 80, RGB(130, 220, 120), 4, .5f);
                }
                break;
        }
    }
    if (heal > 0) {
        G.p.hp = fminf(G.maxhp, G.p.hp + heal * dt);
        if (frand() < dt * 5) add_part(0, p->x + frange(-14, 14), p->y - 10, 0, -50, .8f, 4, RGB(255, 120, 150), 0);
    }
}

static void update_play(const Input *in, float dt) {
    Player *p = &G.p;
    int houses = count_b(B_HOUSE); G.maxhp = 100 + 25 * houses;
    for (int r = 0; r < GROWS; r++) for (int c = 0; c < GCOLS; c++) if (G.sv.grid[r][c] == B_HOUSE && rich_at(r, c)) G.maxhp += 25;
    if (G.maxhp > (int)p->maxhp && p->hp > 0) p->hp += G.maxhp - p->maxhp;
    p->maxhp = G.maxhp; if (p->hp > p->maxhp) p->hp = p->maxhp;

    if (G.dead > 0) {
        G.dead -= dt;
        if (G.dead <= 0) { p->hp = p->maxhp; p->x = AX + AW / 2; p->y = AY + AH / 2; p->iframes = 1.5f; G.wstate = WS_BREAK; G.wtimer = 6.f; banner("Again!", 0); }
    } else {
        /* movement */
        float mx = in->lx, my = in->ly;
        float mag = sqrtf(mx * mx + my * my); if (mag < .18f) { mx = my = 0; mag = 0; } else if (mag > 1) { mx /= mag; my /= mag; mag = 1; }
        p->moving = mag > 0;
        if (p->moving) { p->dx = mx / (mag > 0 ? mag : 1); p->dy = my / (mag > 0 ? mag : 1); if (fabsf(mx) > .2f) p->face = mx > 0 ? 1 : -1; }
        float spd = 250;
        if (p->dashT > 0) { p->dashT -= dt; mx = p->dx; my = p->dy; spd = 800; if (frand() < .8f) add_part(0, p->x, p->y + 6, frange(-20, 20), frange(-20, 20), .3f, 6, RGBA(255, 255, 255, 150), 0); }
        G.tutDist += (fabsf(mx) + fabsf(my)) * spd * dt * .7f;
        p->x += mx * spd * dt; p->y += my * spd * dt;
        p->x = clampf(p->x, AX + 24, AX + AW - 24); p->y = clampf(p->y, AY + 30, AY + AH - 16);
        p->dashCd -= dt; p->cawCd -= dt; p->atkCd -= dt; p->iframes -= dt; p->flash -= dt;
        if (edge(in, BTN_A) && p->dashCd <= 0) {
            p->dashCd = 1.2f; p->dashT = .18f; p->iframes = fmaxf(p->iframes, .25f); sfx_play(S_DASH, 1.f);
            if (!p->moving) { p->dx = p->face; p->dy = 0; }
        }
        if (edge(in, BTN_B) && p->cawCd <= 0) {
            p->cawCd = 6.f; G.cawFx = .55f; sfx_play(S_CAW, 1.f);
            for (int i = 0; i < NE; i++) if (G.en[i].alive && G.en[i].emerge <= 0) {
                Enemy *e = &G.en[i]; float dx = e->x - p->x, dy = e->y - p->y, d = sqrtf(dx * dx + dy * dy) + 1e-3f;
                if (d < 200) { damage_enemy(e, e->type == E_FOX ? 6 : 14, dx / d * 520, dy / d * 520); e->stuck = 0; }
            }
        }
        /* auto-attack */
        float cd = .5f / (G.p.fireBuff > 0 ? 1.6f : 1.f);
        if (p->atkCd <= 0) {
            Enemy *e = nearest_enemy(p->x, p->y, 290);
            if (e) { spawn_shot(0, p->x + p->face * 10, p->y - 6, e->x, e->y, 9 + 0.0f, 580); p->atkCd = cd; sfx_play(S_SHOOT, .35f); }
        }
    }
    p->anim += dt;

    /* waves */
    if (G.dead <= 0) {
        if (G.wstate == WS_BREAK) {
            if (G.tut < 3) G.wtimer = 9.f;                     /* tutorial: the wave waits for you */
            G.wtimer -= dt;
            if (G.wtimer <= 0) start_wave();
        } else if (G.wstate == WS_FIGHT) {
            G.spawnT -= dt;
            if (G.spawnT <= 0 && G.toSpawn > 0) {
                int w = G.sv.wave; float q = frand(); int t;
                if (w < 2) t = q < .7f ? E_RAT : E_SNAKE;
                else if (w < 4) t = q < .4f ? E_RAT : (q < .65f ? E_SNAKE : E_CAT);
                else t = q < .3f ? E_RAT : (q < .55f ? E_SNAKE : E_CAT);
                queue_spawn(t, 2.0f); G.toSpawn--;
                G.spawnT = fmaxf(.32f, 1.25f - .05f * w) * frange(.7f, 1.3f);
                if (G.bossLeft && G.toSpawn < (8 + 4 * w) / 2) { queue_spawn(E_FOX, 3.0f); G.bossLeft = 0; add_ft(0, AX + AW / 2, AY + 60, "THE FOX IS COMING!", RGB(255, 160, 80), 40); }
            }
            if (G.toSpawn <= 0 && !G.bossLeft) G.wstate = WS_CLEAR;
        } else {
            int alive = warns_left(); for (int i = 0; i < NE; i++) if (G.en[i].alive) alive++;
            if (alive == 0) wave_cleared();
        }
    }

    /* burrow warnings */
    for (int i = 0; i < NW; i++) if (G.wn[i].alive) {
        Warn *w = &G.wn[i]; w->t -= dt;
        if (w->t < .9f && frand() < dt * 28) add_part(0, w->x + frange(-16, 16), w->y + 4, frange(-30, 30), -frange(30, 90), .5f, 4, RGB(150, 112, 74), 0);
        if (w->t <= 0) { w->alive = 0; spawn_enemy_at(w->type, w->x, w->y); burst(0, w->x, w->y, 12, 150, RGB(140, 104, 68), 5, .5f); sfx_play(S_TRAP, .5f); }
    }
    /* buried treasure: the GamePad shows where it is, Kruczek digs it up on the TV */
    for (int i = 0; i < NTR; i++) if (G.tr[i].alive) {
        Treasure *tr = &G.tr[i]; float dx = p->x - tr->x, dy = p->y - tr->y;
        if (dx * dx + dy * dy < 44.f * 44.f && G.dead <= 0) {
            tr->prog += dt / 1.3f;
            if (frand() < dt * 20) add_part(0, tr->x + frange(-14, 14), tr->y + 6, frange(-50, 50), -frange(40, 110), .5f, 4, RGB(150, 112, 74), 0);
            if (G.holeSnd <= 0) { sfx_play(S_DIG, .5f); G.holeSnd = .35f; }
            if (tr->prog >= 1.f) {
                tr->alive = 0; G.tutDug = 1; sfx_play(S_UNLOCK, .8f);
                int n = 4 + rand() % 3;
                for (int k = 0; k < n; k++) spawn_drop(k % 3, tr->x + frange(-30, 30), tr->y + frange(-24, 24), 16);
                spawn_drop(3, tr->x, tr->y + 20, 14);
                burst(0, tr->x, tr->y, 24, 220, RGB(255, 214, 90), 5, .7f); add_ft(0, tr->x, tr->y - 36, "Treasure!", RGB(255, 226, 110), 32);
                add_ft(1, padx(tr->x), pady(tr->y) - 14, "found!", RGB(255, 226, 110), 18);
            }
        } else tr->prog = fmaxf(0.f, tr->prog - dt * .5f);
    }

    /* tutorial */
    if (G.tut < 6) {
        G.tutT += dt;
        int adv = 0;
        switch (G.tut) {
            case 0: adv = G.tutDist > 260; break;
            case 1: adv = G.tutBuilt; break;
            case 2: adv = G.tutDug; break;
            case 3: adv = G.tutKills >= 4; break;
            case 4: adv = G.tutCaught; break;
            case 5: if (G.tutT > 6.f) { G.sv.tutDone = 1; G.tut = 6; game_save(); } break;
        }
        if (adv) {
            G.tut++; G.tutT = 0; sfx_play(S_UNLOCK, .6f);
            if (G.tut == 2) spawn_treasures(1);
            if (G.tut == 3) { G.wtimer = 1.5f; }
            if (G.tut == 5) { spawn_treasures(2); }
        }
    }

    /* enemies */
    for (int i = 0; i < NE; i++) {
        Enemy *e = &G.en[i]; if (!e->alive) continue;
        if (e->emerge > 0) { e->emerge -= dt; continue; }        /* still climbing out of the burrow: harmless */
        e->t += dt; e->flash -= dt; e->holeImm -= dt; e->burn -= dt;
        float dx = p->x - e->x, dy = p->y - e->y, d = sqrtf(dx * dx + dy * dy) + 1e-3f;
        float ux = dx / d, uy = dy / d;
        float spd = 0, dmg = 0;
        switch (e->type) { case E_RAT: spd = 98; dmg = 7; break; case E_SNAKE: spd = 150; dmg = 5; break; case E_CAT: spd = 78; dmg = 11; break; default: spd = 86; dmg = 20; break; }
        dmg *= 1.f + 0.04f * (G.sv.wave - 1);
        float mvx = 0, mvy = 0;
        if (e->stuck > 0) e->stuck -= dt;
        else if (G.dead <= 0) {
            if (e->type == E_FOX) {
                e->stT -= dt;
                if (e->st == 0) { mvx = ux * spd; mvy = uy * spd; if (e->stT <= 0) { e->st = 1; e->stT = .85f; e->dirx = ux; e->diry = uy; sfx_play(S_BOSS, .6f); } }
                else if (e->st == 1) { e->dirx = ux * .1f + e->dirx * .9f; e->diry = uy * .1f + e->diry * .9f; if (e->stT <= 0) { e->st = 2; e->stT = .6f; } }
                else { mvx = e->dirx * 560; mvy = e->diry * 560; if (e->stT <= 0) { e->st = 0; e->stT = 3.2f; } }
            } else {
                mvx = ux * spd; mvy = uy * spd;
                if (e->type == E_SNAKE) { float w = sinf(e->t * 7.f) * 95; mvx += -uy * w; mvy += ux * w; }
            }
        }
        e->x += (mvx + e->vx) * dt; e->y += (mvy + e->vy) * dt;
        e->vx -= e->vx * 6 * dt; e->vy -= e->vy * 6 * dt;
        if (fabsf(mvx) > 4) e->face = mvx > 0 ? 1 : -1;
        if (e->type == E_SNAKE) {
            float hx = e->x, hy = e->y;
            for (int s = 0; s < 7; s++) {
                float sx = e->seg[s][0] - hx, sy = e->seg[s][1] - hy, sd = sqrtf(sx * sx + sy * sy) + 1e-3f, want = 9.f;
                if (sd > want) { e->seg[s][0] = hx + sx / sd * want; e->seg[s][1] = hy + sy / sd * want; }
                hx = e->seg[s][0]; hy = e->seg[s][1];
            }
        }
        /* separation */
        for (int j = i + 1; j < NE; j++) {
            Enemy *o = &G.en[j]; if (!o->alive) continue;
            float ox = o->x - e->x, oy = o->y - e->y, od = sqrtf(ox * ox + oy * oy) + 1e-3f, mind = (e->r + o->r) * .8f;
            if (od < mind) { float push = (mind - od) * .5f; e->x -= ox / od * push; e->y -= oy / od * push; o->x += ox / od * push; o->y += oy / od * push; }
        }
        e->x = clampf(e->x, AX + e->r * .8f, AX + AW - e->r * .8f); e->y = clampf(e->y, AY + e->r * .8f, AY + AH - e->r * .6f);
        /* contact */
        if (G.dead <= 0 && d < e->r + 13) {
            hurt_player(dmg); e->vx -= ux * 260; e->vy -= uy * 260; if (e->type != E_FOX) e->stuck = 0;
        }
    }

    /* shots */
    for (int i = 0; i < NS; i++) {
        Shot *s = &G.sh[i]; if (!s->alive) continue;
        s->x += s->vx * dt; s->y += s->vy * dt; s->life -= dt; if (s->life <= 0) { s->alive = 0; continue; }
        for (int j = 0; j < NE; j++) {
            Enemy *e = &G.en[j]; if (!e->alive || e->emerge > 0) continue;
            float dx = e->x - s->x, dy = e->y - s->y;
            if (dx * dx + dy * dy < (e->r + 7) * (e->r + 7)) {
                float sp = sqrtf(s->vx * s->vx + s->vy * s->vy) + 1e-3f;
                damage_enemy(e, s->dmg, s->vx / sp * 90, s->vy / sp * 90);
                burst(0, s->x, s->y, 4, 110, s->kind ? RGB(180, 220, 255) : RGB(255, 255, 255), 3, .25f);
                if (frand() < .4f) sfx_play(S_HIT, .5f);
                if (--s->pierce <= 0) { s->alive = 0; }
                break;
            }
        }
    }

    /* drops */
    float magnet = 66;
    for (int i = 0; i < ND; i++) {
        Drop *d = &G.dr[i]; if (!d->alive) continue;
        d->t += dt; d->life -= dt; if (d->life <= 0) { d->alive = 0; continue; }
        float dx = p->x - d->x, dy = p->y - d->y, dd = sqrtf(dx * dx + dy * dy);
        if (dd < magnet && G.dead <= 0) { d->x += dx / (dd + 1e-3f) * 380 * dt; d->y += dy / (dd + 1e-3f) * 380 * dt; }
        if (dd < 24 && G.dead <= 0) {
            d->alive = 0;
            if (d->kind == 3) { p->hp = fminf(p->maxhp, p->hp + 14); sfx_play(S_HEAL, .8f); add_ft(0, p->x, p->y - 30, "+14", RGB(255, 130, 160), 24); }
            else {
                spawn_fall(d->kind, d->x); sfx_play(S_TICK, 1.f); burst(0, d->x, d->y, 6, 100, RCOL[d->kind], 3, .35f);
                if (G.hintCount < 3) { G.hintCount++; add_ft(0, d->x, d->y - 28, "falls to the GamePad!", RGB(180, 230, 255), 22); }
            }
        }
    }

    /* falling loot on the pad */
    for (int i = 0; i < NF; i++) {
        Fall *f = &G.fl[i]; if (!f->alive) continue;
        if (f->state == 0) {
            f->vy += 520 * dt; if (f->vy > 560) f->vy = 560; f->y += f->vy * dt;
            if (f->y >= f->fy) { f->y = f->fy; f->state = 1; f->ttl = 5.5f; sfx_play(S_TICK, .6f); burst(1, f->x, f->y + 10, 4, 60, RCOL[f->kind], 2.5f, .3f); }
            else if (frand() < .6f) add_part(1, f->x + frange(-4, 4), f->y - 10, 0, -20, .3f, 3, WITH_A(RCOL[f->kind], 150), 0);
        } else if (f->state == 1) {
            f->ttl -= dt; f->t += dt;
            if (f->ttl <= 0) {
                f->alive = 0; G.combo = 0;
                if (frand() < .5f) { G.sv.res[f->kind]++; add_ft(1, f->x, f->y - 18, "half", RGB(255, 220, 150), 15); }
                else add_ft(1, f->x, f->y - 18, "it broke", RGB(255, 140, 140), 15);
                burst(1, f->x, f->y, 8, 90, RCOL[f->kind], 3, .4f);
            }
        } else { f->t += dt * 3.5f; if (f->t >= 1) f->alive = 0; }
    }
    if (G.comboT > 0) { G.comboT -= dt; if (G.comboT <= 0) G.combo = 0; }

    update_buildings(dt);
}

static void update_common(const Input *in, float dt) {
    for (int i = 0; i < NP; i++) if (G.pt[i].alive) { Part *q = &G.pt[i]; q->life -= dt; if (q->life <= 0) { q->alive = 0; continue; } q->vy += q->grav * dt; q->x += q->vx * dt; q->y += q->vy * dt; q->vx *= 1 - 2 * dt; q->vy *= 1 - 1.5f * dt; }
    for (int i = 0; i < NT; i++) if (G.ft[i].alive) { FText *t = &G.ft[i]; t->life -= dt; t->y += t->vy * dt; t->vy *= 1 - 2 * dt; if (t->life <= 0) t->alive = 0; }
    G.bannerT -= dt; G.unlockT -= dt; G.infoT -= dt; G.hurtFlash -= dt; G.cawFx -= dt; G.touchFlash -= dt; G.curShow -= dt;
    G.tvTime += dt; G.padTime += dt;
    (void)in;
}

void game_update(const Input *in, float dt) {
    /* adaptive quality: if frames take > 45 ms for ~1.5 s, drop ink outlines; restore when comfortably fast */
    { static float avg = .016f, slowT = 0, fastT = 0;
      avg += (dt - avg) * .1f;
      if (avg > .045f) { slowT += dt; fastT = 0; } else if (avg < .028f) { fastT += dt; slowT = 0; } else { slowT = fastT = 0; }
      if (slowT > 1.5f && !gfx_lowq()) { gfx_set_lowq(1); slowT = 0; }
      if (fastT > 6.f && gfx_lowq()) { gfx_set_lowq(0); fastT = 0; } }
    if (dt > .05f) dt = .05f;
    /* pad cursor (right stick) + press */
    float rx = in->rx, ry = in->ry;
    if (fabsf(rx) > .2f || fabsf(ry) > .2f) { G.cx = clampf(G.cx + rx * 480 * dt, 0, PADW); G.cy = clampf(G.cy + ry * 480 * dt, 0, PADH); G.curShow = 4.f; }
    int touchEdge = in->touching && !G.prevTouch;
    if (touchEdge) { G.curShow = 0; pad_pointer(in->tx, in->ty); }
    if (edge(in, BTN_R) || edge(in, BTN_ZR)) { G.curShow = 4.f; pad_pointer(G.cx, G.cy); }

    if (G.mode == MODE_PLAY) {
        if (edge(in, BTN_LEFT)) { G.sel = (G.sel + B_N - 1) % B_N; G.demolish = 0; G.infoT = 3.5f; sfx_play(S_UI, 1.f); }
        if (edge(in, BTN_RIGHT)) { G.sel = (G.sel + 1) % B_N; G.demolish = 0; G.infoT = 3.5f; sfx_play(S_UI, 1.f); }
        if (edge(in, BTN_DOWN) || edge(in, BTN_Y)) { G.demolish = !G.demolish; G.infoT = 2.5f; sfx_play(S_UI, 1.f); }
        if (edge(in, BTN_PLUS)) { G.mode = MODE_PAUSE; G.msel = 0; sfx_play(S_UI, 1.f); }
        else update_play(in, dt);
        G.saveT += dt; if (G.saveT > 30) { G.saveT = 0; game_save(); }
    } else if (G.mode == MODE_HELP) {
        if (edge(in, BTN_A) || edge(in, BTN_B) || edge(in, BTN_PLUS)) { G.mode = G.helpFrom; sfx_play(S_UI, 1.f); }
    } else {
        int n = menu_count();
        if (edge(in, BTN_UP)) { G.msel = (G.msel + n - 1) % n; sfx_play(S_TICK, 1.f); }
        if (edge(in, BTN_DOWN)) { G.msel = (G.msel + 1) % n; sfx_play(S_TICK, 1.f); }
        if (edge(in, BTN_A)) menu_activate(G.msel);
        if (G.mode == MODE_PAUSE && (edge(in, BTN_PLUS) || edge(in, BTN_B))) G.mode = MODE_PLAY;
    }
    update_common(in, dt);
    {   float inten = G.mode != MODE_PLAY ? .25f : (G.dead > 0 ? 0.f : (G.wstate == WS_BREAK ? .2f : 1.f));
        audio_set_intensity(inten); }
    G.prevHold = in->hold; G.prevTouch = in->touching;
}

int game_quit(void) { return G.quit; }

/* =====================================================================
 *                              RENDERING
 * ===================================================================== */
static Fb bg_tv, bg_pad, bg_padm;
#define WHITE RGB(255, 255, 255)
#define INK RGB(34, 22, 28)
#define CREAM RGB(246, 230, 196)
#define PANEL RGB(58, 42, 40)
#define PANEL_D RGB(40, 28, 30)
#define ACCENT RGB(236, 140, 56)
static void ink(float s) { gfx_set_outline(fmaxf(1.15f, 2.2f * s), INK); }
static void ink_off(void) { gfx_set_outline(0, 0); }
static uint32_t fl(uint32_t c, float k) { return k > 0 ? gfx_mix(c, WHITE, k > 1 ? 1 : k) : c; }
static void shadow(Fb *f, float x, float y, float rx, float ry) { if (gfx_lowq()) return; fb_ellipse(f, x, y, rx, ry, 0, RGBA(0, 0, 0, 60)); }

static void draw_heart(Fb *f, float x, float y, float s, uint32_t c) {
    ink(s * .8f);
    fb_circle(f, x - 4.5f * s, y - 3 * s, 5.2f * s, c); fb_circle(f, x + 4.5f * s, y - 3 * s, 5.2f * s, c);
    fb_tri(f, x - 9.4f * s, y - 1.2f * s, x + 9.4f * s, y - 1.2f * s, x, y + 9.5f * s, c);
    ink_off();
}
static void draw_res(Fb *f, int k, float x, float y, float s, float t) {
    if (k == 3) { draw_heart(f, x, y, s, RGB(232, 74, 90)); return; }
    ink(s * .9f);
    if (k == R_STICK) {
        fb_line(f, x - 9 * s, y + 6 * s, x + 9 * s, y - 6 * s, 5 * s, RCOL[0]);
        fb_line(f, x - 3 * s, y + 2 * s, x + 2 * s, y - 9 * s, 3 * s, RGB(140, 92, 52));
        fb_circle(f, x + 9 * s, y - 6 * s, 2.7f * s, RGB(210, 156, 100));
    } else if (k == R_STONE) {
        fb_ellipse(f, x, y + s, 11 * s, 8.5f * s, .2f, RGB(112, 120, 132));
        fb_ellipse(f, x - 1.5f * s, y - s, 8.5f * s, 6 * s, .2f, RCOL[1]);
        fb_circle(f, x - 4 * s, y - 3 * s, 2 * s, RGBA(255, 255, 255, 150));
    } else if (k == R_SHINY) {
        fb_tri(f, x, y - 11 * s, x + 9 * s, y, x, y + 11 * s, RGB(255, 214, 72));
        fb_tri(f, x, y - 11 * s, x - 9 * s, y, x, y + 11 * s, RGB(255, 236, 140));
        fb_tri(f, x, y - 11 * s, x + 3 * s, y, x, y + 11 * s, RGB(235, 170, 30));
        float sp = .5f + .5f * sinf(t * 5.f); fb_circle(f, x - 3 * s, y - 4 * s, (1.5f + 1.2f * sp) * s, RGBA(255, 255, 255, 200));
    }
    ink_off();
}

static void draw_building(Fb *f, int b, float x, float y, float s, float t) {
    ink(s);
    switch (b) {
    case B_HOLE:
        fb_ellipse(f, x, y + 8 * s, 34 * s, 20 * s, 0, RGB(126, 92, 58));
        fb_ellipse(f, x, y + 9 * s, 29 * s, 16 * s, 0, RGB(78, 54, 34));
        fb_ellipse(f, x, y + 11 * s, 24 * s, 12 * s, 0, RGB(30, 20, 14));
        for (int i = 0; i < 6; i++) { float a = i * 1.05f + .4f; fb_circle(f, x + cosf(a) * 33 * s, y + 8 * s + sinf(a) * 19 * s, 5 * s, RGB(150, 112, 74)); }
        break;
    case B_HOUSE:
        shadow(f, x, y + 30 * s, 36 * s, 8 * s);
        fb_rrect(f, x - 27 * s, y - 6 * s, 54 * s, 38 * s, 5 * s, RGB(242, 224, 178));
        fb_rect(f, x + 13 * s, y - 36 * s, 9 * s, 16 * s, RGB(158, 70, 50));
        fb_tri(f, x - 36 * s, y - 4 * s, x + 36 * s, y - 4 * s, x, y - 36 * s, RGB(196, 88, 62));
        fb_tri(f, x - 36 * s, y - 4 * s, x, y - 4 * s, x, y - 36 * s, RGB(214, 106, 76));
        fb_rrect(f, x - 7 * s, y + 8 * s, 14 * s, 24 * s, 5 * s, RGB(120, 76, 50));
        fb_circle(f, x + 17 * s, y + 8 * s, 5.5f * s, RGB(255, 226, 120)); fb_ring(f, x + 17 * s, y + 8 * s, 5.5f * s, 1.5f * s, RGB(150, 100, 60));
        fb_circle(f, x - 17 * s, y + 8 * s, 5.5f * s, RGB(255, 226, 120));
        break;
    case B_PICNIC: {
        shadow(f, x, y + 30 * s, 34 * s, 6 * s);
        fb_rrect(f, x - 34 * s, y - 12 * s, 68 * s, 44 * s, 6 * s, RGB(218, 74, 74));
        for (int i = 0; i < 4; i++) for (int j = 0; j < 3; j++) if ((i + j) % 2 == 0)
            fb_rect(f, x - 34 * s + i * 17 * s + (i == 0 ? 1.f * s : 0), y - 12 * s + j * 14.7f * s + (j == 0 ? 1.f * s : 0), 17 * s - (i == 0 || i == 3 ? 1.f * s : 0), 14.7f * s - (j == 0 || j == 2 ? 1.f * s : 0), RGB(255, 240, 238));
        fb_rrect(f, x - 4 * s, y - 14 * s, 26 * s, 20 * s, 5 * s, RGB(170, 118, 70));
        fb_ring(f, x + 9 * s, y - 15 * s, 8 * s, 2.4f * s, RGB(128, 88, 52));
        fb_circle(f, x - 16 * s, y + 6 * s, 6 * s, RGB(232, 52, 62)); fb_circle(f, x - 18 * s, y + 4 * s, 1.8f * s, RGBA(255, 255, 255, 170));
        fb_circle(f, x + 20 * s, y + 14 * s, 5 * s, RGB(255, 214, 90));
        break; }
    case B_FIRE: {
        float fl1 = sinf(t * 9.f), fl2 = sinf(t * 13.f + 1.f);
        fb_circle(f, x, y, 44 * s, RGBA(255, 160, 40, 30)); fb_circle(f, x, y, 30 * s, RGBA(255, 190, 70, 40));
        for (int i = 0; i < 8; i++) { float a = i * .785f; fb_circle(f, x + cosf(a) * 24 * s, y + 10 * s + sinf(a) * 12 * s, 6.5f * s, RGB(128, 134, 146)); fb_circle(f, x + cosf(a) * 24 * s - 1.5f * s, y + 8 * s + sinf(a) * 12 * s, 4.5f * s, RGB(166, 172, 184)); }
        fb_line(f, x - 15 * s, y + 14 * s, x + 14 * s, y + 2 * s, 8 * s, RGB(110, 70, 40)); fb_line(f, x - 14 * s, y + 2 * s, x + 15 * s, y + 14 * s, 8 * s, RGB(130, 84, 48));
        fb_spike(f, x, y + 8 * s, (34 + 5 * fl1) * s, 24 * s, -PI / 2, RGB(255, 132, 40));
        fb_spike(f, x + 3 * s, y + 8 * s, (24 + 4 * fl2) * s, 14 * s, -PI / 2 + .15f, RGB(255, 200, 70));
        fb_spike(f, x, y + 8 * s, 14 * s, 8 * s, -PI / 2, RGB(255, 245, 190));
        break; }
    case B_TOWER:
        shadow(f, x, y + 32 * s, 26 * s, 7 * s);
        fb_rrect(f, x - 18 * s, y - 32 * s, 36 * s, 66 * s, 4 * s, RGB(158, 166, 180));
        fb_rect(f, x - 18 * s, y - 32 * s, 8 * s, 66 * s, RGB(176, 184, 198));
        for (int i = 0; i < 4; i++) fb_line(f, x - 18 * s, y - 14 * s + i * 14 * s, x + 18 * s, y - 14 * s + i * 14 * s, 1.2f * s, RGBA(80, 88, 100, 120));
        for (int i = 0; i < 3; i++) fb_rrect(f, x - 22 * s + i * 16 * s, y - 44 * s, 12 * s, 14 * s, 2 * s, RGB(142, 150, 164));
        fb_rrect(f, x - 3.5f * s, y - 16 * s, 7 * s, 16 * s, 3 * s, RGB(40, 46, 58));
        fb_line(f, x, y - 44 * s, x, y - 66 * s, 2.4f * s, RGB(90, 70, 50));
        fb_tri(f, x, y - 66 * s, x + 18 * s, y - 60 * s + sinf(t * 4) * 1.5f * s, x, y - 53 * s, RGB(222, 70, 70));
        break;
    case B_GARDEN:
        shadow(f, x, y + 32 * s, 36 * s, 6 * s);
        fb_rrect(f, x - 36 * s, y - 6 * s, 72 * s, 38 * s, 9 * s, RGB(122, 84, 54)); fb_rrect(f, x - 32 * s, y - 2 * s, 64 * s, 30 * s, 7 * s, RGB(98, 66, 42));
        for (int i = 0; i < 4; i++) {
            float px = x - 24 * s + i * 16 * s, sw = sinf(t * 2.f + i) * 1.5f * s;
            fb_line(f, px, y + 22 * s, px + sw, y - 4 * s, 3 * s, RGB(76, 150, 70));
            fb_ellipse(f, px - 6 * s + sw, y + 4 * s, 7 * s, 3.4f * s, -.5f, RGB(96, 176, 84)); fb_ellipse(f, px + 6 * s + sw, y + 8 * s, 7 * s, 3.4f * s, .5f, RGB(96, 176, 84));
            uint32_t fc = (i % 2) ? RGB(255, 120, 150) : RGB(255, 214, 80);
            for (int k = 0; k < 5; k++) { float a = k * 1.2566f; fb_circle(f, px + sw + cosf(a) * 4.5f * s, y - 8 * s + sinf(a) * 4.5f * s, 3.4f * s, fc); }
            fb_circle(f, px + sw, y - 8 * s, 2.8f * s, RGB(255, 245, 200));
        }
        break;
    }
    ink_off();
}

static void draw_kruk(Fb *f, float x, float y, float s, float face, float t, int moving, float flash, float dash) {
    ink(s);
    float ff = face >= 0 ? 1.f : -1.f;
    shadow(f, x, y + 18 * s, 21 * s, 6 * s);
    y += sinf(t * (moving ? 14.f : 5.f)) * 1.6f * s;
    uint32_t body = fl(RGB(36, 40, 64), flash), belly = fl(RGB(58, 64, 96), flash), wing = fl(RGB(26, 29, 48), flash);
    if (dash > 0) { for (int i = 1; i <= 3; i++) { gfx_set_alpha(.25f / i); fb_ellipse(f, x - ff * i * 14 * s, y, 20 * s, 14 * s, 0, body); } gfx_set_alpha(1.f); }
    fb_line(f, x - ff * 6 * s, y + 13 * s, x - ff * 5 * s, y + 20 * s, 2.4f * s, RGB(238, 160, 56)); fb_line(f, x + ff * 4 * s, y + 13 * s, x + ff * 5 * s, y + 20 * s, 2.4f * s, RGB(238, 160, 56));
    fb_spike(f, x - ff * 12 * s, y + 1 * s, 19 * s, 15 * s, ff > 0 ? PI - .15f : .15f, wing);
    fb_ellipse(f, x, y, 20 * s, 15 * s, 0, body);
    fb_ellipse(f, x + ff * 2 * s, y + 5 * s, 13 * s, 8 * s, 0, belly);
    float flap = sinf(t * (moving || dash > 0 ? 20.f : 5.f)) * .4f;
    fb_ellipse(f, x - ff * 3 * s, y - 1 * s, 14 * s, 7.5f * s, ff * (.55f + flap), wing);
    fb_ellipse(f, x - ff * 4 * s, y - 4 * s, 8 * s, 2.6f * s, ff * (.55f + flap), RGBA(110, 130, 190, 140));
    fb_circle(f, x + ff * 13 * s, y - 9 * s, 10.5f * s, body);
    fb_spike(f, x + ff * 21 * s, y - 9 * s, 12 * s, 8.5f * s, ff > 0 ? 0.f : PI, fl(RGB(244, 170, 60), flash));
    fb_circle(f, x + ff * 16.5f * s, y - 12 * s, 3.9f * s, WHITE); fb_circle(f, x + ff * 17.8f * s, y - 12 * s, 2 * s, RGB(20, 20, 30));
    ink_off(); fb_circle(f, x + ff * 18.4f * s, y - 12.8f * s, .8f * s, WHITE);
}

static void hpbar(Fb *f, float x, float y, float w, float k) {
    fb_rrect(f, x - w / 2 - 1.5f, y - 1.5f, w + 3, 8, 4, INK);
    if (k > 0) fb_rrect(f, x - w / 2, y, fmaxf(5, w * k), 5, 2.5f, k > .5f ? RGB(150, 190, 78) : (k > .25f ? RGB(236, 176, 60) : RGB(206, 70, 58)));
}

static void draw_enemy(Fb *f, Enemy *e) {
    ink(e->type == E_FOX ? 1.5f : 1.f);
    float x = e->x, y = e->y, ff = e->face, fk = e->flash > 0 ? .7f : 0, t = e->t;
    if (e->burn > 0) fk = fmaxf(fk, .0f);
    switch (e->type) {
    case E_RAT:
        shadow(f, x, y + 11, 16, 5);
        fb_line(f, x - ff * 15, y + 2, x - ff * 24, y + 5 + sinf(t * 9) * 4, 3, fl(RGB(220, 150, 160), fk)); fb_line(f, x - ff * 24, y + 5 + sinf(t * 9) * 4, x - ff * 32, y + sinf(t * 9 + 1) * 6, 2.4f, fl(RGB(220, 150, 160), fk));
        fb_ellipse(f, x, y, 17, 11, 0, fl(RGB(150, 150, 164), fk));
        fb_circle(f, x + ff * 13, y - 2, 9, fl(RGB(166, 166, 180), fk));
        fb_circle(f, x + ff * 9, y - 10, 5.2f, fl(RGB(240, 170, 190), fk)); fb_circle(f, x + ff * 16, y - 9, 4.4f, fl(RGB(240, 170, 190), fk));
        fb_spike(f, x + ff * 18, y - 1, 8, 7, ff > 0 ? 0 : PI, fl(RGB(236, 160, 176), fk));
        fb_circle(f, x + ff * 15, y - 5, 2.2f, RGB(30, 20, 30)); fb_circle(f, x + ff * 25, y - 1, 1.8f, RGB(40, 20, 30));
        break;
    case E_SNAKE: {
        shadow(f, x, y + 8, 14, 4);
        for (int s = 6; s >= 0; s--) { float r = 4.2f + (6 - s) * .35f; fb_circle(f, e->seg[s][0], e->seg[s][1], r, fl((s % 2) ? RGB(86, 164, 74) : RGB(106, 186, 90), fk)); }
        fb_circle(f, x, y, 9.5f, fl(RGB(116, 198, 98), fk));
        float tx = ff * (3 + sinf(t * 16) * 1.5f); fb_line(f, x + ff * 7, y + 1, x + ff * 15 + tx, y + 1, 1.6f, RGB(230, 60, 80));
        fb_circle(f, x + ff * 3, y - 4, 2.8f, WHITE); fb_circle(f, x + ff * 4, y - 4, 1.4f, RGB(20, 20, 20));
        break; }
    case E_CAT:
        shadow(f, x, y + 19, 22, 6);
        fb_line(f, x - ff * 18, y + 5, x - ff * 30, y - 6 + sinf(t * 4) * 5, 6, fl(RGB(232, 140, 62), fk)); fb_circle(f, x - ff * 30, y - 6 + sinf(t * 4) * 5, 3.5f, fl(RGB(250, 230, 210), fk));
        fb_ellipse(f, x, y + 3, 22, 17, 0, fl(RGB(240, 150, 68), fk));
        for (int i = -1; i <= 1; i++) fb_line(f, x + i * 8 - 2, y - 8, x + i * 8, y - 1, 2.4f, fl(RGB(200, 108, 40), fk));
        fb_circle(f, x + ff * 6, y - 11, 15.5f, fl(RGB(246, 160, 76), fk));
        fb_spike(f, x + ff * 6 - 9, y - 21, 13, 11, -PI / 2 - .25f, fl(RGB(236, 140, 60), fk)); fb_spike(f, x + ff * 6 + 9, y - 21, 13, 11, -PI / 2 + .25f, fl(RGB(236, 140, 60), fk));
        fb_ellipse(f, x + ff * 6, y - 8, 9, 6.5f, 0, fl(RGB(255, 236, 214), fk));
        fb_ellipse(f, x + ff * 6 - 5.5f, y - 13, 3.5f, 4.2f, 0, RGB(250, 232, 90)); fb_ellipse(f, x + ff * 6 + 5.5f, y - 13, 3.5f, 4.2f, 0, RGB(250, 232, 90));
        fb_ellipse(f, x + ff * 6 - 5.5f, y - 13, 1.2f, 3.8f, 0, RGB(20, 20, 20)); fb_ellipse(f, x + ff * 6 + 5.5f, y - 13, 1.2f, 3.8f, 0, RGB(20, 20, 20));
        fb_tri(f, x + ff * 6 - 2.5f, y - 8.5f, x + ff * 6 + 2.5f, y - 8.5f, x + ff * 6, y - 5.5f, RGB(240, 120, 140));
        fb_line(f, x + ff * 6 + 6, y - 7, x + ff * 6 + 17, y - 9, 1.2f, RGBA(255, 255, 255, 200)); fb_line(f, x + ff * 6 - 6, y - 7, x + ff * 6 - 17, y - 9, 1.2f, RGBA(255, 255, 255, 200));
        break;
    default: {
        shadow(f, x, y + 36, 46, 9);
        float wag = sinf(t * 5.f) * .15f;
        fb_ellipse(f, x - ff * 46, y + 6, 30, 15, ff > 0 ? PI + .3f + wag : -.3f - wag, fl(RGB(226, 104, 44), fk)); fb_circle(f, x - ff * 70, y + 14, 12, fl(RGB(252, 244, 232), fk));
        fb_ellipse(f, x, y + 4, 46, 30, 0, fl(RGB(234, 114, 50), fk));
        fb_ellipse(f, x + ff * 14, y + 12, 24, 18, 0, fl(RGB(252, 240, 224), fk));
        fb_circle(f, x + ff * 28, y - 12, 25, fl(RGB(240, 124, 56), fk));
        fb_spike(f, x + ff * 28 - 15, y - 30, 24, 18, -PI / 2 - .3f, fl(RGB(214, 90, 36), fk)); fb_spike(f, x + ff * 28 + 15, y - 30, 24, 18, -PI / 2 + .3f, fl(RGB(214, 90, 36), fk));
        fb_ellipse(f, x + ff * 34, y - 4, 16, 11, 0, fl(RGB(252, 240, 224), fk));
        fb_circle(f, x + ff * 46, y - 6, 5, RGB(30, 24, 26));
        fb_ellipse(f, x + ff * 22, y - 17, 5, 5.5f, 0, WHITE); fb_ellipse(f, x + ff * 37, y - 17, 5, 5.5f, 0, WHITE);
        fb_circle(f, x + ff * 24, y - 16, 2.4f, RGB(120, 20, 20)); fb_circle(f, x + ff * 38, y - 16, 2.4f, RGB(120, 20, 20));
        fb_line(f, x + ff * 16, y - 25, x + ff * 28, y - 20, 3, RGB(110, 40, 20)); fb_line(f, x + ff * 44, y - 25, x + ff * 32, y - 20, 3, RGB(110, 40, 20));
        break; }
    }
    ink_off();
    if (e->hp < e->maxhp) hpbar(f, x, y - e->r - (e->type == E_FOX ? 34 : 18), e->type == E_FOX ? 90 : 34, e->hp / e->maxhp);
}

/* ------------- static backgrounds ------------- */
static void grain(Fb *f, int amount) {
    uint32_t r = 2463534242u;
    for (int i = 0; i < f->w * f->h; i++) {
        r ^= r << 13; r ^= r >> 17; r ^= r << 5;
        int n = (int)(r % (2 * amount + 1)) - amount;
        uint32_t c = f->px[i];
        int R = (int)(c >> 24) + n, Gc = (int)((c >> 16) & 255) + n, B = (int)((c >> 8) & 255) + n;
        R = R < 0 ? 0 : (R > 255 ? 255 : R); Gc = Gc < 0 ? 0 : (Gc > 255 ? 255 : Gc); B = B < 0 ? 0 : (B > 255 ? 255 : B);
        f->px[i] = ((uint32_t)R << 24) | ((uint32_t)Gc << 16) | ((uint32_t)B << 8) | 0xFFu;
    }
}
static void vignette(Fb *f, float strength) {
    float cx = f->w / 2.f, cy = f->h / 2.f, md = fsq(cx * cx + cy * cy);
    for (int y = 0; y < f->h; y++) for (int x = 0; x < f->w; x++) {
        float d = fsq((x - cx) * (x - cx) + (y - cy) * (y - cy)) / md; float k = 1.f - strength * d * d;
        uint32_t c = f->px[y * f->w + x];
        f->px[y * f->w + x] = ((uint32_t)((c >> 24) * k) << 24) | ((uint32_t)(((c >> 16) & 255) * k) << 16) | ((uint32_t)(((c >> 8) & 255) * k) << 8) | 0xFFu;
    }
}
static void leaf(Fb *f, float x, float y, float s, float a, uint32_t c) {
    fb_ellipse(f, x, y, 7 * s, 4 * s, a, c); fb_line(f, x - cosf(a) * 7 * s, y - sinf(a) * 7 * s, x + cosf(a) * 6 * s, y + sinf(a) * 6 * s, .9f * s, gfx_mix(c, INK, .45f));
}

static uint32_t etype_col(int t) { return t == E_CAT ? RGB(240, 150, 70) : t == E_SNAKE ? RGB(110, 200, 90) : t == E_FOX ? RGB(230, 90, 40) : RGB(176, 176, 190); }
static void blit(Fb *dst, const Fb *src, int x, int y) {
    for (int yy = 0; yy < src->h; yy++) {
        int dy = y + yy; if (dy < 0 || dy >= dst->h) continue;
        int x0 = x < 0 ? -x : 0, x1 = x + src->w > dst->w ? dst->w - x : src->w;
        if (x1 > x0) memcpy(&dst->px[dy * dst->w + x + x0], &src->px[yy * src->w + x0], (size_t)(x1 - x0) * 4);
    }
}
static void chip_frame(Fb *f, float x, float y, int size) {
    fb_rrect(f, x - 2, y - size * .75f - 2, size * 3.6f + 4, size * 1.5f + 4, size * .75f + 2, INK);
    fb_rrect(f, x, y - size * .75f, size * 3.6f, size * 1.5f, size * .75f, PANEL_D);
}
static void chip_dyn(Fb *f, int k, int n, float x, float y, int size, float t) {
    draw_res(f, k, x + size * .8f, y, size / 24.f * 1.0f, t);
    char b[16]; snprintf(b, sizeof b, "%d", n); gfx_font(FONT_DISPLAY); fb_text_shadow(f, b, x + size * 1.55f, y, (int)(size * 1.05f), CREAM, 0); gfx_font(FONT_UI);
}
/* Pre-rendered build-bar buttons (normal / selected / locked) - drawing 6 outlined buildings per frame was too slow */
static Fb btn_img[3][B_N];
#define BIW ((int)BTN_W + 8)
#define BIH ((int)BAR_H + 8)
static void build_btn_imgs(void) {
    for (int v = 0; v < 3; v++) for (int i = 0; i < B_N; i++) {
        Fb *f = &btn_img[v][i]; f->w = BIW; f->h = BIH; f->px = (uint32_t *)malloc(sizeof(uint32_t) * BIW * BIH);
        fb_clear(f, RGB(84, 58, 44));
        float x = 4, y = 4; int sel = v == 1, lock = v == 2;
        fb_rrect(f, x - 2.5f, y - 2.5f, BTN_W + 5, BAR_H + 5, 14, sel ? ACCENT : INK);
        fb_rrect(f, x, y, BTN_W, BAR_H, 12, lock ? RGB(48, 36, 36) : (sel ? RGB(98, 70, 54) : PANEL));
        gfx_set_alpha(lock ? .35f : 1.f); draw_building(f, i, x + 24, y + 30, 0.4f, 0.f); gfx_set_alpha(1.f);
        fb_text(f, lock ? "???" : BNAME[i], x + 46, y + 15, 15, lock ? RGB(140, 120, 112) : CREAM, 0);
        if (lock) {
            char b[24]; snprintf(b, sizeof b, "wave %d", BREQ[i]); fb_text(f, b, x + BTN_W / 2, y + 48, 15, RGB(176, 154, 140), 1);
            fb_rrect(f, x + 17, y + 24, 14, 11, 3, RGB(210, 210, 214)); fb_ring(f, x + 24, y + 22, 5, 2, RGB(210, 210, 214));
        } else {
            float cx = x + 14;
            for (int k = 0; k < R_N; k++) if (BCOST[i][k] > 0) { draw_res(f, k, cx, y + 49, .5f, 0.f); cx += 34; }
        }
    }
}
/* soft burrow marks, drawn on the GamePad background only */
static void draw_burrow_mark(Fb *f, float x, float y) {
    fb_ellipse(f, x, y + 2, 15, 8, 0, RGBA(40, 26, 20, 150));
    fb_ellipse(f, x, y + 3, 11, 5.5f, 0, RGBA(14, 8, 6, 200));
    for (int i = 0; i < 5; i++) { float a = i * 1.3f + .3f; fb_circle(f, x + cosf(a) * 14, y + 3 + sinf(a) * 7, 2.2f, RGBA(120, 88, 58, 200)); }
}
static void build_bgs(void) {
    bg_tv.w = TVW; bg_tv.h = TVH; bg_tv.px = (uint32_t *)malloc(sizeof(uint32_t) * TVW * TVH);
    bg_pad.w = PADW; bg_pad.h = PADH; bg_pad.px = (uint32_t *)malloc(sizeof(uint32_t) * PADW * PADH);
    Fb *f = &bg_tv; srand(7);
    /* dusk sky with layered hills + tree line */
    fb_vgrad(f, 0, 0, TVW, TVH, RGB(44, 34, 64), RGB(120, 66, 62));
    fb_circle(f, 1080, 640, 150, RGB(236, 150, 84)); fb_circle(f, 1080, 640, 118, RGB(250, 190, 112));
    fb_circle(f, 200, 790, 420, RGB(72, 48, 66)); fb_circle(f, 700, 830, 520, RGB(60, 40, 58)); fb_circle(f, 1200, 800, 440, RGB(70, 46, 62));
    for (int i = 0; i < 26; i++) { float x = i * 52 + frange(-10, 10); float h = frange(24, 52); fb_circle(f, x, 690 + frange(-4, 4), h * .8f, RGB(40, 28, 44)); fb_rect(f, x - 3, 690, 6, 40, RGB(32, 22, 36)); }
    for (int i = 0; i < 22; i++) { float x = i * 61 + frange(-10, 10); fb_circle(f, x, 22 + frange(-6, 6), frange(26, 44), RGB(38, 26, 44)); }
    /* arena frame: chunky wooden planks */
    fb_rrect(f, AX - 16, AY - 14, AW + 32, AH + 32, 22, INK);
    fb_rrect(f, AX - 12, AY - 10, AW + 24, AH + 24, 18, RGB(112, 74, 48));
    for (int i = 0; i < 40; i++) { float x = AX - 10 + i * 31, y = AY - 8; fb_line(f, x, y, x + frange(6, 20), y, 1.2f, RGB(84, 54, 36)); fb_line(f, x, y + AH + 20, x + frange(6, 20), y + AH + 20, 1.2f, RGB(84, 54, 36)); }
    fb_rrect(f, AX - 3, AY - 3, AW + 6, AH + 6, 14, INK);
    /* painterly meadow: irregular patches rather than even stripes */
    fb_rrect(f, AX, AY, AW, AH, 12, RGB(128, 148, 74));
    for (int i = 0; i < 46; i++) {
        float x = frange(AX + 40, AX + AW - 40), y = frange(AY + 30, AY + AH - 30), rx = frange(50, 150), ry = frange(26, 70);
        uint32_t c = (rand() % 3 == 0) ? RGB(150, 164, 80) : ((rand() % 2) ? RGB(112, 138, 68) : RGB(138, 156, 78));
        float ax = x - rx < AX + 6 ? AX + 6 + rx : (x + rx > AX + AW - 6 ? AX + AW - 6 - rx : x), ay = y - ry < AY + 6 ? AY + 6 + ry : (y + ry > AY + AH - 6 ? AY + AH - 6 - ry : y);
        fb_ellipse(f, ax, ay, rx, ry, frange(-.4f, .4f), c);
    }
    for (int i = 0; i < 9; i++) { float x = frange(AX + 80, AX + AW - 80), y = frange(AY + 60, AY + AH - 60); fb_ellipse(f, x, y, frange(30, 60), frange(14, 26), frange(-.3f, .3f), RGB(156, 120, 74)); fb_ellipse(f, x + 4, y + 3, frange(20, 40), frange(8, 16), 0, RGB(146, 110, 68)); }
    for (int i = 0; i < 150; i++) {
        float x = frange(AX + 14, AX + AW - 14), y = frange(AY + 14, AY + AH - 14); int k = rand() % 6;
        if (k == 0) leaf(f, x, y, frange(.8f, 1.3f), frange(0, 6.28f), RGB(214, 112, 44));
        else if (k == 1) leaf(f, x, y, frange(.8f, 1.3f), frange(0, 6.28f), RGB(184, 62, 44));
        else if (k == 2) leaf(f, x, y, frange(.8f, 1.3f), frange(0, 6.28f), RGB(226, 178, 62));
        else if (k == 3) { gfx_set_outline(1.1f, INK); fb_ellipse(f, x, y, 5, 3.5f, frange(0, 3), RGB(150, 146, 140)); gfx_set_outline(0, 0); }
        else { for (int t = -1; t <= 1; t++) fb_line(f, x + t * 3, y + 2, x + t * 4.5f, y - 6 - (t == 0) * 2, 1.7f, RGB(92, 118, 58)); }
    }
    for (int r = 0; r <= GROWS; r++) for (int c = 0; c <= GCOLS; c++) fb_circle(f, AX + c * CELLW, AY + r * CELLW, 2.0f, RGBA(255, 244, 214, 40));
    vignette(f, .5f); grain(f, 5);

    Fb *g = &bg_pad;
    fb_vgrad(g, 0, 0, PADW, PADH, RGB(66, 48, 42), RGB(40, 28, 30));
    for (int i = 0; i < 18; i++) fb_line(g, 0, 20 + i * 27 + frange(-3, 3), PADW, 20 + i * 27 + frange(-3, 3), 1.2f, RGBA(0, 0, 0, 40));
    bg_padm.w = PADW; bg_padm.h = PADH; bg_padm.px = (uint32_t *)malloc(sizeof(uint32_t) * PADW * PADH); fb_copy(&bg_padm, g);
    fb_rrect(g, MX - 9, MY - 9, 838, 360, 14, INK);
    fb_rrect(g, MX - 6, MY - 6, 832, 354, 12, RGB(112, 74, 48));
    fb_rrect(g, MX - 2.5f, MY - 2.5f, 825, 347, 9, INK);
    fb_rrect(g, MX - 1, MY - 1, 822, 344, 8, RGB(128, 148, 74));
    for (int r = 0; r < GROWS; r++) for (int c = 0; c < GCOLS; c++)
        fb_rrect(g, MX + c * CELLW * MS + 1.5f, MY + r * CELLW * MS + 1.5f, CELLW * MS - 3, CELLW * MS - 3, 6, ((r + c) % 2) ? RGB(138, 158, 80) : RGB(124, 146, 72));
    for (int i = 0; i < 26; i++) { float x = frange(MX + 10, MX + 810), y = frange(MY + 10, MY + 332); leaf(g, x, y, .6f, frange(0, 6.28f), (i % 3 == 0) ? RGB(214, 112, 44) : ((i % 3 == 1) ? RGB(226, 178, 62) : RGB(184, 62, 44))); }
    fb_rrect(g, 5, BAR_Y - 7, PADW - 10, BAR_H + 14, 14, INK);
    fb_rrect(g, 8, BAR_Y - 4, PADW - 16, BAR_H + 8, 12, RGB(84, 58, 44));
    for (int i = 0; i < NBURROW; i++) draw_burrow_mark(g, padx(BURROW[i][0]), pady(BURROW[i][1]));
    grain(g, 5);
    /* static parts of the GamePad top bar */
    fb_rrect(g, 3, 1, PADW - 6, 50, 17, INK); fb_rrect(g, 6, 4, PADW - 12, 44, 14, PANEL);
    for (int k = 0; k < R_N; k++) chip_frame(g, 18 + k * 118, 26, 20);
    /* static parts of the TV HUD */
    fb_rrect(f, 25, 11, 1230, 106, 22, INK); fb_rrect(f, 28, 14, 1224, 100, 19, PANEL); fb_rrect(f, 28, 14, 1224, 6, 3, RGB(86, 62, 52));
    draw_heart(f, 66, 48, 1.3f, RGB(240, 80, 110));
    fb_rrect(f, 89, 33, 336, 32, 16, INK); fb_rrect(f, 92, 36, 330, 26, 13, PANEL_D);
    fb_text_shadow(f, "A  Dash", 66, 88, 18, CREAM, 0); fb_rrect(f, 148, 80, 74, 16, 8, INK); fb_rrect(f, 150, 82, 70, 12, 6, PANEL_D);
    fb_text_shadow(f, "B  Caw", 240, 88, 18, CREAM, 0); fb_rrect(f, 348, 80, 76, 16, 8, INK); fb_rrect(f, 350, 82, 72, 12, 6, PANEL_D);
    /* TV-only legend (right side of the HUD) */
    fb_ellipse(f, 905, 48, 22, 13, 0, RGBA(255, 214, 90, 90)); fb_ring(f, 905, 48, 20, 2.5f, RGBA(255, 226, 120, 220));
    fb_text_shadow(f, "Gold glow = rich soil", 940, 40, 22, RGB(255, 226, 140), 0);
    fb_text_shadow(f, "Only the TV can see it", 940, 70, 18, RGB(214, 196, 168), 0);
    build_btn_imgs();
    srand(1234u);
}

/* ------------- TV screen ------------- */
static void txt_a(Fb *f, const char *s, float x, float y, int size, uint32_t c, int al, float a) { gfx_set_alpha(a); gfx_font(FONT_DISPLAY); fb_text_shadow(f, s, x, y, size, c, al); gfx_font(FONT_UI); gfx_set_alpha(1.f); }


static void render_title_tv(Fb *f) {
    float t = G.tvTime;
    fb_vgrad(f, 0, 0, TVW, TVH, RGB(30, 40, 78), RGB(244, 150, 96));
    for (int i = 0; i < 60; i++) { float sx = fmodf(i * 211.f, TVW), sy = fmodf(i * 97.f, 340); fb_circle(f, sx, sy, 1.2f + (i % 3) * .5f, RGBA(255, 255, 255, 120 + (int)(80 * sinf(t * 2 + i)))); }
    fb_circle(f, 940, 330, 120, RGBA(255, 220, 160, 50)); fb_circle(f, 940, 330, 88, RGB(255, 232, 190));
    fb_circle(f, 200, 760, 520, RGB(52, 70, 92)); fb_circle(f, 1100, 800, 520, RGB(40, 56, 80));
    fb_rect(f, 0, 640, TVW, 80, RGB(26, 38, 56));
    float by = 380 + sinf(t * 1.6f) * 14;
    draw_kruk(f, 640, by, 3.6f, 1, t, 1, 0, 0);
    txt_a(f, "KRUCZEK", 640, 112, 108, RGB(255, 236, 190), 1, 1.f);
    txt_a(f, "SURVIVAL", 640, 200, 70, RGB(255, 170, 90), 1, 1.f);
    txt_a(f, "Two screens. One nest.", 640, 600, 30, RGB(236, 240, 255), 1, .95f);
    txt_a(f, "Pick up the GamePad and touch the screen", 640, 660, 24, RGB(190, 210, 240), 1, .7f + .3f * sinf(t * 3));
}


static void render_help_tv(Fb *f) {
    fb_copy(f, &bg_tv);
    gfx_set_alpha(.9f); fb_rect(f, 0, 0, TVW, TVH, RGB(14, 10, 14)); gfx_set_alpha(.88f); fb_rrect(f, 110, 60, 1060, 600, 34, INK); fb_rrect(f, 114, 64, 1052, 592, 31, PANEL); gfx_set_alpha(1);
    txt_a(f, "HOW TO PLAY", 640, 112, 58, CREAM, 1, 1.f);
    static const char *L[] = {
        "1   Fight on the TV. Kruczek shoots on his own.",
        "      Left stick = move     A = dash     B = caw (pushes enemies away)",
        "2   Enemies drop loot. Walk over it and it falls onto the GamePad.",
        "3   TAP the falling loot on the GamePad before it breaks.",
        "4   Spend loot on the GamePad map to build. Clear waves to unlock more.",
    };
    for (int i = 0; i < 5; i++) fb_text_shadow(f, L[i], 170, 190 + i * 42, i == 1 ? 22 : 25, i == 1 ? RGB(214, 196, 168) : CREAM, 0);
    fb_rrect(f, 170, 418, 940, 3, 1.5f, RGB(120, 90, 70));
    txt_a(f, "Each screen knows something the other cannot see - talk to each other!", 640, 456, 26, ACCENT, 1, 1.f);
    fb_text_shadow(f, "GamePad only:  enemy burrows (flash before enemies appear) and buried treasure (gold X).", 170, 510, 23, RGB(190, 220, 255), 0);
    fb_text_shadow(f, "TV only:  rich soil (gold glow, build there for bonuses) and enemy health bars.", 170, 552, 23, RGB(255, 226, 140), 0);
    fb_text_shadow(f, "Use the GamePad to go back", 640, 620, 20, RGBA(240, 222, 190, 200), 1);
}
static void draw_help_pad(Fb *f) {
    float t = G.padTime;
    txt_a(f, "HOW TO PLAY", PADW / 2, 40, 36, CREAM, 1, 1.f);
    static const char *L[] = { "Loot falls onto this screen - TAP it fast!", "Tap the map to build. Tap a button to pick what.",
        "Gold X (buried loot) and flashing burrows", "are visible ONLY on this screen.", "Rich soil (gold glow) and enemy health", "are visible ONLY on the TV.", "Tell each other what you see!" };
    for (int i = 0; i < 7; i++) fb_text_shadow(f, L[i], PADW / 2, 100 + i * 36 + (i >= 2 ? 10 : 0) + (i >= 4 ? 10 : 0) + (i == 6 ? 12 : 0), i == 6 ? 22 : 21, i == 6 ? ACCENT : CREAM, 1);
    float pulse = 1.f + .02f * sinf(t * 6);
    float w = HELPBTN_W * pulse, h = HELPBTN_H * pulse, x = HELPBTN_X + (HELPBTN_W - w) / 2, y = HELPBTN_Y + (HELPBTN_H - h) / 2;
    fb_rrect(f, x - 3, y - 3, w + 6, h + 6, 22, ACCENT); fb_rrect(f, x, y, w, h, 19, RGB(104, 74, 56));
    gfx_font(FONT_DISPLAY); fb_text_shadow(f, "Back", PADW / 2, HELPBTN_Y + HELPBTN_H / 2, 26, CREAM, 1); gfx_font(FONT_UI);
    if (G.touchFlash > 0) { float k = 1 - G.touchFlash / .35f; gfx_set_alpha(1 - k); fb_ring(f, G.lastTouchX, G.lastTouchY, 8 + k * 26, 3, WHITE); gfx_set_alpha(1); }
}

static void render_tv(Fb *f) {
    if (G.mode == MODE_TITLE) { render_title_tv(f); return; }
    if (G.mode == MODE_HELP) { render_help_tv(f); return; }
    Player *p = &G.p; float t = G.tvTime;
    fb_copy(f, &bg_tv);

    /* rich soil: glows gold on the TV only */
    for (int r = 0; r < GROWS; r++) for (int c = 0; c < GCOLS; c++) if (G.sv.soil[r][c]) {
        float cx = cellcx(c), cy = cellcy(r), pu = .5f + .5f * sinf(t * 2.2f + c + r);
        int has = G.sv.grid[r][c] >= 0;
        fb_ellipse(f, cx, cy + 6, 44, 27, 0, RGBA(255, 214, 90, has ? 26 : (int)(46 + 26 * pu)));
        fb_ring(f, cx, cy + 6, has ? 40.f : 30.f, 2.2f, RGBA(255, 232, 130, has ? 120 : (int)(110 + 80 * pu)));
        if (!has) for (int k = 0; k < 3; k++) { float a = t * 1.1f + k * 2.1f + r; fb_circle(f, cx + cosf(a) * 20, cy + 4 + sinf(a) * 10, 1.8f + pu, RGBA(255, 244, 190, 200)); }
    }
    /* burrow dust (the warning itself is on the GamePad) and treasure digging */
    for (int i = 0; i < NW; i++) if (G.wn[i].alive && G.wn[i].t < .9f) {
        Warn *w = &G.wn[i]; float k = 1 - w->t / .9f;
        fb_ellipse(f, w->x, w->y + 6, 18 + 12 * k, 8 + 5 * k, 0, RGBA(110, 84, 56, 190));
    }
    for (int i = 0; i < NTR; i++) if (G.tr[i].alive && G.tr[i].prog > 0.f) {
        Treasure *tr = &G.tr[i];
        fb_ellipse(f, tr->x, tr->y + 8, 26, 12, 0, RGBA(96, 70, 46, 220));
        fb_circle(f, tr->x, tr->y, 4 + tr->prog * 22, RGBA(255, 214, 90, 110)); fb_ring(f, tr->x, tr->y, 30, 4, RGBA(255, 255, 255, 190));
    }
    /* buildings */
    for (int r = 0; r < GROWS; r++) for (int c = 0; c < GCOLS; c++) {
        int b = G.sv.grid[r][c]; if (b < 0) continue;
        float pop = clampf(G.cpop[r][c], 0, 1), sc = easeback(pop) * 0.92f;
        draw_building(f, b, cellcx(c), cellcy(r), sc, t + c * .7f);
        if (b == B_PICNIC) fb_ring(f, cellcx(c), cellcy(r) + 6, 66, 2, RGBA(255, 255, 255, 40));
        if (b == B_FIRE) fb_ring(f, cellcx(c), cellcy(r), 120, 2, RGBA(255, 190, 80, 36));
        if (b == B_TOWER) fb_ring(f, cellcx(c), cellcy(r), 330, 1.5f, RGBA(255, 255, 255, 18));
    }
    /* drops */
    for (int i = 0; i < ND; i++) if (G.dr[i].alive) {
        Drop *d = &G.dr[i]; float bob = sinf(d->t * 4) * 3, a = d->life < 3 ? (.4f + .6f * (sinf(d->life * 14) > 0)) : 1.f;
        gfx_set_alpha(a); fb_ellipse(f, d->x, d->y + 12, 11, 4, 0, RGBA(0, 0, 0, 70));
        draw_res(f, d->kind, d->x, d->y + bob, 1.35f, d->t); gfx_set_alpha(1.f);
    }
    /* sorted actors */
    int order[NE + 1]; float ys[NE + 1]; int n = 0;
    for (int i = 0; i < NE; i++) if (G.en[i].alive) { order[n] = i; ys[n] = G.en[i].y; n++; }
    order[n] = -1; ys[n] = p->y; n++;
    for (int i = 1; i < n; i++) { int o = order[i]; float yv = ys[i]; int j = i - 1; while (j >= 0 && ys[j] > yv) { order[j + 1] = order[j]; ys[j + 1] = ys[j]; j--; } order[j + 1] = o; ys[j + 1] = yv; }
    for (int k = 0; k < n; k++) {
        if (order[k] >= 0) {
            Enemy *e = &G.en[order[k]];
            if (e->type == E_FOX && e->st == 1) {
                float pl = .35f + .25f * sinf(t * 30); float L = 700;
                gfx_set_alpha(pl); fb_line(f, e->x, e->y, e->x + e->dirx * L, e->y + e->diry * L, 26, RGB(255, 70, 60)); gfx_set_alpha(1);
                fb_text_shadow(f, "!", e->x, e->y - 78, 44, RGB(255, 220, 80), 1);
            }
            if (e->stuck > 0) fb_ring(f, e->x, e->y, e->r + 6, 3, RGBA(200, 255, 150, 180));
            if (e->emerge > 0) gfx_set_alpha(.3f + .7f * (1 - e->emerge / .6f));
            draw_enemy(f, e); gfx_set_alpha(1.f);
        } else if (G.dead <= 0) {
            int blink = p->iframes > 0 && ((int)(t * 20) % 2 == 0);
            if (blink) gfx_set_alpha(.45f);
            if (p->fireBuff > 0) fb_circle(f, p->x, p->y, 40, RGBA(255, 170, 60, 40));
            draw_kruk(f, p->x, p->y, 1.0f, p->face, p->anim, p->moving, p->flash > 0 ? .8f : 0, p->dashT);
            gfx_set_alpha(1);
        }
    }
    /* shots */
    for (int i = 0; i < NS; i++) if (G.sh[i].alive) {
        Shot *s = &G.sh[i]; float ang = atan2f(s->vy, s->vx);
        if (s->kind == 0) { fb_ellipse(f, s->x, s->y, 11, 3.4f, ang, RGB(238, 240, 255)); fb_ellipse(f, s->x - cosf(ang) * 5, s->y - sinf(ang) * 5, 7, 2.2f, ang, RGBA(150, 170, 255, 180)); }
        else { fb_circle(f, s->x, s->y, 9, RGBA(150, 210, 255, 70)); fb_circle(f, s->x, s->y, 4.5f, RGB(210, 240, 255)); }
    }
    /* caw ring */
    if (G.cawFx > 0) { float k = 1 - G.cawFx / .55f; gfx_set_alpha(1 - k); fb_ring(f, p->x, p->y, 20 + k * 190, 10 * (1 - k) + 2, RGB(255, 255, 255)); fb_ring(f, p->x, p->y, 10 + k * 150, 5, RGB(170, 200, 255)); gfx_set_alpha(1); }
    /* particles */
    for (int i = 0; i < NP; i++) if (G.pt[i].alive && !G.pt[i].pad) { Part *q = &G.pt[i]; float k = q->life / q->max; gfx_set_alpha(k); fb_circle(f, q->x, q->y, q->size * (.4f + .6f * k), q->col); gfx_set_alpha(1); }
    for (int i = 0; i < NT; i++) if (G.ft[i].alive && !G.ft[i].pad) { FText *x = &G.ft[i]; txt_a(f, x->txt, x->x, x->y, x->size, x->col, 1, clampf(x->life / x->max * 2, 0, 1)); }

    /* hurt vignette */
    if (G.hurtFlash > 0) { float a = G.hurtFlash / .35f * .3f; gfx_set_alpha(a); fb_rect(f, 0, 0, TVW, 14, RGB(255, 40, 40)); fb_rect(f, 0, TVH - 14, TVW, 14, RGB(255, 40, 40)); fb_rect(f, 0, 0, 14, TVH, RGB(255, 40, 40)); fb_rect(f, TVW - 14, 0, 14, TVH, RGB(255, 40, 40)); gfx_set_alpha(1); }

    /* HUD (static frame is baked into the background) */
    float hk = clampf(p->hp / p->maxhp, 0, 1);
    if (hk > 0) fb_rrect(f, 92, 36, fmaxf(26, 330 * hk), 26, 13, hk > .5f ? RGB(150, 190, 78) : (hk > .25f ? RGB(236, 176, 60) : RGB(206, 70, 58)));
    char b[96]; snprintf(b, sizeof b, "%d / %d", (int)ceilf(p->hp), (int)p->maxhp); fb_text_shadow(f, b, 257, 49, 19, CREAM, 1);
    float dk = 1 - clampf(p->dashCd / 1.2f, 0, 1), ck = 1 - clampf(p->cawCd / 6.f, 0, 1);
    fb_rrect(f, 150, 82, fmaxf(12, 70 * dk), 12, 6, dk >= 1 ? RGB(120, 196, 200) : RGB(90, 110, 112));
    fb_rrect(f, 350, 82, fmaxf(12, 72 * ck), 12, 6, ck >= 1 ? RGB(236, 176, 60) : RGB(130, 108, 78));
    snprintf(b, sizeof b, "WAVE %d", G.sv.wave); gfx_font(FONT_DISPLAY); fb_text_shadow(f, b, 640, 44, 46, CREAM, 1); gfx_font(FONT_UI);
    int alive = warns_left(); for (int i = 0; i < NE; i++) if (G.en[i].alive) alive++;
    if (G.dead > 0) snprintf(b, sizeof b, "Respawning...");
    else if (G.wstate == WS_BREAK) snprintf(b, sizeof b, "Break - next wave in %d s", G.tut < 3 ? 0 : (int)ceilf(G.wtimer));
    else snprintf(b, sizeof b, "Enemies left: %d", alive + G.toSpawn + G.bossLeft);
    if (!(G.wstate == WS_BREAK && G.tut < 3)) fb_text_shadow(f, b, 650, 88, 21, RGB(226, 208, 176), 1);
    else fb_text_shadow(f, "Break - the wave starts when you are ready", 650, 88, 21, RGB(226, 208, 176), 1);
    if (G.tut < 6) {
        static const char *tvt[6] = { "Move Kruczek with the LEFT STICK", "Now look at the GamePad: tap its map to build a Hole",
            "Gold X on the GamePad = buried loot. Walk Kruczek there!", "Defeat the enemies! A = dash, B = caw. Kruczek shoots alone",
            "Walk over loot, then TAP it on the GamePad before it breaks", "Well done! Survive the waves and unlock new buildings" };
        float pu = .5f + .5f * sinf(t * 5);
        fb_rrect(f, 212, 662, 856, 54, 24, pu > .5f ? ACCENT : INK); fb_rrect(f, 215, 665, 850, 48, 21, PANEL);
        snprintf(b, sizeof b, "TUTORIAL %d/6", G.tut + 1); fb_text_shadow(f, b, 236, 689, 15, ACCENT, 0);
        fb_text_shadow(f, tvt[G.tut], 700, 689, 23, CREAM, 1);
    } else if (G.dead <= 0) {
        fb_text_shadow(f, G.wstate == WS_BREAK ? "Break: build on the GamePad, and dig up its gold X marks with Kruczek" : "Tap the loot that falls onto the GamePad before it breaks", 640, 692, 21, RGBA(240, 222, 190, 215), 1);
    }

    /* banners */
    if (G.bannerT > 0 && G.dead <= 0) {
        float k = clampf(G.bannerT / 2.6f, 0, 1), a = k > .85f ? (1 - k) / .15f : (k < .2f ? k / .2f : 1.f);
        float sc = G.bannerT > 2.3f ? 1.f + (G.bannerT - 2.3f) * 1.5f : 1.f;
        gfx_set_alpha(a * .85f); fb_rrect(f, 640 - 423 * sc, 233, 846 * sc, 106, 33, INK); fb_rrect(f, 640 - 420 * sc, 236, 840 * sc, 100, 30, G.bannerBoss ? RGB(150, 48, 38) : PANEL); gfx_set_alpha(1);
        txt_a(f, G.banner, 640, 286, 46, G.bannerBoss ? RGB(255, 206, 130) : CREAM, 1, a);
    }
    if (G.unlockT > 0) {
        float a = clampf(G.unlockT / .5f, 0, 1);
        gfx_set_alpha(a * .92f); fb_rrect(f, 377, 361, 526, 124, 31, INK); fb_rrect(f, 380, 364, 520, 118, 28, PANEL); fb_rrect(f, 388, 372, 504, 102, 22, RGB(78, 56, 46)); gfx_set_alpha(1);
        gfx_set_alpha(a); draw_building(f, G.unlockB, 450, 420, 1.1f, t); gfx_set_alpha(1);
        txt_a(f, "NEW BLUEPRINT!", 690, 398, 28, ACCENT, 1, a); txt_a(f, BNAME[G.unlockB], 690, 440, 42, CREAM, 1, a);
    }
    if (G.dead > 0) {
        gfx_set_alpha(.55f); fb_rect(f, 0, 0, TVW, TVH, RGB(10, 10, 20)); gfx_set_alpha(1);
        txt_a(f, "Kruczek is down...", 640, 330, 74, RGB(255, 206, 190), 1, 1.f);
        fb_text_shadow(f, "You lose a quarter of your loot. The wave starts over.", 640, 410, 26, RGB(220, 220, 240), 1);
    }
    if (G.mode == MODE_PAUSE) { gfx_set_alpha(.6f); fb_rect(f, 0, 0, TVW, TVH, RGB(8, 12, 20)); gfx_set_alpha(1); txt_a(f, "PAUSED", 640, 340, 100, CREAM, 1, 1.f); fb_text_shadow(f, "The menu is on the GamePad screen", 640, 420, 28, RGB(200, 214, 236), 1); }
}

/* ------------- GamePad screen ------------- */
static void draw_menu_pad(Fb *f) {
    float t = G.padTime;
    txt_a(f, G.mode == MODE_TITLE ? "KRUCZEK SURVIVAL" : "PAUSED", PADW / 2, 52, 40, CREAM, 1, 1.f);
    for (int i = 0; i < menu_count(); i++) {
        char lab[48]; menu_label(i, lab, sizeof lab);
        float y = menu_y(i), pulse = (i == G.msel) ? 1.f + .02f * sinf(t * 6) : 1.f;
        float w = MENU_W * pulse, h = MENU_H * pulse, x = MENU_X + (MENU_W - w) / 2, yy = y + (MENU_H - h) / 2;
        fb_rrect(f, x - 3, yy - 3, w + 6, h + 6, 20, i == G.msel ? ACCENT : INK);
        fb_rrect(f, x, yy, w, h, 17, i == G.msel ? RGB(104, 74, 56) : PANEL);
        gfx_font(FONT_DISPLAY); fb_text_shadow(f, lab, PADW / 2, y + MENU_H / 2, 25, CREAM, 1); gfx_font(FONT_UI);
    }
    fb_text(f, "Tap a button   ·   or D-pad + A", PADW / 2, 452, 17, RGBA(240, 222, 190, 190), 1);
    if (G.touchFlash > 0) { float k = 1 - G.touchFlash / .35f; gfx_set_alpha(1 - k); fb_ring(f, G.lastTouchX, G.lastTouchY, 8 + k * 26, 3, WHITE); gfx_set_alpha(1); }
}

static void render_pad(Fb *f) {
    float t = G.padTime;
    fb_copy(f, G.mode == MODE_PLAY ? &bg_pad : &bg_padm);
    if (G.mode == MODE_HELP) { draw_help_pad(f); return; }
    if (G.mode != MODE_PLAY) { draw_menu_pad(f); return; }

    /* map: buildings */
    for (int r = 0; r < GROWS; r++) for (int c = 0; c < GCOLS; c++) {
        int b = G.sv.grid[r][c]; float px = padx(cellcx(c)), py = pady(cellcy(r));
        if (b >= 0) draw_building(f, b, px, py, 0.62f * easeback(clampf(G.cpop[r][c], 0, 1)), t + c * .7f);
    }
    /* hover cell (cursor) / last touch */
    int hr = -1, hc = -1;
    if (G.curShow > 0) { hc = (int)((G.cx - MX) / (CELLW * MS)); hr = (int)((G.cy - MY) / (CELLW * MS)); }
    if (hr >= 0 && hr < GROWS && hc >= 0 && hc < GCOLS && G.cy < BAR_Y) {
        float x = MX + hc * CELLW * MS, y = MY + hr * CELLW * MS;
        fb_rrect(f, x, y, CELLW * MS, CELLW * MS, 7, RGBA(255, 255, 255, 60));
        if (G.sv.grid[hr][hc] < 0 && !G.demolish) { gfx_set_alpha(.5f); draw_building(f, G.sel, padx(cellcx(hc)), pady(cellcy(hr)), .62f, t); gfx_set_alpha(1); }
    }
    /* live battlefield on the map */
    for (int i = 0; i < ND; i++) if (G.dr[i].alive) fb_circle(f, padx(G.dr[i].x), pady(G.dr[i].y), 3.4f, RCOL[G.dr[i].kind < 3 ? G.dr[i].kind : 0]);
    for (int i = 0; i < NE; i++) if (G.en[i].alive) {
        Enemy *e = &G.en[i]; uint32_t c = e->type == E_CAT ? RGB(240, 150, 70) : e->type == E_SNAKE ? RGB(110, 200, 90) : e->type == E_FOX ? RGB(230, 90, 40) : RGB(176, 176, 190);
        float ex = padx(e->x), ey = pady(e->y), er = e->r * MS * 1.05f;
        gfx_set_outline(1.4f, INK);
        if (e->type == E_CAT || e->type == E_FOX) { fb_spike(f, ex - er * .55f, ey - er * .6f, er * .8f, er * .7f, -PI / 2 - .3f, c); fb_spike(f, ex + er * .55f, ey - er * .6f, er * .8f, er * .7f, -PI / 2 + .3f, c); }
        fb_circle(f, ex, ey, er, c); gfx_set_outline(0, 0);
        fb_circle(f, ex - er * .35f, ey - er * .1f, fmaxf(1.4f, er * .2f), WHITE); fb_circle(f, ex + er * .35f, ey - er * .1f, fmaxf(1.4f, er * .2f), WHITE);
        fb_circle(f, ex - er * .3f, ey - er * .08f, fmaxf(.8f, er * .1f), INK); fb_circle(f, ex + er * .4f, ey - er * .08f, fmaxf(.8f, er * .1f), INK);
    }
    if (G.dead <= 0) draw_kruk(f, padx(G.p.x), pady(G.p.y), .5f, G.p.face, G.p.anim, G.p.moving, 0, 0);
    if (G.cawFx > 0) { float k = 1 - G.cawFx / .55f; gfx_set_alpha(1 - k); fb_ring(f, padx(G.p.x), pady(G.p.y), (20 + k * 190) * MS, 3, WHITE); gfx_set_alpha(1); }
    for (int i = 0; i < NS; i++) if (G.sh[i].alive) fb_circle(f, padx(G.sh[i].x), pady(G.sh[i].y), 2, WITH_A(WHITE, 180));

    /* top bar (frame is baked) */
    for (int k = 0; k < R_N; k++) chip_dyn(f, k, G.sv.res[k], 18 + k * 118, 26, 20, t);
    if (G.combo >= 2) { char b[24]; snprintf(b, sizeof b, "COMBO x%d", G.combo); gfx_font(FONT_DISPLAY); fb_text_shadow(f, b, 388, 26, 21, ACCENT, 0); gfx_font(FONT_UI); }
    { char b[64]; int alive = warns_left(); for (int i = 0; i < NE; i++) if (G.en[i].alive) alive++;
      if (G.wstate == WS_BREAK) { if (G.tut < 3) snprintf(b, sizeof b, "Wave %d  ·  break", G.sv.wave); else snprintf(b, sizeof b, "Wave %d  ·  starts in %d s", G.sv.wave, (int)ceilf(G.wtimer)); }
      else snprintf(b, sizeof b, "Wave %d  ·  enemies: %d", G.sv.wave, alive + G.toSpawn + G.bossLeft);
      fb_text_shadow(f, b, PADW - 18, 26, 18, CREAM, 2); }

    /* tutorial pill (pad side) */
    float pillOff = 0;
    if (G.tut < 6) {
        static const char *pt[6] = { "TV: move Kruczek with the LEFT STICK", "Tap a spot on this map to build a Hole (4 sticks)",
            "Gold X = buried loot. Only THIS screen can see it - guide Kruczek on the TV", "Burrows flash here BEFORE enemies climb out. Dodge with A on the TV",
            "Loot falls onto this screen - TAP it before it breaks!", "Well done! Survive the waves and unlock new buildings" };
        float pu = .5f + .5f * sinf(t * 5); float w = text_width(pt[G.tut], 16) + 34;
        fb_rrect(f, PADW / 2 - w / 2 - 3, MY + 3, w + 6, 36, 18, pu > .5f ? ACCENT : INK); fb_rrect(f, PADW / 2 - w / 2, MY + 6, w, 30, 15, PANEL);
        fb_text(f, pt[G.tut], PADW / 2, MY + 21, 16, CREAM, 1); pillOff = 40;
    }
    /* burrow warnings: GamePad only */
    for (int i = 0; i < NW; i++) if (G.wn[i].alive) {
        Warn *w = &G.wn[i]; float x = padx(w->x), y = pady(w->y), k = w->t / w->total; float pu = .5f + .5f * sinf(t * 14);
        uint32_t c = etype_col(w->type); float big = w->type == E_FOX ? 1.5f : 1.f;
        gfx_set_alpha(.55f + .45f * pu); fb_ring(f, x, y, (8 + 24 * k) * big, 3, c); gfx_set_alpha(1.f);
        fb_circle(f, x, y, 5.5f * big, c); fb_text_shadow(f, w->type == E_FOX ? "BOSS!" : "!", x, y - 20 * big, w->type == E_FOX ? 18 : 20, RGB(255, 226, 110), 1);
    }
    /* buried treasure: GamePad only */
    for (int i = 0; i < NTR; i++) if (G.tr[i].alive) {
        Treasure *tr = &G.tr[i]; float x = padx(tr->x), y = pady(tr->y), pu = .5f + .5f * sinf(t * 4 + i);
        fb_circle(f, x, y, 16 + 5 * pu, RGBA(255, 214, 90, (int)(50 + 40 * pu)));
        gfx_set_outline(1.8f, INK); fb_line(f, x - 8, y - 8, x + 8, y + 8, 5, RGB(255, 214, 72)); fb_line(f, x - 8, y + 8, x + 8, y - 8, 5, RGB(255, 214, 72)); gfx_set_outline(0, 0);
        if (tr->prog > 0) { fb_ring(f, x, y, 20, 3, RGBA(255, 255, 255, 200)); }
    }

    /* info pill */
    if (G.infoT > 0) {
        char b[96];
        if (G.demolish) snprintf(b, sizeof b, "Demolish: tap a building (50%% refund)");
        else if (!unlocked(G.sel)) snprintf(b, sizeof b, "%s - unlocks after wave %d", BNAME[G.sel], BREQ[G.sel]);
        else snprintf(b, sizeof b, "%s: %s", BNAME[G.sel], BDESC[G.sel]);
        float w = text_width(b, 17) + 30, a = clampf(G.infoT, 0, 1);
        gfx_set_alpha(a * .88f); fb_rrect(f, PADW / 2 - w / 2 - 2, MY + 4 + pillOff, w + 4, 34, 17, INK); fb_rrect(f, PADW / 2 - w / 2, MY + 6 + pillOff, w, 30, 15, PANEL); gfx_set_alpha(1);
        gfx_set_alpha(a); fb_text(f, b, PADW / 2, MY + 21 + pillOff, 16, CREAM, 1); gfx_set_alpha(1);
    }

    /* build bar (buttons are pre-rendered) */
    for (int i = 0; i < B_N; i++) {
        float x = btn_x(i), y = BAR_Y; int sel = (i == G.sel && !G.demolish), lock = !unlocked(i);
        if (G.tut == 1 && i == 0) { gfx_set_alpha(.4f + .5f * (.5f + .5f * sinf(t * 7))); fb_rrect(f, x - 7, y - 7, BTN_W + 14, BAR_H + 14, 18, ACCENT); gfx_set_alpha(1); }
        blit(f, &btn_img[lock ? 2 : (sel ? 1 : 0)][i], (int)x - 4, (int)y - 4);
        if (!lock) {
            float cx = x + 14;
            for (int k = 0; k < R_N; k++) if (BCOST[i][k] > 0) {
                char b[16]; snprintf(b, sizeof b, "%d", BCOST[i][k]);
                fb_text(f, b, cx + 11, y + 49, 15, G.sv.res[k] >= BCOST[i][k] ? CREAM : RGB(255, 128, 110), 0); cx += 34;
            }
        }
    }
    { float x = DEMO_X, y = BAR_Y;
      fb_rrect(f, x - 2.5f, y - 2.5f, 69, BAR_H + 5, 14, G.demolish ? RGB(226, 84, 70) : INK);
      fb_rrect(f, x, y, 64, BAR_H, 12, G.demolish ? RGB(110, 56, 48) : PANEL);
      fb_line(f, x + 22, y + 34, x + 44, y + 12, 5, RGB(160, 112, 66)); fb_rrect(f, x + 34, y + 8, 18, 12, 4, RGB(176, 184, 198));
      fb_text(f, "Demolish", x + 32, y + 54, 12, CREAM, 1); }
    { float x = MENUBTN_X, y = BAR_Y; fb_rrect(f, x - 2.5f, y - 2.5f, 61, BAR_H + 5, 14, INK); fb_rrect(f, x, y, 56, BAR_H, 12, PANEL);
      for (int i = 0; i < 3; i++) fb_rrect(f, x + 14, y + 14 + i * 11, 28, 5, 2.5f, CREAM);
      fb_text(f, "Menu", x + 28, y + 54, 12, CREAM, 1); }

    /* falling loot */
    for (int i = 0; i < NF; i++) if (G.fl[i].alive) {
        Fall *fa = &G.fl[i];
        if (fa->state < 2) {
            float pulse = fa->state == 1 ? 1.f + .12f * sinf(fa->t * 10) : 1.f;
            fb_ellipse(f, fa->x, fa->y + 20, 17 * pulse, 5, 0, RGBA(0, 0, 0, 80));
            draw_res(f, fa->kind, fa->x, fa->y, 1.7f * pulse, t);
            if (fa->state == 1) { gfx_set_alpha(.9f); fb_ring(f, fa->x, fa->y, 34, 3.2f, fa->ttl < 1.8f ? RGB(255, 110, 100) : RGB(255, 255, 255)); gfx_set_alpha(1); }
        } else {
            float k = fa->t; float tx = 18 + 118 * 0 + 30 + fa->kind * 118, ty = 26;
            float x = fa->fx + (tx - fa->fx) * k, y = fa->fy + (ty - fa->fy) * k; gfx_set_alpha(1 - k * .6f); draw_res(f, fa->kind, x, y, 1.7f * (1 - k * .6f), t); gfx_set_alpha(1);
        }
    }
    /* particles + texts for pad */
    for (int i = 0; i < NP; i++) if (G.pt[i].alive && G.pt[i].pad) { Part *q = &G.pt[i]; float k = q->life / q->max; gfx_set_alpha(k); fb_circle(f, q->x, q->y, q->size * (.4f + .6f * k), q->col); gfx_set_alpha(1); }
    for (int i = 0; i < NT; i++) if (G.ft[i].alive && G.ft[i].pad) { FText *x = &G.ft[i]; txt_a(f, x->txt, x->x, x->y, x->size, x->col, 1, clampf(x->life / x->max * 2, 0, 1)); }

    if (G.unlockT > 0) {
        float a = clampf(G.unlockT / .5f, 0, 1);
        gfx_set_alpha(a * .93f); fb_rrect(f, 224, 147, 406, 116, 29, INK); fb_rrect(f, 227, 150, 400, 110, 26, PANEL); fb_rrect(f, 234, 157, 386, 96, 20, RGB(78, 56, 46)); gfx_set_alpha(1);
        gfx_set_alpha(a); draw_building(f, G.unlockB, 290, 208, 1.0f, t); gfx_set_alpha(1);
        txt_a(f, "NEW BLUEPRINT!", 470, 182, 21, ACCENT, 1, a); txt_a(f, BNAME[G.unlockB], 470, 224, 36, CREAM, 1, a);
    }
    if (G.dead > 0) { gfx_set_alpha(.5f); fb_rect(f, 0, 0, PADW, PADH, RGB(10, 10, 20)); gfx_set_alpha(1); fb_text_shadow(f, "Kruczek is resting...", PADW / 2, 220, 34, RGB(255, 210, 210), 1); }
    if (G.touchFlash > 0) { float k = 1 - G.touchFlash / .35f; gfx_set_alpha((1 - k) * .8f); fb_ring(f, G.lastTouchX, G.lastTouchY, 6 + k * 24, 3, WHITE); gfx_set_alpha(1); }
    if (G.curShow > 0) { fb_ring(f, G.cx, G.cy, 11, 2.6f, WHITE); fb_circle(f, G.cx, G.cy, 3, RGB(255, 214, 100)); }
}

void game_render_tv(Fb *tv) { render_tv(tv); }
void game_render_pad(Fb *pad) { render_pad(pad); }
void game_render(Fb *tv, Fb *pad) { render_tv(tv); render_pad(pad); }

void game_init(void) {
    gfx_init(); audio_init();
    memset(&G, 0, sizeof G);
    new_game_data(); G.sv.sound = 1; G.sv.music = 1;
    Save tmp;
    if (plat_load(&tmp, (int)sizeof tmp) == (int)sizeof tmp && tmp.magic == SAVE_MAGIC) { G.sv = tmp; G.hasSave = 1; }
    audio_set_enabled(G.sv.sound, G.sv.music);
    srand(1234u);
    build_bgs();
    reset_world();
    G.mode = MODE_TITLE;
}

#ifdef KR_BOT
void game_bot(Input *in) {
    static int fr = 0; fr++;
    memset(in, 0, sizeof *in);
    int tapnow = (fr % 5) == 0;
    if (getenv("KR_HELP") && fr == 3) { G.mode = MODE_HELP; G.helpFrom = MODE_TITLE; }
    if (G.mode == MODE_HELP && getenv("KR_HELP")) return;
    if (G.mode == MODE_TITLE) { if (tapnow) { in->touching = 1; in->tx = MENU_X + 20; in->ty = menu_y(0) + 20; } return; }
    if (G.mode == MODE_PAUSE) { G.mode = MODE_PLAY; return; }
    Player *p = &G.p;
    if (G.mode == MODE_HELP) { G.mode = MODE_PLAY; return; }
    Enemy *e = nearest_enemy(p->x, p->y, 230);
    float cxm = AX + AW / 2 - p->x, cym = AY + AH / 2 - p->y, cd = sqrtf(cxm * cxm + cym * cym) + 1;
    if (e) {
        float dx = p->x - e->x, dy = p->y - e->y, d = sqrtf(dx * dx + dy * dy) + 1;
        in->lx = dx / d + cxm / cd * .5f; in->ly = dy / d + cym / cd * .5f;
        if (d < 80 && (fr % 40) == 0) in->hold |= BTN_A;
    } else if (G.tut == 0) { in->lx = ((fr / 50) % 2) ? 1.f : -1.f; in->ly = 0; }
    else {
        Treasure *tt = NULL; for (int i = 0; i < NTR; i++) if (G.tr[i].alive) tt = &G.tr[i];
        float tx = tt ? tt->x : AX + AW / 2, ty = tt ? tt->y : AY + AH / 2, ddx = tx - p->x, ddy = ty - p->y, dd = sqrtf(ddx * ddx + ddy * ddy) + 1;
        in->lx = ddx / dd * (dd > 20); in->ly = ddy / dd * (dd > 20);
    }
    int near = 0; for (int i = 0; i < NE; i++) if (G.en[i].alive) { float dx = G.en[i].x - p->x, dy = G.en[i].y - p->y; if (dx * dx + dy * dy < 150 * 150) near++; }
    if (near >= 3 && (fr % 30) == 0) in->hold |= BTN_B;
    if (tapnow) {
        for (int i = 0; i < NF; i++) if (G.fl[i].alive && G.fl[i].state < 2) { in->touching = 1; in->tx = G.fl[i].x; in->ty = G.fl[i].y; return; }
        static const int pref[B_N] = { B_TOWER, B_FIRE, B_PICNIC, B_HOUSE, B_GARDEN, B_HOLE };
        for (int k = 0; k < B_N; k++) {
            int b = pref[k]; if (!unlocked(b) || !can_afford(b)) continue;
            if (k > 2 && count_b(b) >= 3) continue;
            for (int tries = 0; tries < 40; tries++) {
                int r = rand() % GROWS, c = rand() % GCOLS; if (G.sv.grid[r][c] >= 0) continue;
                G.sel = b; G.demolish = 0; in->touching = 1; in->tx = padx(cellcx(c)); in->ty = pady(cellcy(r)); return;
            }
        }
    }
}
#endif
