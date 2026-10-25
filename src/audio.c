/* Procedural audio: clean sound effects + a layered soundtrack generated in code. */
#include "audio.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PI 3.14159265f
#define TAU 6.28318531f

typedef struct { float ph[4]; float lp; float lp2; } Syn;
typedef float (*GenFn)(float t, float u, Syn *s);

static uint32_t rng = 12345;
static float rnd(void) { rng = rng * 1664525u + 1013904223u; return ((rng >> 9) & 0xFFFF) / 32768.f - 1.f; }

#define STBL 2048
static float sin_tbl[STBL + 1]; static int tbl_ok = 0;
enum { W_SIN, W_SQR, W_SAW, W_TRI, W_PUL };
static float osc(Syn *s, int k, float f, int type) {
    s->ph[k] += f / AUDIO_RATE; if (s->ph[k] >= 1.f) s->ph[k] -= floorf(s->ph[k]);
    float p = s->ph[k];
    switch (type) {
        case W_SIN: return sin_tbl[(int)(p * STBL)];
        case W_SQR: return p < .5f ? 1.f : -1.f;
        case W_SAW: return 2.f * p - 1.f;
        case W_TRI: return 4.f * fabsf(p - .5f) - 1.f;
        default:    return p < .25f ? 1.f : -1.f;
    }
}
static float lpf(float *st, float v, float a) { *st += (v - *st) * a; return *st; }

/* ---- sound effects (all soft and musical) ---- */
static float g_shoot(float t, float u, Syn *s) { (void)t; return (osc(s, 0, 980 - 520 * u, W_SIN) * .6f + osc(s, 1, 1960 - 1040 * u, W_TRI) * .15f) * powf(1 - u, 1.6f) * .35f; }
static float g_hit(float t, float u, Syn *s) { (void)t; float n = lpf(&s->lp, rnd(), .35f); return (n * .6f + osc(s, 0, 170 - 70 * u, W_SIN)) * powf(1 - u, 2.f) * .38f; }
static float g_kill(float t, float u, Syn *s) { (void)t; return (osc(s, 0, 520 * powf(.45f, u), W_SIN) + osc(s, 1, 1040 * powf(.45f, u), W_TRI) * .25f) * powf(1 - u, 1.4f) * .45f; }
static float g_caw(float t, float u, Syn *s) { /* kruk: dwa krótkie krakania */
    (void)t; float g = u < .5f ? u * 2 : (u - .5f) * 2;
    float e = sinf(PI * powf(g, .7f)); e = e * e;
    float f = (u < .5f ? 470 : 430) - 130 * g + sinf(g * 40) * 9;
    float v = osc(s, 0, f, W_SAW) * .5f + osc(s, 1, f * 2.02f, W_TRI) * .22f + rnd() * .14f;
    v = lpf(&s->lp, v, .42f);
    return v * e * .7f;
}
static float g_dash(float t, float u, Syn *s) { (void)t; float n = rnd(); lpf(&s->lp, n, .05f + .35f * u); return s->lp * sinf(PI * u) * 1.5f; }
static float g_coin(float t, float u, Syn *s) { float f = t < .08f ? 1175 : 1568; return (osc(s, 0, f, W_SIN) + osc(s, 1, f * 2, W_SIN) * .3f) * powf(1 - u, 1.8f) * .3f; }
static float g_spark(float t, float u, Syn *s) { /* złapane w locie: jasny dzwoneczek */
    static const float n[3] = { 1047, 1319, 1760 }; int i = (int)(t / .06f); if (i > 2) i = 2; float lt = t - i * .06f;
    return (osc(s, 0, n[i], W_SIN) + osc(s, 1, n[i] * 2.01f, W_SIN) * .25f) * expf(-lt * 7.f) * (1 - u * .4f) * .32f;
}
static float g_build(float t, float u, Syn *s) { /* stuk młotka + dzwonek */
    (void)u; float v = 0;
    if (t < .14f) v += (osc(s, 0, 150, W_SIN) + lpf(&s->lp, rnd(), .3f) * .4f) * expf(-t * 28);
    else v += (osc(s, 1, 392, W_TRI) + osc(s, 2, 784, W_SIN) * .3f) * expf(-(t - .14f) * 12) * .5f;
    return v * .8f;
}
static float g_deny(float t, float u, Syn *s) { (void)t; return osc(s, 0, 190 - 50 * u, W_TRI) * powf(1 - u, 1.5f) * .4f; }
static float g_hurt(float t, float u, Syn *s) { (void)t; float f = 360 - 160 * u; return lpf(&s->lp, osc(s, 0, f, W_SAW) * .5f + rnd() * .15f, .3f) * powf(1 - u, 1.4f) * .8f; }
static float g_wave(float t, float u, Syn *s) { /* trzy nuty w górę */
    (void)u; static const float n[4] = { 440, 523, 659, 880 }; int i = (int)(t / .14f); if (i > 3) i = 3;
    float lt = t - i * .14f; float e = i == 3 ? expf(-lt * 3.5f) : expf(-lt * 9.f);
    return (osc(s, 0, n[i], W_TRI) + osc(s, 1, n[i] * 2, W_SIN) * .3f) * e * .5f;
}
static float g_unlock(float t, float u, Syn *s) { /* arpeggio w górę */
    (void)u; static const float n[7] = { 523, 659, 784, 1047, 1319, 1568, 2093 }; int i = (int)(t / .075f); if (i > 6) i = 6;
    float lt = t - i * .075f;
    return (osc(s, 0, n[i], W_SIN) + osc(s, 1, n[i] * 2, W_SIN) * .2f) * (i == 6 ? expf(-lt * 4) : expf(-lt * 12)) * .38f;
}
static float g_fail(float t, float u, Syn *s) { /* łagodny opadający motyw w molu */
    (void)u; static const float n[4] = { 440, 392, 349, 294 }; static const float st[4] = { 0, .38f, .76f, 1.14f };
    int i = 0; for (int k = 0; k < 4; k++) if (t >= st[k]) i = k;
    float lt = t - st[i]; float e = fminf(lt / .02f, 1.f) * expf(-lt * (i == 3 ? 2.2f : 4.f));
    return (osc(s, 0, n[i], W_TRI) + osc(s, 1, n[i] * .5f, W_SIN) * .6f + osc(s, 2, n[i] * 2, W_SIN) * .2f) * e * .5f;
}
static float g_heal(float t, float u, Syn *s) { (void)u; static const float n[3] = { 659, 784, 988 }; int i = (int)(t / .09f); if (i > 2) i = 2; float lt = t - i * .09f; return osc(s, 0, n[i], W_SIN) * expf(-lt * 6) * .35f; }
static float g_trap(float t, float u, Syn *s) { /* miękkie "thup" */
    (void)t; float n = lpf(&s->lp, rnd(), .18f); return (osc(s, 0, 120 - 50 * u, W_SIN) * .9f + n * .5f) * powf(1 - u, 2.f) * .6f;
}
static float g_ui(float t, float u, Syn *s) { (void)t; return osc(s, 0, 880, W_SIN) * powf(1 - u, 2.f) * .22f; }
static float g_dig(float t, float u, Syn *s) { (void)u; float ph = fmodf(t, .1f); float n = lpf(&s->lp, rnd(), .22f); return n * expf(-ph * 35) * 1.3f * (t < .3f ? 1.f : 0.f); }
static float g_boss(float t, float u, Syn *s) { (void)t; float f = 82.4f; float v = osc(s, 0, f, W_SAW) * .5f + osc(s, 1, f * 1.5f, W_TRI) * .3f + osc(s, 2, f * 2, W_SIN) * .2f; return lpf(&s->lp, v, .2f) * sinf(PI * powf(u, .6f)) * 1.1f; }
static float g_tick(float t, float u, Syn *s) { (void)t; return osc(s, 0, 1400, W_SIN) * powf(1 - u, 2.f) * .14f; }

typedef struct { float dur; GenFn fn; float vol; } SfxDef;
static const SfxDef defs[S_COUNT] = {
    [S_SHOOT] = { .10f, g_shoot, .5f }, [S_HIT] = { .09f, g_hit, .7f }, [S_KILL] = { .20f, g_kill, .7f },
    [S_CAW] = { .6f, g_caw, .9f }, [S_DASH] = { .22f, g_dash, .6f }, [S_COIN] = { .25f, g_coin, .8f },
    [S_SPARK] = { .35f, g_spark, .9f }, [S_BUILD] = { .6f, g_build, 1.f }, [S_DENY] = { .22f, g_deny, .9f },
    [S_HURT] = { .3f, g_hurt, .9f }, [S_WAVE] = { .8f, g_wave, .9f }, [S_UNLOCK] = { .75f, g_unlock, .9f },
    [S_FAIL] = { 2.2f, g_fail, .9f }, [S_HEAL] = { .35f, g_heal, .8f }, [S_TRAP] = { .3f, g_trap, .9f },
    [S_UI] = { .07f, g_ui, .8f }, [S_DIG] = { .35f, g_dig, .8f }, [S_BOSS] = { 1.0f, g_boss, .9f }, [S_TICK] = { .05f, g_tick, .7f },
};

static int16_t *bank[S_COUNT]; static int bank_len[S_COUNT];
typedef struct { int id; int pos; float vol; } Voice;
#define NVOICE 24
static Voice voices[NVOICE];
static int sfx_on = 1, music_on = 1;
static float intensity_target = .2f, intensity = .2f;

void audio_init(void) {
    if (!tbl_ok) { for (int i = 0; i <= STBL; i++) sin_tbl[i] = sinf((float)i / STBL * TAU); tbl_ok = 1; }
    for (int i = 0; i < NVOICE; i++) voices[i].id = -1;
    for (int i = 0; i < S_COUNT; i++) {
        int n = (int)(defs[i].dur * AUDIO_RATE);
        bank[i] = (int16_t *)malloc(sizeof(int16_t) * n); bank_len[i] = n;
        Syn s; memset(&s, 0, sizeof s);
        for (int k = 0; k < n; k++) {
            float t = (float)k / AUDIO_RATE, u = (float)k / n;
            float v = defs[i].fn(t, u, &s) * defs[i].vol;
            v *= fminf(1.f, (n - k) / 300.f); if (v > 1) v = 1; if (v < -1) v = -1;
            bank[i][k] = (int16_t)(v * 28000);
        }
    }
}
void audio_set_enabled(int sfx, int music) { sfx_on = sfx; music_on = music; }
void audio_set_intensity(float v) { intensity_target = v; }
void sfx_play(int id, float vol) {
    if (!sfx_on || id < 0 || id >= S_COUNT || !bank[id]) return;
    int slot = -1;
    for (int i = 0; i < NVOICE; i++) if (voices[i].id < 0 || voices[i].pos >= bank_len[voices[i].id]) { slot = i; break; }
    if (slot < 0) slot = rand() % NVOICE;
    voices[slot].id = id; voices[slot].pos = 0; voices[slot].vol = vol;
}

/* ---------------- soundtrack ----------------
 * 16 bars, 100 BPM, A minor:  Am F C G | Am F G E7   Am F C G | F G Am Am
 * layers: warm pad, bass, plucked arpeggio, lead melody, soft drums (battle only), stereo delay */
#define BPM 100.f
static const int chord_root[16] = { 57,53,48,55, 57,53,55,52,  57,53,48,55, 53,55,57,57 };
static const int chord_third[16] = { 3,4,4,4, 3,4,4,4, 3,4,4,4, 4,4,3,3 };   /* semitones to the 3rd */
static const int chord_fifth[16] = { 7,7,7,7, 7,7,7,7, 7,7,7,7, 7,7,7,7 };
/* lead: 16 steps (two bars) per phrase, values are chord-degree picks: 0 rest,1 root,2 third,3 fifth,4 octave,5 9th/2nd */
static const int lead_a[16] = { 4,0,3,0,2,0,3,4, 0,5,4,0,3,0,2,0 };
static const int lead_b[16] = { 0,4,0,5,4,3,0,2, 3,0,4,0,5,0,4,3 };

/* Music engine: everything per-sample is table lookups and multiplies (the Wii U CPU has no
 * fast libm). Note frequencies and envelope constants are computed once per step. */
static inline float tsin(float ph) { float x = ph * STBL; int i = (int)x; float fr = x - i; return sin_tbl[i] + (sin_tbl[i + 1] - sin_tbl[i]) * fr; }
static double mt = 0; static float mph[10]; static float hat_lp, kick_ph, snare_lp, vib_ph;
static float dl[2][12000]; static int dlpos = 0; static const int DLEN = (int)(AUDIO_RATE * .36f);
static float mtof(float m) { return 440.f * powf(2.f, (m - 69) / 12.f); }
static inline float adv(float *ph, float f) { *ph += f * (1.f / AUDIO_RATE); if (*ph >= 1) *ph -= 1; return *ph; }
static inline float pul(float *ph, float f, float duty) { return adv(ph, f) < duty ? 1.f : -1.f; }
static inline float tri(float *ph, float f) { return 4.f * fabsf(adv(ph, f) - .5f) - 1.f; }
static inline float sine(float *ph, float f) { return tsin(adv(ph, f)); }

static float lead_note(int bar, int step, int *deg) {
    const int *ph = (bar / 2) % 2 == 0 ? lead_a : lead_b;
    int d = ph[(bar % 2) * 8 + step]; *deg = d; if (!d) return 0;
    float r = (float)chord_root[bar] + 12;
    switch (d) { case 1: return r; case 2: return r + chord_third[bar]; case 3: return r + 7; case 4: return r + 12; default: return r + 14; }
}

#define DEC(rate) (1.f - (rate) / AUDIO_RATE)         /* cheap per-sample exponential decay factor */
static int last_step = -1;
static float f_pad[4], f_bass, f_arp, f_lead; static int has_bass, has_arp, has_lead, has_kick, has_snare;
static float e_bass, e_arp, e_lead, e_kick, e_kf, e_snare, e_hat, hat_amp, lt_acc;

static void music_sample(float *outL, float *outR) {
    float step_len = 60.f / BPM / 2.f;
    mt += 1.0 / AUDIO_RATE;
    double total = 16 * 8 * step_len; if (mt >= total) mt -= total;
    int gstep = (int)(mt / step_len);
    int bar = gstep / 8, step = gstep % 8;
    intensity += (intensity_target - intensity) * (1.f / (AUDIO_RATE * 1.2f));
    int battle = intensity > .5f;

    if (gstep != last_step) {                      /* new 8th-note step: recompute everything once */
        last_step = gstep; lt_acc = 0;
        float root = (float)chord_root[bar], third = root + chord_third[bar], fifth = root + chord_fifth[bar];
        f_pad[0] = mtof(root + 12); f_pad[1] = mtof(third + 12); f_pad[2] = mtof(fifth + 12); f_pad[3] = mtof(root);
        static const int bass_pat[8] = { 1,0,0,1,1,0,1,0 };
        has_bass = bass_pat[step]; f_bass = mtof(root - 12 + ((step == 6) ? 7 : 0)); if (has_bass) e_bass = 1.f;
        static const int arp[8] = { 0,1,2,1,0,2,1,2 };
        float an = arp[step] == 0 ? root + 12 : (arp[step] == 1 ? third + 12 : fifth + 12);
        has_arp = (step % 2 == 0 || battle); f_arp = mtof(an + 12); e_arp = 1.f;
        int deg; float ln = lead_note(bar, step, &deg); has_lead = deg; if (deg) { f_lead = mtof(ln); e_lead = 1.f; }
        has_kick = (step == 0 || step == 4 || (step == 3 && (bar % 2))); if (has_kick) { e_kick = 1.f; e_kf = 1.f; }
        has_snare = (step == 2 || step == 6); if (has_snare) e_snare = 1.f;
        e_hat = 1.f; hat_amp = (step % 2) ? .06f : .035f;
    }
    lt_acc += 1.f / AUDIO_RATE;
    float v = 0, lead = 0;

    float padg = .035f;
    v += (tri(&mph[0], f_pad[0]) + tri(&mph[1], f_pad[1]) + tri(&mph[2], f_pad[2])) * padg;
    v += sine(&mph[3], f_pad[3]) * .03f;

    if (has_bass) { v += (sine(&mph[4], f_bass) * .8f + tri(&mph[5], f_bass * 2) * .2f) * e_bass * (battle ? .24f : .18f); e_bass *= DEC(5.f); }
    if (has_arp)  { v += (sine(&mph[6], f_arp) * .7f + tri(&mph[7], f_arp) * .3f) * e_arp * .075f; e_arp *= DEC(9.f); }

    if (has_lead) {
        float vib = 1.f + tsin(adv(&vib_ph, .875f)) * .004f;        /* ~5.5 rad/s */
        float f = f_lead * vib;
        float att = lt_acc < .01f ? lt_acc * 100.f : 1.f;
        lead = (pul(&mph[8], f, .3f) * .5f + sine(&mph[9], f) * .5f) * e_lead * att * (battle ? .09f : .045f);
        e_lead *= DEC(3.2f);
        v += lead;
    }
    if (battle) {
        if (has_kick) { kick_ph += (110.f * e_kf + 42.f) * (1.f / AUDIO_RATE); if (kick_ph > 1) kick_ph -= 1; v += tsin(kick_ph) * e_kick * .32f; e_kf *= DEC(18.f); e_kick *= DEC(11.f); }
        if (has_snare) { float n = rnd(); snare_lp += (n - snare_lp) * .5f; v += (n - snare_lp) * e_snare * .12f; e_snare *= DEC(18.f); }
        float n = rnd(); hat_lp += (n - hat_lp) * .6f; v += (n - hat_lp) * e_hat * hat_amp; e_hat *= DEC(45.f);
    }
    v *= .85f;
    float dr = dl[0][dlpos], dr2 = dl[1][dlpos];
    dl[1][dlpos] = lead * 1.0f + dr * .42f; dl[0][dlpos] = (v - lead) * .12f + dr2 * .42f;
    dlpos++; if (dlpos >= DLEN) dlpos = 0;
    *outL = v + dr * .8f; *outR = v + dr2 * .8f;
}

void audio_mix(int16_t *out, int frames) {
    for (int i = 0; i < frames; i++) {
        float m = 0;
        for (int v = 0; v < NVOICE; v++) {
            Voice *vo = &voices[v];
            if (vo->id >= 0 && vo->pos < bank_len[vo->id]) m += bank[vo->id][vo->pos++] / 32768.f * vo->vol;
        }
        float l = m, r = m;
        if (music_on) { float ml, mr; music_sample(&ml, &mr); l += ml * .9f; r += mr * .9f; }
        l *= 1.12f; r *= 1.12f;
        l = l > 1 ? 1 : (l < -1 ? -1 : l); r = r > 1 ? 1 : (r < -1 ? -1 : r);
        out[i * 2] = (int16_t)(l * 30000); out[i * 2 + 1] = (int16_t)(r * 30000);
    }
}
