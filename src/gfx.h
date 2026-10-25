#pragma once
#include <stdint.h>

/* Framebuffer: 32-bit words 0xRRGGBBAA (alpha of stored pixels is always 255). */
typedef struct { uint32_t *px; int w, h; } Fb;

#define RGBA(r,g,b,a) ((((uint32_t)(r))<<24)|(((uint32_t)(g))<<16)|(((uint32_t)(b))<<8)|((uint32_t)(a)))
#define RGB(r,g,b) RGBA(r,g,b,255)
#define WITH_A(c,a) (((c)&0xFFFFFF00u)|((uint32_t)(a)))

void gfx_set_lowq(int q);              /* 1 = cheap mode: no ink outlines */
int  gfx_lowq(void);
float fsq(float x);                    /* fast sqrt (no libm) */
void gfx_set_alpha(float a);
void gfx_set_outline(float w, uint32_t col);  /* >0: every opaque shape gets a dark ink outline */
enum { FONT_UI = 0, FONT_DISPLAY = 1 };
void gfx_font(int id);                 /* global alpha multiplier (ghosts, fades) */
uint32_t gfx_mix(uint32_t a, uint32_t b, float t);

void fb_clear(Fb *f, uint32_t c);
void fb_rect(Fb *f, float x, float y, float w, float h, uint32_t c);
void fb_circle(Fb *f, float cx, float cy, float r, uint32_t c);
void fb_ring(Fb *f, float cx, float cy, float r, float thick, uint32_t c);
void fb_ellipse(Fb *f, float cx, float cy, float rx, float ry, float ang, uint32_t c);
void fb_rrect(Fb *f, float x, float y, float w, float h, float r, uint32_t c);
void fb_line(Fb *f, float x0, float y0, float x1, float y1, float thick, uint32_t c);
void fb_tri(Fb *f, float ax, float ay, float bx, float by, float cx, float cy, uint32_t c);
/* isoceles triangle: base centre at (x,y), pointing along `ang`, length len, base width wid */
void fb_spike(Fb *f, float x, float y, float len, float wid, float ang, uint32_t c);
void fb_vgrad(Fb *f, float x, float y, float w, float h, uint32_t top, uint32_t bot);
void fb_copy(Fb *dst, const Fb *src);

/* text: y is the vertical centre of the line. align: 0 left, 1 centre, 2 right */
void  gfx_init(void);
void  fb_text(Fb *f, const char *s, float x, float y, int size, uint32_t c, int align);
void  fb_text_shadow(Fb *f, const char *s, float x, float y, int size, uint32_t c, int align);
float text_width(const char *s, int size);
