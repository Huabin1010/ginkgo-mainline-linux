/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef GEMINI_STATUS_GPU_H
#define GEMINI_STATUS_GPU_H

#include <poll.h>
#include <stdint.h>

struct gs_font;
struct gs_glyph;

int hud_gl_init(int *w, int *h);
void hud_gl_pack_font(const struct gs_font *f);
int hud_gl_finish(void);
void hud_gl_begin(void);
void hud_gl_set_view(float scale, float xoff, float yoff);
void hud_gl_fill_rect(int x, int y, int w, int h, uint32_t c);
void hud_gl_fill_rect_a(int x, int y, int w, int h, uint32_t c, int a);
void hud_gl_fill_round(int x, int y, int w, int h, int r, uint32_t c);
void hud_gl_glyph(const struct gs_font *f, const struct gs_glyph *g,
		  int x, int y, uint32_t c);
void hud_gl_present(void);
void hud_gl_stats(int *nverts, int *nflushes, int *wait_us, int *wait_to);
void hud_gl_set_idle(void (*fn)(void));
void hud_gl_set_poll(const struct pollfd *fds, int n);
void hud_gl_blank(int off);
void hud_gl_shutdown(void);

#endif
