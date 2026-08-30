/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * GBM + EGL + GLES2 scanout for ginkgo-status.
 * CPU only builds the display list; Adreno 610 rasterizes.
 */
#define _GNU_SOURCE
#define EGL_NO_PLATFORM_SPECIFIC_TYPES
#define EGL_EGLEXT_PROTOTYPES

#include "ginkgo-status-gpu.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <drm_fourcc.h>
#include <dirent.h>
#include <fcntl.h>
#include <gbm.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <poll.h>

#ifndef EGL_PLATFORM_GBM_KHR
#define EGL_PLATFORM_GBM_KHR 0x31D7
#endif

/* Must match ginkgo-status-font.h */
struct gs_glyph {
	uint32_t cp;
	uint16_t w, h, adv;
	int16_t xoff, yoff;
	uint32_t off;
};

struct gs_font {
	int px, ascent, descent, n;
	const struct gs_glyph *g;
	const uint8_t *bits;
};

#define MAX_VTX 24576
#define MAX_FONTS 4
#define ATLAS_W 2048
#define ATLAS_H 2048

struct vtx {
	float x, y, u, v;
	float r, g, b, a;
	float ew, eh, er, em;
};

struct packed_font {
	const struct gs_font *f;
	uint16_t *x, *y;
};

static int kms_fd = -1;
static uint32_t conn_id, crtc_id;
static drmModeModeInfo mode;
static int gl_w, gl_h, crtc_set, flip_done;
static int gl_runtime_off;
static struct gbm_device *gbm;
static struct gbm_surface *gsurf;
static struct gbm_bo *bo_prev, *bo_cur;
static EGLDisplay egl_dpy = EGL_NO_DISPLAY;
static EGLContext egl_ctx = EGL_NO_CONTEXT;
static EGLSurface egl_surf = EGL_NO_SURFACE;
static GLuint prog, vbo, tex_atlas;
static GLint u_screen, u_tex, u_view;
static float view_scale = 1.f, view_ox, view_oy;
static struct vtx vtx[MAX_VTX];
static int nverts, ready;
static uint8_t *atlas;
static int pack_x = 1, pack_y = 1, pack_row_h, atlas_fail;
static struct packed_font fonts[MAX_FONTS];
static int nfonts;
static void (*gl_idle)(void);
#define GL_EXTRA_MAX 8
static struct pollfd gl_extra[GL_EXTRA_MAX];
static int gl_nextra;
static int stat_nverts, stat_nflushes, stat_wait_us, stat_wait_to;

void hud_gl_set_idle(void (*fn)(void))
{
	gl_idle = fn;
}

void hud_gl_set_poll(const struct pollfd *fds, int n)
{
	if (n > GL_EXTRA_MAX)
		n = GL_EXTRA_MAX;
	if (n < 0)
		n = 0;
	if (n && fds)
		memcpy(gl_extra, fds, (size_t)n * sizeof(gl_extra[0]));
	gl_nextra = n;
}

static const char *VS =
	"attribute vec2 a_pos;\n"
	"attribute vec2 a_uv;\n"
	"attribute vec4 a_color;\n"
	"attribute vec4 a_extra;\n"
	"uniform vec2 u_screen;\n"
	"uniform vec3 u_view;\n"
	"varying vec2 v_uv;\n"
	"varying vec4 v_color;\n"
	"varying vec4 v_extra;\n"
	"void main(){\n"
	"  vec2 mid = u_screen * 0.5;\n"
	"  vec2 p = (a_pos - mid) * u_view.x + mid + vec2(u_view.y, u_view.z);\n"
	"  gl_Position = vec4(p.x / u_screen.x * 2.0 - 1.0,\n"
	"                     1.0 - p.y / u_screen.y * 2.0, 0.0, 1.0);\n"
	"  v_uv = a_uv; v_color = a_color; v_extra = a_extra;\n"
	"}\n";

static const char *FS =
	"precision mediump float;\n"
	"varying vec2 v_uv;\n"
	"varying vec4 v_color;\n"
	"varying vec4 v_extra;\n"
	"uniform sampler2D u_tex;\n"
	"void main(){\n"
	"  if (v_extra.w < 0.5) {\n"
	"    float r = max(v_extra.z, 0.0);\n"
	"    vec2 halfsz = vec2(v_extra.x, v_extra.y) * 0.5;\n"
	"    vec2 p = v_uv - halfsz;\n"
	"    vec2 b = max(halfsz - vec2(r), vec2(0.0));\n"
	"    vec2 q = abs(p) - b;\n"
	"    float d = min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;\n"
	"    float a = 1.0 - smoothstep(-0.75, 0.75, d);\n"
	"    gl_FragColor = vec4(v_color.rgb, v_color.a * a);\n"
	"  } else {\n"
	"    float a = texture2D(u_tex, v_uv).r;\n"
	"    gl_FragColor = vec4(v_color.rgb, v_color.a * a);\n"
	"  }\n"
	"}\n";

static void gl_log(const char *msg)
{
	int k = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);
	char line[320];

	snprintf(line, sizeof(line), "ginkgo-status: %s\n", msg);
	if (k >= 0) {
		(void)!write(k, line, strlen(line));
		close(k);
	}
}

static void bo_destroy_fb(struct gbm_bo *bo, void *data)
{
	uint32_t fb = (uint32_t)(uintptr_t)data;

	(void)bo;
	if (fb && kms_fd >= 0)
		drmModeRmFB(kms_fd, fb);
}

static uint32_t fb_from_bo(struct gbm_bo *bo)
{
	int i, n;
	uint32_t fb, handles[4] = { 0 }, strides[4] = { 0 }, offsets[4] = { 0 };
	uint64_t mods[4] = { 0 };
	uint32_t w, h, fmt, flags = 0;
	uint64_t mod;

	fb = (uint32_t)(uintptr_t)gbm_bo_get_user_data(bo);
	if (fb)
		return fb;

	w = gbm_bo_get_width(bo);
	h = gbm_bo_get_height(bo);
	fmt = gbm_bo_get_format(bo);
	n = gbm_bo_get_plane_count(bo);
	mod = gbm_bo_get_modifier(bo);
	if (n < 1)
		n = 1;
	if (n > 4)
		n = 4;
	for (i = 0; i < n; i++) {
		handles[i] = gbm_bo_get_handle_for_plane(bo, i).u32;
		strides[i] = gbm_bo_get_stride_for_plane(bo, i);
		offsets[i] = gbm_bo_get_offset(bo, i);
		mods[i] = mod;
	}
	if (mod && mod != DRM_FORMAT_MOD_INVALID)
		flags = DRM_MODE_FB_MODIFIERS;
	if (drmModeAddFB2WithModifiers(kms_fd, w, h, fmt, handles, strides,
				       offsets, mods, &fb, flags)) {
		if (drmModeAddFB2(kms_fd, w, h, fmt, handles, strides, offsets,
				  &fb, 0)) {
			gl_log("drmModeAddFB2 failed");
			return 0;
		}
	}
	gbm_bo_set_user_data(bo, (void *)(uintptr_t)fb, bo_destroy_fb);
	return fb;
}

static void page_flip_handler(int fd, unsigned frame, unsigned sec,
			      unsigned usec, void *data)
{
	(void)fd;
	(void)frame;
	(void)sec;
	(void)usec;
	*(int *)data = 1;
}

static void wait_flip(void)
{
	drmEventContext ev;
	struct pollfd p[1 + GL_EXTRA_MAX];
	int n, i, r, spins = 0, extra;
	struct timespec t0, t1;

	memset(&ev, 0, sizeof(ev));
	ev.version = 2;
	ev.page_flip_handler = page_flip_handler;
	clock_gettime(CLOCK_MONOTONIC, &t0);
	while (!flip_done) {
		p[0].fd = kms_fd;
		p[0].events = POLLIN;
		p[0].revents = 0;
		n = 1;
		for (i = 0; i < gl_nextra && n < 1 + GL_EXTRA_MAX; i++) {
			p[n] = gl_extra[i];
			p[n].revents = 0;
			n++;
		}
		r = poll(p, n, 64);
		if (r < 0)
			break;
		if (p[0].revents & POLLIN)
			drmHandleEvent(kms_fd, &ev);
		extra = 0;
		for (i = 1; i < n; i++) {
			if (p[i].revents & (POLLIN | POLLERR))
				extra = 1;
		}
		if (extra && gl_idle)
			gl_idle();
		if (r == 0 && ++spins > 3) {
			stat_wait_to++;
			break;
		}
	}
	clock_gettime(CLOCK_MONOTONIC, &t1);
	stat_wait_us = (int)((t1.tv_sec - t0.tv_sec) * 1000000 +
			     (t1.tv_nsec - t0.tv_nsec) / 1000);
}

static EGLConfig pick_config(EGLDisplay dpy, uint32_t format)
{
	EGLint n = 0, i;
	EGLConfig *cfgs, picked = NULL;

	eglGetConfigs(dpy, NULL, 0, &n);
	if (n < 1)
		return NULL;
	cfgs = calloc((size_t)n, sizeof(*cfgs));
	if (!cfgs)
		return NULL;
	eglGetConfigs(dpy, cfgs, n, &n);
	for (i = 0; i < n; i++) {
		EGLint id = 0, types = 0, rtype = 0;

		eglGetConfigAttrib(dpy, cfgs[i], EGL_NATIVE_VISUAL_ID, &id);
		eglGetConfigAttrib(dpy, cfgs[i], EGL_SURFACE_TYPE, &types);
		eglGetConfigAttrib(dpy, cfgs[i], EGL_RENDERABLE_TYPE, &rtype);
		if ((types & EGL_WINDOW_BIT) &&
		    (rtype & EGL_OPENGL_ES2_BIT) &&
		    (uint32_t)id == format) {
			picked = cfgs[i];
			break;
		}
	}
	free(cfgs);
	return picked;
}

static GLuint compile_shader(GLenum type, const char *src)
{
	GLuint s = glCreateShader(type);
	GLint ok = 0;
	char log[256];

	glShaderSource(s, 1, &src, NULL);
	glCompileShader(s);
	glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		glGetShaderInfoLog(s, sizeof(log), NULL, log);
		gl_log(log);
		glDeleteShader(s);
		return 0;
	}
	return s;
}

static int init_drm(int *w, int *h)
{
	drmModeRes *res;
	drmModeConnector *conn = NULL;
	drmModeEncoder *enc = NULL;
	int i, j;

	kms_fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
	if (kms_fd < 0) {
		gl_log("open card0 failed");
		return -1;
	}
	drmSetClientCap(kms_fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);

	res = drmModeGetResources(kms_fd);
	if (!res) {
		gl_log("drmModeGetResources failed");
		return -1;
	}
	for (i = 0; i < res->count_connectors; i++) {
		conn = drmModeGetConnector(kms_fd, res->connectors[i]);
		if (conn && conn->connection == DRM_MODE_CONNECTED &&
		    conn->count_modes > 0)
			break;
		drmModeFreeConnector(conn);
		conn = NULL;
	}
	if (!conn) {
		drmModeFreeResources(res);
		gl_log("no connected DRM connector");
		return -1;
	}

	conn_id = conn->connector_id;
	mode = conn->modes[0];
	for (i = 0; i < conn->count_modes; i++) {
		if (conn->modes[i].hdisplay == 1080 &&
		    conn->modes[i].vdisplay == 2340) {
			mode = conn->modes[i];
			break;
		}
	}

	if (conn->encoder_id)
		enc = drmModeGetEncoder(kms_fd, conn->encoder_id);
	if (!enc && conn->count_encoders)
		enc = drmModeGetEncoder(kms_fd, conn->encoders[0]);
	if (enc) {
		crtc_id = enc->crtc_id;
		drmModeFreeEncoder(enc);
		enc = NULL;
	}
	if (!crtc_id) {
		for (i = 0; i < conn->count_encoders && !crtc_id; i++) {
			enc = drmModeGetEncoder(kms_fd, conn->encoders[i]);
			if (!enc)
				continue;
			for (j = 0; j < res->count_crtcs; j++) {
				if (enc->possible_crtcs & (1u << j)) {
					crtc_id = res->crtcs[j];
					break;
				}
			}
			drmModeFreeEncoder(enc);
		}
	}
	if (crtc_id) {
		drmModeCrtc *c = drmModeGetCrtc(kms_fd, crtc_id);

		if (c && c->mode_valid)
			mode = c->mode;
		if (c)
			drmModeFreeCrtc(c);
	}
	if (!crtc_id || mode.hdisplay < 480 || mode.vdisplay < 480) {
		drmModeFreeConnector(conn);
		drmModeFreeResources(res);
		gl_log("no usable CRTC/mode");
		return -1;
	}
	*w = mode.hdisplay;
	*h = mode.vdisplay;
	drmModeFreeConnector(conn);
	drmModeFreeResources(res);
	return 0;
}

int hud_gl_init(int *w, int *h)
{
	EGLint major = 0, minor = 0;
	EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	EGLConfig cfg;
	uint32_t format = GBM_FORMAT_XRGB8888;
	char msg[160];

	if (init_drm(w, h))
		return -1;
	gl_w = *w;
	gl_h = *h;

	gbm = gbm_create_device(kms_fd);
	if (!gbm) {
		gl_log("gbm_create_device failed");
		return -1;
	}
	gsurf = gbm_surface_create(gbm, gl_w, gl_h, format,
				   GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
	if (!gsurf) {
		gl_log("gbm_surface_create failed");
		return -1;
	}

	egl_dpy = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, gbm, NULL);
	if (egl_dpy == EGL_NO_DISPLAY)
		egl_dpy = eglGetDisplay((EGLNativeDisplayType)gbm);
	if (egl_dpy == EGL_NO_DISPLAY || !eglInitialize(egl_dpy, &major, &minor)) {
		gl_log("eglInitialize failed");
		return -1;
	}
	if (!eglBindAPI(EGL_OPENGL_ES_API)) {
		gl_log("eglBindAPI failed");
		return -1;
	}
	cfg = pick_config(egl_dpy, format);
	if (!cfg) {
		EGLint n = 0;
		const EGLint attribs[] = {
			EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
			EGL_RED_SIZE, 8,
			EGL_GREEN_SIZE, 8,
			EGL_BLUE_SIZE, 8,
			EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
			EGL_NONE
		};

		eglChooseConfig(egl_dpy, attribs, &cfg, 1, &n);
		if (n < 1) {
			gl_log("no EGL config");
			return -1;
		}
	}
	egl_ctx = eglCreateContext(egl_dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
	egl_surf = eglCreateWindowSurface(egl_dpy, cfg,
					  (EGLNativeWindowType)gsurf, NULL);
	if (egl_ctx == EGL_NO_CONTEXT || egl_surf == EGL_NO_SURFACE ||
	    !eglMakeCurrent(egl_dpy, egl_surf, egl_surf, egl_ctx)) {
		gl_log("eglMakeCurrent failed");
		return -1;
	}
	/* DRM page flip is the vsync. Swap-interval 1 plus wait_flip
	 * stacked two waits and starved the touch fd. */
	eglSwapInterval(egl_dpy, 0);
	snprintf(msg, sizeof(msg), "gles %s / %s %dx%d",
		 glGetString(GL_RENDERER) ? (const char *)glGetString(GL_RENDERER) : "?",
		 glGetString(GL_VERSION) ? (const char *)glGetString(GL_VERSION) : "?",
		 gl_w, gl_h);
	gl_log(msg);
	return 0;
}

void hud_gl_pack_font(const struct gs_font *f)
{
	int i;
	struct packed_font *p;

	if (!f || nfonts >= MAX_FONTS || atlas_fail)
		return;
	if (!atlas) {
		atlas = calloc((size_t)ATLAS_W * (size_t)ATLAS_H, 1);
		if (!atlas) {
			atlas_fail = 1;
			return;
		}
	}
	p = &fonts[nfonts++];
	p->f = f;
	p->x = calloc((size_t)f->n, sizeof(uint16_t));
	p->y = calloc((size_t)f->n, sizeof(uint16_t));
	if (!p->x || !p->y) {
		atlas_fail = 1;
		return;
	}
	for (i = 0; i < f->n; i++) {
		const struct gs_glyph *g = &f->g[i];
		int row;

		if (g->w < 1 || g->h < 1)
			continue;
		if (pack_x + g->w + 1 >= ATLAS_W) {
			pack_x = 1;
			pack_y += pack_row_h + 1;
			pack_row_h = 0;
		}
		if (pack_y + g->h + 1 >= ATLAS_H) {
			atlas_fail = 1;
			gl_log("font atlas overflow");
			return;
		}
		p->x[i] = (uint16_t)pack_x;
		p->y[i] = (uint16_t)pack_y;
		for (row = 0; row < g->h; row++)
			memcpy(atlas + (size_t)(pack_y + row) * ATLAS_W + pack_x,
			       f->bits + g->off + (uint32_t)row * g->w, g->w);
		pack_x += g->w + 1;
		if (g->h > pack_row_h)
			pack_row_h = g->h;
	}
}

static int build_program(void)
{
	GLuint vs, fs;
	GLint ok = 0;
	char log[256];

	vs = compile_shader(GL_VERTEX_SHADER, VS);
	fs = compile_shader(GL_FRAGMENT_SHADER, FS);
	if (!vs || !fs)
		return -1;
	prog = glCreateProgram();
	glBindAttribLocation(prog, 0, "a_pos");
	glBindAttribLocation(prog, 1, "a_uv");
	glBindAttribLocation(prog, 2, "a_color");
	glBindAttribLocation(prog, 3, "a_extra");
	glAttachShader(prog, vs);
	glAttachShader(prog, fs);
	glLinkProgram(prog);
	glDeleteShader(vs);
	glDeleteShader(fs);
	glGetProgramiv(prog, GL_LINK_STATUS, &ok);
	if (!ok) {
		glGetProgramInfoLog(prog, sizeof(log), NULL, log);
		gl_log(log);
		return -1;
	}
	u_screen = glGetUniformLocation(prog, "u_screen");
	u_tex = glGetUniformLocation(prog, "u_tex");
	u_view = glGetUniformLocation(prog, "u_view");
	return 0;
}

int hud_gl_finish(void)
{
	if (atlas_fail || !atlas)
		return -1;
	if (build_program())
		return -1;
	glGenBuffers(1, &vbo);
	glGenTextures(1, &tex_atlas);
	glBindTexture(GL_TEXTURE_2D, tex_atlas);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, ATLAS_W, ATLAS_H, 0,
		     GL_LUMINANCE, GL_UNSIGNED_BYTE, atlas);
	free(atlas);
	atlas = NULL;
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glViewport(0, 0, gl_w, gl_h);
	ready = 1;
	return 0;
}

void hud_gl_begin(void)
{
	nverts = 0;
	stat_nverts = 0;
	stat_nflushes = 0;
	stat_wait_us = 0;
	stat_wait_to = 0;
	view_scale = 1.f;
	view_ox = 0.f;
	view_oy = 0.f;
	if (ready) {
		glClearColor(0.f, 0.f, 0.f, 1.f);
		glClear(GL_COLOR_BUFFER_BIT);
	}
}

static void flush_batch(void);

void hud_gl_set_view(float scale, float xoff, float yoff)
{
	flush_batch();
	if (scale < 0.05f)
		scale = 0.05f;
	view_scale = scale;
	view_ox = xoff;
	view_oy = yoff;
	if (ready && tex_atlas) {
		GLint filt = (scale < 0.999f) ? GL_LINEAR : GL_NEAREST;

		glBindTexture(GL_TEXTURE_2D, tex_atlas);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filt);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filt);
	}
}

static void bind_attribs(void)
{
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glEnableVertexAttribArray(2);
	glEnableVertexAttribArray(3);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(struct vtx),
			      (void *)offsetof(struct vtx, x));
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(struct vtx),
			      (void *)offsetof(struct vtx, u));
	glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(struct vtx),
			      (void *)offsetof(struct vtx, r));
	glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(struct vtx),
			      (void *)offsetof(struct vtx, ew));
}

static void flush_batch(void)
{
	if (nverts < 3 || !ready)
		return;
	glUseProgram(prog);
	glUniform2f(u_screen, (float)gl_w, (float)gl_h);
	if (u_view >= 0)
		glUniform3f(u_view, view_scale, view_ox, view_oy);
	glUniform1i(u_tex, 0);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, tex_atlas);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)nverts * (GLsizeiptr)sizeof(struct vtx),
		     vtx, GL_STREAM_DRAW);
	bind_attribs();
	glDrawArrays(GL_TRIANGLES, 0, nverts);
	stat_nverts += nverts;
	stat_nflushes++;
	nverts = 0;
}

static void emit_v(float x, float y, float u, float v,
		   float r, float g, float b, float a,
		   float ew, float eh, float er, float em)
{
	struct vtx *p;

	if (nverts >= MAX_VTX)
		flush_batch();
	p = &vtx[nverts++];
	p->x = x;
	p->y = y;
	p->u = u;
	p->v = v;
	p->r = r;
	p->g = g;
	p->b = b;
	p->a = a;
	p->ew = ew;
	p->eh = eh;
	p->er = er;
	p->em = em;
}

static void emit_quad(float x0, float y0, float x1, float y1,
		      float u0, float v0, float u1, float v1,
		      float r, float g, float b, float a,
		      float ew, float eh, float er, float em)
{
	emit_v(x0, y0, u0, v0, r, g, b, a, ew, eh, er, em);
	emit_v(x1, y0, u1, v0, r, g, b, a, ew, eh, er, em);
	emit_v(x0, y1, u0, v1, r, g, b, a, ew, eh, er, em);
	emit_v(x1, y0, u1, v0, r, g, b, a, ew, eh, er, em);
	emit_v(x1, y1, u1, v1, r, g, b, a, ew, eh, er, em);
	emit_v(x0, y1, u0, v1, r, g, b, a, ew, eh, er, em);
}

static void rgba(uint32_t c, float *r, float *g, float *b)
{
	*r = ((c >> 16) & 255) / 255.0f;
	*g = ((c >> 8) & 255) / 255.0f;
	*b = (c & 255) / 255.0f;
}

void hud_gl_fill_round(int x, int y, int w, int h, int r, uint32_t c)
{
	float cr, cg, cb, rr;

	if (w < 1 || h < 1)
		return;
	if (r < 0)
		r = 0;
	if (r * 2 > w)
		r = w / 2;
	if (r * 2 > h)
		r = h / 2;
	rgba(c, &cr, &cg, &cb);
	rr = (float)r;
	emit_quad((float)x, (float)y, (float)(x + w), (float)(y + h),
		  0, 0, (float)w, (float)h,
		  cr, cg, cb, 1.0f,
		  (float)w, (float)h, rr, 0.0f);
}

void hud_gl_fill_rect_a(int x, int y, int w, int h, uint32_t c, int a)
{
	float cr, cg, cb, aa;

	if (w < 1 || h < 1 || a <= 0)
		return;
	if (a > 255)
		a = 255;
	rgba(c, &cr, &cg, &cb);
	aa = (float)a / 255.0f;
	emit_quad((float)x, (float)y, (float)(x + w), (float)(y + h),
		  0, 0, (float)w, (float)h,
		  cr, cg, cb, aa,
		  (float)w, (float)h, 0.0f, 0.0f);
}

void hud_gl_fill_rect(int x, int y, int w, int h, uint32_t c)
{
	hud_gl_fill_rect_a(x, y, w, h, c, 255);
}

void hud_gl_glyph(const struct gs_font *f, const struct gs_glyph *g,
		  int x, int y, uint32_t c)
{
	int i, idx;
	struct packed_font *p = NULL;
	float cr, cg, cb, u0, v0, u1, v1;
	int dx, dy;

	if (!f || !g || g->w < 1 || g->h < 1)
		return;
	idx = (int)(g - f->g);
	if (idx < 0 || idx >= f->n)
		return;
	for (i = 0; i < nfonts; i++) {
		if (fonts[i].f == f) {
			p = &fonts[i];
			break;
		}
	}
	if (!p)
		return;
	rgba(c, &cr, &cg, &cb);
	u0 = (p->x[idx] + 0.5f) / (float)ATLAS_W;
	v0 = (p->y[idx] + 0.5f) / (float)ATLAS_H;
	u1 = (p->x[idx] + g->w - 0.5f) / (float)ATLAS_W;
	v1 = (p->y[idx] + g->h - 0.5f) / (float)ATLAS_H;
	dx = x + g->xoff;
	dy = y + g->yoff;
	emit_quad((float)dx, (float)dy, (float)(dx + g->w), (float)(dy + g->h),
		  u0, v0, u1, v1,
		  cr, cg, cb, 1.0f,
		  0, 0, 0, 1.0f);
}

void hud_gl_present(void)
{
	uint32_t fb;

	flush_batch();
	if (egl_dpy == EGL_NO_DISPLAY)
		return;
	eglSwapBuffers(egl_dpy, egl_surf);
	bo_cur = gbm_surface_lock_front_buffer(gsurf);
	if (!bo_cur) {
		gl_log("lock_front_buffer failed");
		return;
	}
	fb = fb_from_bo(bo_cur);
	if (!fb)
		return;
	if (!crtc_set) {
		if (drmModeSetCrtc(kms_fd, crtc_id, fb, 0, 0, &conn_id, 1,
				   &mode) == 0)
			crtc_set = 1;
		else
			gl_log("drmModeSetCrtc failed");
	} else {
		flip_done = 0;
		if (drmModePageFlip(kms_fd, crtc_id, fb, DRM_MODE_PAGE_FLIP_EVENT,
				    &flip_done) == 0)
			wait_flip();
	}
	if (bo_prev)
		gbm_surface_release_buffer(gsurf, bo_prev);
	bo_prev = bo_cur;
	bo_cur = NULL;
}

void hud_gl_stats(int *nverts_out, int *nflushes, int *wait_us, int *wait_to)
{
	if (nverts_out)
		*nverts_out = stat_nverts;
	if (nflushes)
		*nflushes = stat_nflushes;
	if (wait_us)
		*wait_us = stat_wait_us;
	if (wait_to)
		*wait_to = stat_wait_to;
}

static int write_file(const char *path, const char *s)
{
	int fd = open(path, O_WRONLY | O_CLOEXEC);
	ssize_t n;

	if (fd < 0)
		return -1;
	n = write(fd, s, strlen(s));
	close(fd);
	return n < 0 ? -1 : 0;
}

static int panel_dcs_sleep(int off)
{
	DIR *d = opendir("/sys/bus/mipi-dsi/devices");
	struct dirent *ent;
	char path[160];
	int ok = -1;

	if (!d)
		return -1;
	while ((ent = readdir(d))) {
		if (ent->d_name[0] == '.')
			continue;
		snprintf(path, sizeof(path),
			 "/sys/bus/mipi-dsi/devices/%s/panel_sleep", ent->d_name);
		if (access(path, W_OK) != 0)
			continue;
		ok = write_file(path, off ? "1\n" : "0\n");
		break;
	}
	closedir(d);
	return ok;
}

static int disable_crtc(void)
{
	if (kms_fd < 0 || !crtc_id)
		return -1;
	return drmModeSetCrtc(kms_fd, crtc_id, 0, 0, 0, NULL, 0, NULL);
}

void hud_gl_blank(int off)
{
	uint32_t fb;

	/*
	 * 0x28 first while DSI is up, then drop CRTC so MSM runtime-suspends
	 * MDP/DSI. Panel unprepare no longer GPIO-resets (incell). Unblank
	 * modesets first so enable() can send 0x29 with the host on.
	 */
	if (off) {
		if (panel_dcs_sleep(1) != 0)
			gl_log("panel sleep failed");
		if (egl_dpy != EGL_NO_DISPLAY &&
		    eglGetCurrentContext() != EGL_NO_CONTEXT)
			glFinish();
		if (disable_crtc() != 0)
			gl_log("CRTC disable failed");
		crtc_set = 0;
		if (egl_dpy != EGL_NO_DISPLAY) {
			eglMakeCurrent(egl_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE,
				       EGL_NO_CONTEXT);
			gl_runtime_off = 1;
		}
		gl_log("display runtime off");
		return;
	}
	if (gl_runtime_off && egl_dpy != EGL_NO_DISPLAY &&
	    egl_surf != EGL_NO_SURFACE && egl_ctx != EGL_NO_CONTEXT) {
		if (!eglMakeCurrent(egl_dpy, egl_surf, egl_surf, egl_ctx))
			gl_log("eglMakeCurrent resume failed");
	}
	gl_runtime_off = 0;
	if (kms_fd >= 0 && crtc_id && bo_prev &&
	    (fb = fb_from_bo(bo_prev)) != 0) {
		if (drmModeSetCrtc(kms_fd, crtc_id, fb, 0, 0, &conn_id, 1,
				   &mode) == 0)
			crtc_set = 1;
		else {
			gl_log("SetCrtc resume failed");
			crtc_set = 0;
		}
	} else {
		crtc_set = 0;
	}
	gl_log("display runtime on");
}

void hud_gl_shutdown(void)
{
	int i;

	ready = 0;
	if (egl_dpy != EGL_NO_DISPLAY) {
		eglMakeCurrent(egl_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE,
			       EGL_NO_CONTEXT);
		if (egl_surf != EGL_NO_SURFACE)
			eglDestroySurface(egl_dpy, egl_surf);
		if (egl_ctx != EGL_NO_CONTEXT)
			eglDestroyContext(egl_dpy, egl_ctx);
		eglTerminate(egl_dpy);
	}
	egl_dpy = EGL_NO_DISPLAY;
	egl_surf = EGL_NO_SURFACE;
	egl_ctx = EGL_NO_CONTEXT;
	if (bo_prev && gsurf)
		gbm_surface_release_buffer(gsurf, bo_prev);
	bo_prev = NULL;
	if (gsurf)
		gbm_surface_destroy(gsurf);
	gsurf = NULL;
	if (gbm)
		gbm_device_destroy(gbm);
	gbm = NULL;
	if (kms_fd >= 0)
		close(kms_fd);
	kms_fd = -1;
	free(atlas);
	atlas = NULL;
	for (i = 0; i < nfonts; i++) {
		free(fonts[i].x);
		free(fonts[i].y);
		fonts[i].x = fonts[i].y = NULL;
		fonts[i].f = NULL;
	}
	nfonts = 0;
	crtc_set = 0;
	atlas_fail = 0;
	pack_x = pack_y = 1;
	pack_row_h = 0;
}
