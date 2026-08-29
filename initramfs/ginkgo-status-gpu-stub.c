/* SPDX-License-Identifier: GPL-2.0-only */
/* CPU-only fallback when the GLES sysroot is not available at build time. */
#include "ginkgo-status-gpu.h"

int hud_gl_init(int *w, int *h)
{
	(void)w;
	(void)h;
	return -1;
}

void hud_gl_pack_font(const struct gs_font *f)
{
	(void)f;
}

int hud_gl_finish(void)
{
	return -1;
}

void hud_gl_begin(void)
{
}

void hud_gl_set_view(float scale, float xoff, float yoff)
{
	(void)scale;
	(void)xoff;
	(void)yoff;
}

void hud_gl_fill_rect(int x, int y, int w, int h, uint32_t c)
{
	(void)x;
	(void)y;
	(void)w;
	(void)h;
	(void)c;
}

void hud_gl_fill_rect_a(int x, int y, int w, int h, uint32_t c, int a)
{
	(void)x;
	(void)y;
	(void)w;
	(void)h;
	(void)c;
	(void)a;
}

void hud_gl_fill_round(int x, int y, int w, int h, int r, uint32_t c)
{
	(void)x;
	(void)y;
	(void)w;
	(void)h;
	(void)r;
	(void)c;
}

void hud_gl_glyph(const struct gs_font *f, const struct gs_glyph *g,
		  int x, int y, uint32_t c)
{
	(void)f;
	(void)g;
	(void)x;
	(void)y;
	(void)c;
}

void hud_gl_present(void)
{
}

void hud_gl_stats(int *nverts, int *nflushes, int *wait_us, int *wait_to)
{
	if (nverts)
		*nverts = 0;
	if (nflushes)
		*nflushes = 0;
	if (wait_us)
		*wait_us = 0;
	if (wait_to)
		*wait_to = 0;
}

void hud_gl_set_idle(void (*fn)(void))
{
	(void)fn;
}

void hud_gl_set_poll(const struct pollfd *fds, int n)
{
	(void)fds;
	(void)n;
}

void hud_gl_blank(int off)
{
	(void)off;
}

void hud_gl_shutdown(void)
{
}
