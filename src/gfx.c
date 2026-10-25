#include "gfx.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#include "font_data.h"
static int g_font = 0;
void gfx_font(int id) { g_font = id; }

static float g_alpha = 1.0f;
static int g_alpha_i = 256;                 /* 0..256 fixed point */
void gfx_set_alpha(float a) { g_alpha = a; g_alpha_i = (int)(a * 256.f + .5f); if (g_alpha_i > 256) g_alpha_i = 256; if (g_alpha_i < 0) g_alpha_i = 0; }
static float g_ol = 0; static uint32_t g_olcol = 0;
static int g_lowq = 0;
void gfx_set_lowq(int q) { g_lowq = q; }
int gfx_lowq(void) { return g_lowq; }
void gfx_set_outline(float w, uint32_t col) { g_ol = w; g_olcol = col; }

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline int imax(int a, int b) { return a > b ? a : b; }
static inline int imin(int a, int b) { return a < b ? a : b; }

/* The Espresso CPU has no hardware sqrt; use a bit-trick estimate + two Newton steps. */
float fsq(float x) {
    if (x <= 1e-12f) return 0.f;
    union { float f; uint32_t i; } u; u.f = x;
    u.i = 0x5f375a86u - (u.i >> 1);
    float y = u.f;
    y = y * (1.5f - 0.5f * x * y * y);
    y = y * (1.5f - 0.5f * x * y * y);
    return x * y;
}

uint32_t gfx_mix(uint32_t a, uint32_t b, float t) {
    int r = (int)(((a >> 24) & 255) * (1 - t) + ((b >> 24) & 255) * t);
    int g = (int)(((a >> 16) & 255) * (1 - t) + ((b >> 16) & 255) * t);
    int bl = (int)(((a >> 8) & 255) * (1 - t) + ((b >> 8) & 255) * t);
    int al = (int)((a & 255) * (1 - t) + (b & 255) * t);
    return RGBA(r, g, bl, al);
}

/* blend colour c with 0..256 weight w over the pixel */
static inline void blend(uint32_t *p, uint32_t c, int w) {
    if (w <= 0) return;
    if (w >= 256) { *p = c | 0xFFu; return; }
    uint32_t d = *p; int iw = 256 - w;
    uint32_t r = (((c >> 24) & 255) * w + ((d >> 24) & 255) * iw) >> 8;
    uint32_t g = (((c >> 16) & 255) * w + ((d >> 16) & 255) * iw) >> 8;
    uint32_t b = (((c >> 8) & 255) * w + ((d >> 8) & 255) * iw) >> 8;
    *p = (r << 24) | (g << 16) | (b << 8) | 0xFFu;
}
/* base weight (0..256) of a colour including global alpha */
static inline int basew(uint32_t c) { int a = (int)(c & 255); a += a >> 7; return (a * g_alpha_i) >> 8; }
/* coverage-scaled put: cov float 0..1 */
static inline void put(Fb *f, int x, int y, uint32_t c, float cov) {
    int w = (int)(cov * (float)basew(c) + .5f);
    blend(&f->px[y * f->w + x], c & 0xFFFFFF00u, w);
}
static inline void putw(Fb *f, int x, int y, uint32_t c, int bw, float cov) {
    int w = (int)(cov * (float)bw + .5f);
    blend(&f->px[y * f->w + x], c & 0xFFFFFF00u, w);
}
/* fill a horizontal run [xa,xb) on row y with full weight bw */
static inline void run(Fb *f, int y, int xa, int xb, uint32_t c, int bw) {
    if (xa < 0) xa = 0;
    if (xb > f->w) xb = f->w;
    if (xa >= xb || y < 0 || y >= f->h) return;
    uint32_t *p = &f->px[y * f->w];
    uint32_t cc = c & 0xFFFFFF00u;
    if (bw >= 256) { uint32_t v = cc | 0xFFu; for (int x = xa; x < xb; x++) p[x] = v; }
    else for (int x = xa; x < xb; x++) blend(&p[x], cc, bw);
}

void fb_clear(Fb *f, uint32_t c) {
    uint32_t v = (c & 0xFFFFFF00u) | 0xFFu;
    int n = f->w * f->h;
    for (int i = 0; i < n; i++) f->px[i] = v;
}

void fb_copy(Fb *dst, const Fb *src) { memcpy(dst->px, src->px, (size_t)src->w * src->h * 4); }

void fb_rect(Fb *f, float x, float y, float w, float h, uint32_t c) {
    int x0 = (int)floorf(x), y0 = (int)floorf(y), x1 = (int)ceilf(x + w), y1 = (int)ceilf(y + h);
    x0 = imax(x0, 0); y0 = imax(y0, 0); x1 = imin(x1, f->w); y1 = imin(y1, f->h);
    int bw = basew(c);
    int ix0 = (int)ceilf(x), ix1 = (int)floorf(x + w);          /* fully covered columns */
    for (int yy = y0; yy < y1; yy++) {
        float vy = fminf(y + h, yy + 1.f) - fmaxf(y, (float)yy);
        if (vy >= .999f && ix1 > ix0) {
            for (int xx = x0; xx < ix0 && xx < x1; xx++) putw(f, xx, yy, c, bw, fminf(x + w, xx + 1.f) - fmaxf(x, (float)xx));
            run(f, yy, ix0, ix1, c, bw);
            for (int xx = imax(ix1, x0); xx < x1; xx++) if (xx >= ix0) putw(f, xx, yy, c, bw, fminf(x + w, xx + 1.f) - fmaxf(x, (float)xx));
        } else {
            for (int xx = x0; xx < x1; xx++) {
                float vx = fminf(x + w, xx + 1.f) - fmaxf(x, (float)xx);
                putw(f, xx, yy, c, bw, vx * vy);
            }
        }
    }
}

void fb_vgrad(Fb *f, float x, float y, float w, float h, uint32_t top, uint32_t bot) {
    int y0 = imax((int)y, 0), y1 = imin((int)(y + h), f->h);
    int x0 = imax((int)x, 0), x1 = imin((int)(x + w), f->w);
    for (int yy = y0; yy < y1; yy++) {
        uint32_t c = gfx_mix(top, bot, (yy - y) / h);
        uint32_t v = (c & 0xFFFFFF00u) | 0xFFu;
        for (int xx = x0; xx < x1; xx++) f->px[yy * f->w + xx] = v;
    }
}

static void core_circle(Fb *f, float cx, float cy, float r, uint32_t c) {
    int y0 = imax((int)floorf(cy - r - 1), 0), y1 = imin((int)ceilf(cy + r + 1), f->h);
    float in = r - 1.f; float in2 = in > 0 ? in * in : -1.f;
    float out = r + 1.f, out2 = out * out;
    int bw = basew(c);
    for (int y = y0; y < y1; y++) {
        float dy = y + .5f - cy; float dy2 = dy * dy;
        if (dy2 >= out2) continue;
        float ho = fsq(out2 - dy2);
        int xa = imax((int)floorf(cx - ho), 0), xb = imin((int)ceilf(cx + ho), f->w);
        int sa = xb, sb = xb;                                  /* solid span */
        if (dy2 < in2) { float hi = fsq(in2 - dy2); sa = (int)ceilf(cx - hi - .5f); sb = (int)floorf(cx + hi - .5f) + 1; if (sa < xa) sa = xa; if (sb > xb) sb = xb; if (sb < sa) sb = sa; }
        for (int x = xa; x < sa; x++) { float dx = x + .5f - cx; float cov = clampf(r - fsq(dx * dx + dy2) + .5f, 0, 1); if (cov > 0) putw(f, x, y, c, bw, cov); }
        run(f, y, sa, sb, c, bw);
        for (int x = sb; x < xb; x++) { float dx = x + .5f - cx; float cov = clampf(r - fsq(dx * dx + dy2) + .5f, 0, 1); if (cov > 0) putw(f, x, y, c, bw, cov); }
    }
}

void fb_ring(Fb *f, float cx, float cy, float r, float thick, uint32_t c) {
    float R = r + thick / 2 + 1;
    float ri = r - thick / 2 - 1; float ri2 = ri > 0 ? ri * ri : -1.f;
    int y0 = imax((int)floorf(cy - R), 0), y1 = imin((int)ceilf(cy + R), f->h);
    int bw = basew(c);
    for (int y = y0; y < y1; y++) {
        float dy = y + .5f - cy; float dy2 = dy * dy;
        float ho = fsq(R * R - dy2);
        int xa = imax((int)floorf(cx - ho), 0), xb = imin((int)ceilf(cx + ho), f->w);
        int ha = xb, hb = xb;                                  /* hole */
        if (dy2 < ri2) { float hi = fsq(ri2 - dy2); ha = (int)ceilf(cx - hi - .5f); hb = (int)floorf(cx + hi - .5f) + 1; if (ha < xa) ha = xa; if (hb > xb) hb = xb; if (hb < ha) hb = ha; }
        for (int x = xa; x < xb; x++) {
            if (hb > ha && x == ha) { x = hb - 1; continue; }
            float dx = x + .5f - cx;
            float d = fabsf(fsq(dx * dx + dy2) - r) - thick / 2;
            float cov = clampf(.5f - d, 0, 1);
            if (cov > 0) putw(f, x, y, c, bw, cov);
        }
    }
}

static void core_ellipse(Fb *f, float cx, float cy, float rx, float ry, float ang, uint32_t c) {
    float R = fmaxf(rx, ry) + 1;
    int y0 = imax((int)floorf(cy - R), 0), y1 = imin((int)ceilf(cy + R), f->h);
    float ca = cosf(ang), sa = sinf(ang);
    float irx2 = 1.f / (rx * rx), iry2 = 1.f / (ry * ry);
    float qa = ca * ca * irx2 + sa * sa * iry2, qb = 2.f * ca * sa * (irx2 - iry2), qc = sa * sa * irx2 + ca * ca * iry2;
    float mn = fminf(rx, ry); float lim = (1.f + 1.5f / mn); lim *= lim;
    float inl = (mn > 3.f) ? (1.f - 1.5f / mn) : -1.f; inl = inl > 0 ? inl * inl : -1.f;
    int bw = basew(c);
    float inv2a = 1.f / (2.f * qa);
    for (int y = y0; y < y1; y++) {
        float dy = y + .5f - cy;
        /* outer x-range where q2 <= lim */
        float bdy = qb * dy, cdy = qc * dy * dy;
        float disc = bdy * bdy - 4.f * qa * (cdy - lim);
        if (disc < 0) continue;
        float sq = fsq(disc);
        int xa = imax((int)floorf(cx + (-bdy - sq) * inv2a - .5f), 0), xb = imin((int)ceilf(cx + (-bdy + sq) * inv2a + .5f) + 1, f->w);
        int ia = xb, ib = xb;                                   /* solid inner span [ia,ib) */
        if (inl > 0) {
            float d2 = bdy * bdy - 4.f * qa * (cdy - inl);
            if (d2 > 0) { float s2 = fsq(d2); ia = (int)ceilf(cx + (-bdy - s2) * inv2a - .5f); ib = (int)floorf(cx + (-bdy + s2) * inv2a - .5f) + 1; if (ia < xa) ia = xa; if (ib > xb) ib = xb; if (ib < ia) ib = ia; }
        }
        for (int x = xa; x < xb; x++) {
            if (ib > ia && x == ia) { run(f, y, ia, ib, c, bw); x = ib - 1; continue; }
            float dx = x + .5f - cx;
            float lx = dx * ca + dy * sa, ly = -dx * sa + dy * ca;
            float q2 = lx * lx * irx2 + ly * ly * iry2;
            if (q2 > lim) continue;
            float q = fsq(q2);
            float g = fsq(lx * lx * irx2 * irx2 + ly * ly * iry2 * iry2) / (q > 1e-6f ? q : 1e-6f);
            float d = (q - 1.f) / (g > 1e-6f ? g : 1e-6f);
            float cov = clampf(.5f - d, 0, 1);
            if (cov > 0) putw(f, x, y, c, bw, cov);
        }
    }
}

static void core_rrect(Fb *f, float x, float y, float w, float h, float r, uint32_t c) {
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    int x0 = imax((int)floorf(x - 1), 0), x1 = imin((int)ceilf(x + w + 1), f->w);
    int y0 = imax((int)floorf(y - 1), 0), y1 = imin((int)ceilf(y + h + 1), f->h);
    float hx = w / 2, hy = h / 2, cx = x + hx, cy = y + hy;
    int bw = basew(c);
    float solid = r - 1.f;
    for (int yy = y0; yy < y1; yy++) {
        float dy = fabsf(yy + .5f - cy) - (hy - r);
        /* columns where dx <= -1 ... fully inside if max(dx,dy) <= r-1 and no corner */
        int sa = x1, sb = x1;
        if (dy <= solid) {
            float lim = solid;                                  /* dx <= lim  → solid */
            float span = (hx - r) + lim;
            sa = (int)ceilf(cx - span - .5f); sb = (int)floorf(cx + span - .5f) + 1;
            if (sa < x0) { sa = x0; }
            if (sb > x1) { sb = x1; }
            if (sb < sa) { sb = sa; }
        }
        for (int xx = x0; xx < x1; xx++) {
            if (sb > sa && xx == sa) { run(f, yy, sa, sb, c, bw); xx = sb - 1; continue; }
            float dx = fabsf(xx + .5f - cx) - (hx - r);
            float d;
            if (dx > 0 && dy > 0) d = fsq(dx * dx + dy * dy) - r;
            else d = fmaxf(dx, dy) - r;
            float cov = clampf(.5f - d, 0, 1);
            if (cov > 0) putw(f, xx, yy, c, bw, cov);
        }
    }
}

static void core_line(Fb *f, float ax, float ay, float bx, float by, float thick, uint32_t c) {
    float R = thick / 2 + 1;
    int x0 = imax((int)floorf(fminf(ax, bx) - R), 0), x1 = imin((int)ceilf(fmaxf(ax, bx) + R), f->w);
    int y0 = imax((int)floorf(fminf(ay, by) - R), 0), y1 = imin((int)ceilf(fmaxf(ay, by) + R), f->h);
    float vx = bx - ax, vy = by - ay; float L2 = vx * vx + vy * vy;
    float ht = thick / 2, R2 = (ht + 1.f) * (ht + 1.f);
    float in = ht - 1.f; float in2 = in > 0 ? in * in : -1.f;
    int bw = basew(c);
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            float px = x + .5f - ax, py = y + .5f - ay;
            float t = L2 > 0 ? clampf((px * vx + py * vy) / L2, 0, 1) : 0;
            float dx = px - vx * t, dy = py - vy * t;
            float d2 = dx * dx + dy * dy;
            if (d2 > R2) continue;
            if (d2 < in2) { blend(&f->px[y * f->w + x], c & 0xFFFFFF00u, bw); continue; }
            float cov = clampf(.5f - (fsq(d2) - ht), 0, 1);
            if (cov > 0) putw(f, x, y, c, bw, cov);
        }
    }
}

static void core_tri(Fb *f, float ax, float ay, float bx, float by, float cx, float cy, float infl, uint32_t c) {
    float area = (bx - ax) * (cy - ay) - (cx - ax) * (by - ay);
    if (fabsf(area) < 1e-4f) return;
    if (area < 0) { float tx = bx, ty = by; bx = cx; by = cy; cx = tx; cy = ty; }
    float ex[3] = { bx - ax, cx - bx, ax - cx }, ey[3] = { by - ay, cy - by, ay - cy };
    float ox[3] = { ax, bx, cx }, oy[3] = { ay, by, cy };
    float inv[3];
    for (int i = 0; i < 3; i++) { float l = fsq(ex[i] * ex[i] + ey[i] * ey[i]); inv[i] = 1.f / (l > 1e-6f ? l : 1e-6f); }
    float pad_ = 1 + infl * 2.5f;
    int x0 = imax((int)floorf(fminf(ax, fminf(bx, cx)) - pad_), 0), x1 = imin((int)ceilf(fmaxf(ax, fmaxf(bx, cx)) + pad_), f->w);
    int y0 = imax((int)floorf(fminf(ay, fminf(by, cy)) - pad_), 0), y1 = imin((int)ceilf(fmaxf(ay, fmaxf(by, cy)) + pad_), f->h);
    int bw = basew(c);
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            float px = x + .5f, py = y + .5f, dmin = 1e9f;
            for (int i = 0; i < 3; i++) {
                float d = (ex[i] * (py - oy[i]) - ey[i] * (px - ox[i])) * inv[i];
                if (d < dmin) dmin = d;
            }
            float cov = clampf(dmin + .5f + infl, 0, 1);
            if (cov > 0) putw(f, x, y, c, bw, cov);
        }
    }
}

void fb_tri(Fb *f, float ax, float ay, float bx, float by, float cx, float cy, uint32_t c) {
    if (g_ol > 0 && !g_lowq && (c & 255) >= 200) core_tri(f, ax, ay, bx, by, cx, cy, g_ol, g_olcol);
    core_tri(f, ax, ay, bx, by, cx, cy, 0, c);
}

void fb_spike(Fb *f, float x, float y, float len, float wid, float ang, uint32_t c) {
    float ca = cosf(ang), sa = sinf(ang);
    float tx = x + ca * len, ty = y + sa * len;
    float nx = -sa * wid / 2, ny = ca * wid / 2;
    fb_tri(f, x + nx, y + ny, x - nx, y - ny, tx, ty, c);
}


#define OL (g_ol > 0 && !g_lowq && (c & 255) >= 200)
void fb_circle(Fb *f, float cx, float cy, float r, uint32_t c) { if (OL) core_circle(f, cx, cy, r + g_ol, g_olcol); core_circle(f, cx, cy, r, c); }
void fb_ellipse(Fb *f, float cx, float cy, float rx, float ry, float ang, uint32_t c) { if (OL) core_ellipse(f, cx, cy, rx + g_ol, ry + g_ol, ang, g_olcol); core_ellipse(f, cx, cy, rx, ry, ang, c); }
void fb_rrect(Fb *f, float x, float y, float w, float h, float r, uint32_t c) { if (OL) core_rrect(f, x - g_ol, y - g_ol, w + 2 * g_ol, h + 2 * g_ol, r + g_ol, g_olcol); core_rrect(f, x, y, w, h, r, c); }
void fb_line(Fb *f, float ax, float ay, float bx, float by, float thick, uint32_t c) { if (OL) core_line(f, ax, ay, bx, by, thick + 2 * g_ol, g_olcol); core_line(f, ax, ay, bx, by, thick, c); }

/* ---------- text ---------- */
typedef struct { int font, size, cp; unsigned char *bmp; int w, h, xo, yo; float adv; int used; } Glyph;
#define GSLOTS 8192
static Glyph gcache[GSLOTS];
static stbtt_fontinfo gfonts[2];
static int gfont_ok = 0;

void gfx_init(void) {
    if (stbtt_InitFont(&gfonts[0], font_ui, 0) && stbtt_InitFont(&gfonts[1], font_display, 0)) gfont_ok = 1;
}

static Glyph *getglyph(int size, int cp) {
    unsigned h = ((unsigned)cp * 2654435761u + (unsigned)size * 40503u + (unsigned)g_font * 7919u) & (GSLOTS - 1);
    stbtt_fontinfo *gfont = &gfonts[g_font];
    for (int i = 0; i < GSLOTS; i++) {
        Glyph *g = &gcache[(h + i) & (GSLOTS - 1)];
        if (!g->used) {
            float sc = stbtt_ScaleForPixelHeight(gfont, (float)size);
            int adv, lsb;
            stbtt_GetCodepointHMetrics(gfont, cp, &adv, &lsb);
            g->used = 1; g->font = g_font; g->size = size; g->cp = cp; g->adv = adv * sc;
            g->bmp = stbtt_GetCodepointBitmap(gfont, 0, sc, cp, &g->w, &g->h, &g->xo, &g->yo);
            return g;
        }
        if (g->size == size && g->cp == cp && g->font == g_font) return g;
    }
    return &gcache[0];
}

static int utf8next(const char **s) {
    const unsigned char *p = (const unsigned char *)*s;
    int c = *p;
    if (c < 0x80) { *s += 1; return c; }
    if ((c & 0xE0) == 0xC0 && p[1]) { *s += 2; return ((c & 0x1F) << 6) | (p[1] & 0x3F); }
    if ((c & 0xF0) == 0xE0 && p[1] && p[2]) { *s += 3; return ((c & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F); }
    if (p[1] && p[2] && p[3]) { *s += 4; return ((c & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F); }
    *s += 1; return '?';
}

float text_width(const char *s, int size) {
    if (!gfont_ok) return 0;
    float w = 0;
    while (*s) { int cp = utf8next(&s); w += getglyph(size, cp)->adv; }
    return w;
}

void fb_text(Fb *f, const char *s, float x, float y, int size, uint32_t c, int align) {
    if (!gfont_ok) return;
    float w = text_width(s, size);
    if (align == 1) x -= w / 2; else if (align == 2) x -= w;
    float baseline = y + size * 0.36f;
    int tw = basew(c);
    float pen = x;
    while (*s) {
        int cp = utf8next(&s);
        Glyph *g = getglyph(size, cp);
        if (g->bmp) {
            int gx = (int)floorf(pen) + g->xo, gy = (int)floorf(baseline) + g->yo;
            for (int yy = 0; yy < g->h; yy++) {
                int py = gy + yy; if (py < 0 || py >= f->h) continue;
                for (int xx = 0; xx < g->w; xx++) {
                    int px = gx + xx; if (px < 0 || px >= f->w) continue;
                    unsigned char a = g->bmp[yy * g->w + xx];
                    if (a) putw(f, px, py, c, tw, a / 255.f);
                }
            }
        }
        pen += g->adv;
    }
}

void fb_text_shadow(Fb *f, const char *s, float x, float y, int size, uint32_t c, int align) {
    fb_text(f, s, x + 2, y + 2, size, RGBA(24, 14, 16, 190), align);
    fb_text(f, s, x, y, size, c, align);
}
