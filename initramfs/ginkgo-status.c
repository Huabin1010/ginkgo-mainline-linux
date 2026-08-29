/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Boot HUD for ginkgo: Wi-Fi + CPU/GPU/RAM.
 * Prefers GBM/EGL/GLES2 (Adreno 610); falls back to CPU write(/dev/fb0).
 * Volume Up (no power) still reboot-fastboot. GDM stays masked.
 */
#include "ginkgo-status-font.h"
#include "ginkgo-status-gpu.h"
#include "ginkgo-wifi-flow.h"

#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <dirent.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <linux/kd.h>
#include <linux/reboot.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef FBIO_WAITFORVSYNC
#define FBIO_WAITFORVSYNC _IOW('F', 0x20, __u32)
#endif

#define MAX_AP 24
#define MAX_EV 32
#define PW_MAX 64
#define MAX_CPU 8

struct ap {
	char ssid[96];
	char security[40];
	char freq[24];
	int signal;
	int secure;
	int active;
};

struct wifi {
	int have_dev;
	int connected;
	int busy;
	char iface[32];
	char ssid[96];
	char ip[64];
	char gw[64];
	char freq[32];
	char hwaddr[32];
	int signal;
	int signal_dbm;
};

enum sheet { SHEET_NONE, SHEET_LIST, SHEET_DETAIL, SHEET_PASS };

static int fb_fd = -1, tty_fd = -1, drm_fd = -1;
static uint32_t *back;
static int W, H, pitch;
static int use_gl;
static unsigned opened_ev;
static struct pollfd pfds[MAX_EV];
static int npfd;
static int abs_min_x[MAX_EV], abs_max_x[MAX_EV];
static int abs_min_y[MAX_EV], abs_max_y[MAX_EV];
static int is_touch[MAX_EV];
static int has_mt[MAX_EV];
#define MAX_SLOTS 12
static int mt_cur;
static int mt_id[MAX_SLOTS];
static int mt_x[MAX_SLOTS], mt_y[MAX_SLOTS];
static int mt_have_x[MAX_SLOTS], mt_have_y[MAX_SLOTS];
static int mt_major[MAX_SLOTS];
static int mt_z[MAX_SLOTS];
static int mt_fresh[MAX_SLOTS];
static int mt_nslots = MAX_SLOTS;
static int mt_bind_slot = -1;
static int slot_x, slot_y, slot_id = -1, down_x, down_y, dragging;
static int contact_armed;
static uint64_t t_power_key, t_contact, t_down;
static int last_raw_x, last_raw_y, last_raw_ok;
static int list_scroll, list_scroll_tgt;
static enum sheet sheet, sheet_tgt;
static float sheet_t, sheet_from, sheet_goal = -1.f;
static uint64_t t_sheet;
static struct wifi wifi;
static struct ap aps[MAX_AP];
static int nap, selected_ap = -1, hover_ap = -1;
static char selected_ssid[96], selected_sec[40], selected_freq[24];
static int selected_signal, selected_secure, selected_active;
static int forgetting;
static char forget_ssid[96];
static char password[PW_MAX];
static int pw_len, shift_on, sym_on, osk_sym2, connecting, connect_fail;
enum {
	OSK_CHAR = 1,
	OSK_SHIFT,
	OSK_SYM,
	OSK_MORE,
	OSK_SPACE,
	OSK_BKSP,
	OSK_GO
};
#define OSK_MAX 48
struct osk_key {
	char lab[8];
	char ch;
	unsigned char kind;
	short x, y, w, h;
};
static struct osk_key osk_keys[OSK_MAX];
static int nosk, osk_held, osk_hi = -1, osk_anim = -1;
static float osk_u;
static int connect_need_pass;
static char connect_ssid[96];
static char known_psk_ssid[96];
static char known_psk[PW_MAX];
static int join_wait_live;
static uint64_t t_join_wait;
static enum gw_fail_kind last_fail_kind;
static char last_fail_raw[192];
static char status_line[96];
static pid_t job_pid = -1;
static int job_kind; /* 1=info 2=scan 3=connect 4=link 5=forget */
static int job_out[2] = { -1, -1 };
static char job_buf[16384];
static size_t job_n;
static float cpu_t, gpu_t, ram_t, cpu_s, gpu_s, ram_s;
static float cpu_core_t[MAX_CPU], cpu_core_s[MAX_CPU];
static int ncpu, gpu_ok, gpu_mhz, cpu_temp_c = -1, gpu_temp_c = -1;
static int batt_ok, batt_pct = -1, batt_mw = -1, batt_charging;
static unsigned long ram_total_kb, ram_used_kb;
static unsigned long long cpu_idle_a, cpu_tot_a;
static unsigned long long cpu_core_idle[MAX_CPU], cpu_core_tot[MAX_CPU];
static uint64_t t_wifi, t_scan, t_assoc, t_occ;
static unsigned uptime_sec;
static float pulse;
static int want_scan, want_info, want_connect, want_link, want_forget;
static char pend_ssid[96], pend_pass[PW_MAX], pend_sec[40], pend_forget[96];
static int lang_en;
static int bl_max, bl_cur, bl_pct = -1, bl_keep = 80, dragging_bl;
static char bl_path[192];
static char bl_power_path[192];
static int hud_dirty = 1;
static uint64_t t_scan_input, t_unblank;
static int screen_off;
static int mt_got_pos;
static unsigned long rx_bps, tx_bps;
static unsigned long long net_rx_b, net_tx_b;
static uint64_t t_net;
static int net_ready;
static char net_iface[32];

#define HUD_CONF "/var/lib/ginkgo/hud.conf"

static const char *T(const char *zh, const char *en)
{
	return lang_en ? en : zh;
}

static void dirty(void)
{
	hud_dirty = 1;
}

static void logmsg(const char *msg)
{
	int k = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);
	char line[256];

	snprintf(line, sizeof(line), "ginkgo-status: %s\n", msg);
	if (k >= 0) {
		(void)!write(k, line, strlen(line));
		close(k);
	}
}

static void usb_gadget_disconnect(void)
{
	const char *udc = "/sys/kernel/config/usb_gadget/ginkgo/UDC";
	struct timespec ts = { .tv_sec = 0, .tv_nsec = 500000000L };
	int fd = open(udc, O_WRONLY | O_CLOEXEC);

	/*
	 * Drop the HS D+ pull-up before pshold. A warm reboot with the
	 * RNDIS gadget still bound leaves the host on 1d6b:0104 and ABL
	 * fastboot bulk (flash) fails; hardware Vol- + Power does a
	 * hard reset and re-enumerates clean as 18d1:d00d.
	 */
	if (fd >= 0) {
		(void)!write(fd, "\n", 1);
		close(fd);
	}
	nanosleep(&ts, NULL);
}

static void do_reboot_fastboot(void)
{
	logmsg("volume-up reboot-fastboot");
	sync();
	usb_gadget_disconnect();
	if (syscall(SYS_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2,
		    LINUX_REBOOT_CMD_RESTART2, "bootloader") < 0)
		logmsg("SYS_reboot bootloader failed");
	execl("/usr/local/sbin/reboot-fastboot", "reboot-fastboot", (char *)NULL);
	_exit(1);
}

static uint64_t nsec_now(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

#define PERF_HITCH 8
static unsigned perf_n, perf_h20, perf_h33, perf_h50;
static unsigned long perf_sum_us, perf_max_us;
static char perf_slow[96];
static unsigned perf_bkt[6];
static uint64_t t_perf_dump, t_loop0;
static int last_h_i;
static struct {
	unsigned loop, cpu, draw, wait, after;
	int nv, nf, wto, met, sheet;
} last_h[PERF_HITCH];

static void perf_add(int loop_us, int cpu_us, int draw_us, int wait_us,
		     int after_us, int nv, int nf, int wto, int met)
{
	int bi;
	char line[192];

	if (loop_us < 0)
		return;
	perf_n++;
	perf_sum_us += (unsigned)loop_us;
	if ((unsigned)loop_us > perf_max_us)
		perf_max_us = (unsigned)loop_us;
	if (loop_us > 20000)
		perf_h20++;
	if (loop_us > 33000)
		perf_h33++;
	if (loop_us > 50000)
		perf_h50++;
	if (loop_us <= 17000)
		bi = 0;
	else if (loop_us <= 20000)
		bi = 1;
	else if (loop_us <= 25000)
		bi = 2;
	else if (loop_us <= 33000)
		bi = 3;
	else if (loop_us <= 50000)
		bi = 4;
	else
		bi = 5;
	perf_bkt[bi]++;
	if (loop_us > 20000) {
		int i = last_h_i % PERF_HITCH;

		last_h[i].loop = (unsigned)loop_us;
		last_h[i].cpu = (unsigned)cpu_us;
		last_h[i].draw = (unsigned)draw_us;
		last_h[i].wait = (unsigned)wait_us;
		last_h[i].after = (unsigned)after_us;
		last_h[i].nv = nv;
		last_h[i].nf = nf;
		last_h[i].wto = wto;
		last_h[i].met = met;
		last_h[i].sheet = (int)(sheet_t * 100.0f);
		last_h_i++;
	}
	if (loop_us > 33000) {
		snprintf(line, sizeof(line),
			 "hitch %d us cpu=%d draw=%d wait=%d after=%d nv=%d flush=%d wto=%d met=%d s=%d %s",
			 loop_us, cpu_us, draw_us, wait_us, after_us, nv, nf, wto, met,
			 (int)(sheet_t * 100.0f), perf_slow);
		logmsg(line);
	}
}

static void perf_dump(uint64_t now)
{
	FILE *f;
	unsigned i, avg;
	char line[256];

	if (now - t_perf_dump < 2000000000ull || perf_n < 30)
		return;
	t_perf_dump = now;
	avg = perf_n ? (unsigned)(perf_sum_us / perf_n) : 0;
	f = fopen("/run/ginkgo-hud-perf.txt", "w");
	if (f) {
		fprintf(f,
			"n=%u avg_us=%u max_us=%lu >20ms=%u >33ms=%u >50ms=%u\n"
			"hist <=17=%u 18-20=%u 21-25=%u 26-33=%u 34-50=%u >50=%u\n",
			perf_n, avg, perf_max_us, perf_h20, perf_h33, perf_h50,
			perf_bkt[0], perf_bkt[1], perf_bkt[2], perf_bkt[3],
			perf_bkt[4], perf_bkt[5]);
		fprintf(f, "last_hitches loop/cpu/draw/wait/after nv flush wto met sheet%%\n");
		for (i = 0; i < PERF_HITCH; i++) {
			int k = (last_h_i + i) % PERF_HITCH;

			if (!last_h[k].loop)
				continue;
			fprintf(f, "  %u %u %u %u %u  %d %d %d %d %d\n",
				last_h[k].loop, last_h[k].cpu, last_h[k].draw,
				last_h[k].wait, last_h[k].after, last_h[k].nv,
				last_h[k].nf, last_h[k].wto, last_h[k].met,
				last_h[k].sheet);
		}
		fclose(f);
	}
	snprintf(line, sizeof(line),
		 "perf n=%u avg=%uus max=%luus hitch20=%u hitch33=%u hist=%u/%u/%u/%u/%u/%u",
		 perf_n, avg, perf_max_us, perf_h20, perf_h33,
		 perf_bkt[0], perf_bkt[1], perf_bkt[2], perf_bkt[3],
		 perf_bkt[4], perf_bkt[5]);
	logmsg(line);
}

static int clampi(int v, int a, int b)
{
	if (v < a)
		return a;
	if (v > b)
		return b;
	return v;
}

static float clampf(float v, float a, float b)
{
	if (v < a)
		return a;
	if (v > b)
		return b;
	return v;
}

static float ease_in_out_cubic(float u)
{
	if (u < 0.5f)
		return 4.0f * u * u * u;
	{
		float t = -2.0f * u + 2.0f;

		return 1.0f - 0.5f * t * t * t;
	}
}

static uint32_t px(int r, int g, int b)
{
	return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static uint32_t mix(uint32_t a, uint32_t b, int alpha)
{
	int ar = (a >> 16) & 255, ag = (a >> 8) & 255, ab = a & 255;
	int br = (b >> 16) & 255, bg = (b >> 8) & 255, bb = b & 255;
	int ia = 255 - alpha;

	return px((br * alpha + ar * ia) / 255,
		  (bg * alpha + ag * ia) / 255,
		  (bb * alpha + ab * ia) / 255);
}

static void plot(int x, int y, uint32_t c, int a)
{
	uint32_t *p;

	if ((unsigned)x >= (unsigned)W || (unsigned)y >= (unsigned)H || a <= 0)
		return;
	p = back + y * W + x;
	if (a >= 255)
		*p = c;
	else
		*p = mix(*p, c, a);
}

static void fill_rect(int x, int y, int w, int h, uint32_t c)
{
	int yy, xx, x1, y1;

	if (use_gl) {
		hud_gl_fill_rect(x, y, w, h, c);
		return;
	}
	x1 = clampi(x + w, 0, W);
	y1 = clampi(y + h, 0, H);
	x = clampi(x, 0, W);
	y = clampi(y, 0, H);
	for (yy = y; yy < y1; yy++) {
		uint32_t *p = back + yy * W + x;
		for (xx = x; xx < x1; xx++)
			*p++ = c;
	}
}

static void fill_round(int x, int y, int w, int h, int r, uint32_t c)
{
	int yy, rr = r * r;

	if (use_gl) {
		hud_gl_fill_round(x, y, w, h, r, c);
		return;
	}
	if (r < 1) {
		fill_rect(x, y, w, h, c);
		return;
	}
	for (yy = 0; yy < h; yy++) {
		int dx = 0, py = y + yy;
		if (yy < r) {
			int dy = r - 1 - yy;
			while (dx * dx + dy * dy > rr && dx < r)
				dx++;
		} else if (yy >= h - r) {
			int dy = yy - (h - r);
			while (dx * dx + dy * dy > rr && dx < r)
				dx++;
		}
		fill_rect(x + dx, py, w - 2 * dx, 1, c);
	}
}

static void __attribute__((unused))
ring(int cx, int cy, float ro, float t, float frac, uint32_t bg, uint32_t fg)
{
	int x, y, r = (int)(ro + t + 2);
	float ri = ro - t;

	frac = clampf(frac, 0, 1);
	for (y = -r; y <= r; y++) {
		for (x = -r; x <= r; x++) {
			float d = sqrtf((float)x * x + (float)y * y);
			float cov, ang, a1, a2;
			int alpha;
			uint32_t col;

			a1 = clampf(ro + 0.6f - d, 0, 1);
			a2 = clampf(d - (ri - 0.6f), 0, 1);
			cov = a1 < a2 ? a1 : a2;
			if (cov <= 0)
				continue;
			ang = atan2f((float)-x, (float)-y); /* 0 at top, clockwise */
			if (ang < 0)
				ang += 6.2831853f;
			col = (ang < frac * 6.2831853f) ? fg : bg;
			alpha = (int)(cov * 255.0f);
			plot(cx + x, cy + y, col, alpha);
		}
	}
}

static const struct gs_glyph *find_g(const struct gs_font *f, uint32_t cp)
{
	int lo = 0, hi = f->n - 1, i;

	while (lo <= hi) {
		int mid = (lo + hi) >> 1;
		uint32_t g = f->g[mid].cp;
		if (g == cp)
			return &f->g[mid];
		if (g < cp)
			lo = mid + 1;
		else
			hi = mid - 1;
	}
	for (i = 0; i < f->n; i++) {
		if (f->g[i].cp == cp)
			return &f->g[i];
	}
	return NULL;
}

static uint32_t u8next(const char **pp)
{
	const unsigned char *p = (const unsigned char *)*pp;
	uint32_t cp;

	if (!*p)
		return 0;
	if (p[0] < 0x80) {
		cp = p[0];
		*pp += 1;
	} else if ((p[0] & 0xe0) == 0xc0 && p[1]) {
		cp = ((p[0] & 0x1f) << 6) | (p[1] & 0x3f);
		*pp += 2;
	} else if ((p[0] & 0xf0) == 0xe0 && p[1] && p[2]) {
		cp = ((p[0] & 0x0f) << 12) | ((p[1] & 0x3f) << 6) | (p[2] & 0x3f);
		*pp += 3;
	} else if ((p[0] & 0xf8) == 0xf0 && p[1] && p[2] && p[3]) {
		cp = ((p[0] & 0x07) << 18) | ((p[1] & 0x3f) << 12) |
		     ((p[2] & 0x3f) << 6) | (p[3] & 0x3f);
		*pp += 4;
	} else {
		cp = '?';
		*pp += 1;
	}
	return cp;
}

static int text_w(const struct gs_font *f, const char *s)
{
	int w = 0;
	const char *p = s;
	uint32_t cp;

	while ((cp = u8next(&p))) {
		const struct gs_glyph *g;
		if (cp == ' ') {
			w += f->px / 3;
			continue;
		}
		g = find_g(f, cp);
		w += g ? g->adv : f->px / 2;
	}
	return w;
}

static void draw_text(const struct gs_font *f, int x, int y, const char *s, uint32_t c);

/* Ink box relative to draw_text's y (em-box top). yoff varies by glyph,
 * so metric px/2 is not visual center — especially CJK vs Latin. */
static int text_ink(const struct gs_font *f, const char *s, int *top, int *bot)
{
	const char *p = s;
	uint32_t cp;
	int w = 0, t = 10000, b = -10000;

	while ((cp = u8next(&p))) {
		const struct gs_glyph *g;

		if (cp == ' ') {
			w += f->px / 3;
			continue;
		}
		g = find_g(f, cp);
		if (!g) {
			w += f->px / 2;
			continue;
		}
		if (g->h > 0) {
			if (g->yoff < t)
				t = g->yoff;
			if (g->yoff + (int)g->h > b)
				b = g->yoff + (int)g->h;
		}
		w += g->adv;
	}
	if (b <= t) {
		t = 0;
		b = f->px;
	}
	if (top)
		*top = t;
	if (bot)
		*bot = b;
	return w;
}

static void draw_text_vcenter(const struct gs_font *f, int x, int y, int h,
			     const char *s, uint32_t c)
{
	int top, bot, ink_h;

	if (!s || !s[0] || h < 1)
		return;
	text_ink(f, s, &top, &bot);
	ink_h = bot - top;
	draw_text(f, x, y + (h - ink_h) / 2 - top, s, c);
}

static void draw_text_centered(const struct gs_font *f, int x, int y, int w, int h,
			      const char *s, uint32_t c)
{
	int top, bot, tw, ink_h;

	if (!s || !s[0] || w < 1 || h < 1)
		return;
	tw = text_ink(f, s, &top, &bot);
	ink_h = bot - top;
	draw_text(f, x + (w - tw) / 2, y + (h - ink_h) / 2 - top, s, c);
}

static void draw_btn(int x, int y, int w, int h, int r, uint32_t bg,
		    const struct gs_font *f, const char *s, uint32_t fg)
{
	fill_round(x, y, w, h, r, bg);
	draw_text_centered(f, x, y, w, h, s, fg);
}

static void draw_text(const struct gs_font *f, int x, int y, const char *s, uint32_t c)
{
	const char *p = s;
	uint32_t cp;

	while ((cp = u8next(&p))) {
		const struct gs_glyph *g;
		int gx, gy, ix, iy;

		if (cp == ' ') {
			x += f->px / 3;
			continue;
		}
		g = find_g(f, cp);
		if (!g) {
			fill_rect(x + 2, y + 4, f->px / 2 - 4, f->px - 8, mix(c, 0, 80));
			x += f->px / 2;
			continue;
		}
		if (use_gl) {
			hud_gl_glyph(f, g, x, y, c);
			x += g->adv;
			continue;
		}
		for (iy = 0; iy < g->h; iy++) {
			gy = y + g->yoff + iy;
			for (ix = 0; ix < g->w; ix++) {
				int a = f->bits[g->off + (uint32_t)iy * g->w + ix];
				gx = x + g->xoff + ix;
				if (a)
					plot(gx, gy, c, a);
			}
		}
		x += g->adv;
	}
}

static void draw_text_fit(const struct gs_font *f, int x, int y, int maxw, const char *s, uint32_t c)
{
	char tmp[160];
	int n = 0, w;
	const char *p = s;

	if (text_w(f, s) <= maxw) {
		draw_text(f, x, y, s, c);
		return;
	}
	tmp[0] = 0;
	while (*p && n < (int)sizeof(tmp) - 8) {
		const char *save = p;
		uint32_t cp = u8next(&p);
		int add = (int)(p - save);
		if (n + add + 3 >= (int)sizeof(tmp))
			break;
		memcpy(tmp + n, save, add);
		tmp[n + add] = 0;
		w = text_w(f, tmp) + text_w(f, "…");
		if (w > maxw) {
			tmp[n] = 0;
			break;
		}
		n += add;
		(void)cp;
	}
	strcat(tmp, "…");
	draw_text(f, x, y, tmp, c);
}

static int hit(int x, int y, int rx, int ry, int rw, int rh)
{
	return x >= rx && y >= ry && x < rx + rw && y < ry + rh;
}

static long read_long(const char *path)
{
	char b[64];
	int fd = open(path, O_RDONLY);
	ssize_t n;
	long v;

	if (fd < 0)
		return -1;
	n = read(fd, b, sizeof(b) - 1);
	close(fd);
	if (n <= 0)
		return -1;
	b[n] = 0;
	v = strtol(b, NULL, 10);
	return v;
}

static void sample_cpu(void)
{
	FILE *f = fopen("/proc/stat", "r");
	char line[256];

	if (!f)
		return;
	while (fgets(line, sizeof(line), f)) {
		unsigned long long u, n, s, id, io, irq, sirq, st, tot, busy;
		int core = -1;

		if (!strncmp(line, "cpu ", 4)) {
			if (sscanf(line + 3,
				   "%llu %llu %llu %llu %llu %llu %llu %llu",
				   &u, &n, &s, &id, &io, &irq, &sirq, &st) < 5)
				continue;
			tot = u + n + s + id + io + irq + sirq + st;
			if (cpu_tot_a && tot > cpu_tot_a) {
				busy = (tot - cpu_tot_a) - (id - cpu_idle_a);
				cpu_t = clampf(100.0f * (float)busy /
						       (float)(tot - cpu_tot_a),
					       0, 100);
			}
			cpu_idle_a = id;
			cpu_tot_a = tot;
			continue;
		}
		if (sscanf(line, "cpu%d %llu %llu %llu %llu %llu %llu %llu %llu",
			   &core, &u, &n, &s, &id, &io, &irq, &sirq, &st) < 6)
			continue;
		if (core < 0 || core >= MAX_CPU)
			continue;
		tot = u + n + s + id + io + irq + sirq + st;
		if (cpu_core_tot[core] && tot > cpu_core_tot[core]) {
			busy = (tot - cpu_core_tot[core]) -
			       (id - cpu_core_idle[core]);
			cpu_core_t[core] = clampf(100.0f * (float)busy /
							  (float)(tot - cpu_core_tot[core]),
						  0, 100);
		}
		cpu_core_idle[core] = id;
		cpu_core_tot[core] = tot;
		if (core + 1 > ncpu)
			ncpu = core + 1;
	}
	fclose(f);
}

static char tz_cpu_path[192], tz_gpu_path[192], tz_best_path[192];
static int tz_cached;

static int tz_read(const char *path)
{
	long millic;
	int c;

	if (!path || !path[0])
		return -1;
	millic = read_long(path);
	if (millic < 0)
		return -1;
	c = (millic > 1000) ? (int)(millic / 1000) : (int)millic;
	if (c < 10 || c > 125)
		return -1;
	return c;
}

static void sample_temp(void)
{
	DIR *d;
	struct dirent *ent;
	int best = -1, best_cpu = -1, best_gpu = -1;

	if (tz_cached) {
		best_cpu = tz_read(tz_cpu_path);
		best_gpu = tz_read(tz_gpu_path);
		best = tz_read(tz_best_path);
		cpu_temp_c = (best_cpu > 0) ? best_cpu : best;
		gpu_temp_c = best_gpu;
		return;
	}
	d = opendir("/sys/class/thermal");
	if (!d)
		return;
	while ((ent = readdir(d))) {
		char path[192], typ[64];
		long millic;
		int c, is_cpu, is_gpu, fd;
		ssize_t n;

		if (strncmp(ent->d_name, "thermal_zone", 12))
			continue;
		snprintf(path, sizeof(path), "/sys/class/thermal/%s/type", ent->d_name);
		fd = open(path, O_RDONLY | O_CLOEXEC);
		if (fd < 0)
			continue;
		n = read(fd, typ, sizeof(typ) - 1);
		close(fd);
		if (n <= 0)
			continue;
		typ[n] = 0;
		if (typ[n - 1] == '\n')
			typ[n - 1] = 0;
		is_cpu = (strstr(typ, "cpu") || strstr(typ, "CPU") ||
			  strstr(typ, "krait") || strstr(typ, "kryo"));
		is_gpu = (strstr(typ, "gpu") || strstr(typ, "GPU") ||
			  strstr(typ, "adreno"));
		if (!is_cpu && !is_gpu && !strstr(typ, "tsens") && !strstr(typ, "soc"))
			continue;
		snprintf(path, sizeof(path), "/sys/class/thermal/%s/temp", ent->d_name);
		millic = read_long(path);
		if (millic > 1000)
			c = (int)(millic / 1000);
		else
			c = (int)millic;
		if (c < 10 || c > 125)
			continue;
		if (c > best) {
			best = c;
			snprintf(tz_best_path, sizeof(tz_best_path), "%s", path);
		}
		if (is_cpu && c > best_cpu) {
			best_cpu = c;
			snprintf(tz_cpu_path, sizeof(tz_cpu_path), "%s", path);
		}
		if (is_gpu && c > best_gpu) {
			best_gpu = c;
			snprintf(tz_gpu_path, sizeof(tz_gpu_path), "%s", path);
		}
	}
	closedir(d);
	tz_cached = 1;
	cpu_temp_c = (best_cpu > 0) ? best_cpu : best;
	gpu_temp_c = best_gpu;
}

static char batt_ua_path[192], batt_uv_path[192], batt_cap_path[192];
static char batt_st_path[192];
static int batt_cached;

static void estimate_power(void)
{
	int i, n = ncpu > 0 ? ncpu : 8;
	float w = 1.20f; /* display + idle SoC, screen on */

	for (i = 0; i < n && i < MAX_CPU; i++) {
		float pct = cpu_core_t[i] / 100.0f;

		if (pct < 0.f)
			pct = 0.f;
		/* Kryo 260: cpu0–3 A53, cpu4–7 A73 */
		w += pct * ((i < 4) ? 0.38f : 0.90f);
	}
	if (gpu_ok && gpu_t > 0.f)
		w += (gpu_t / 100.0f) * 2.00f;
	if (w < 0.4f)
		w = 0.4f;
	batt_mw = (int)(w * 1000.0f + 0.5f);
	batt_ok = 1;
}

static void sample_batt(void)
{
	long ua = 0, uv = 0, cap = -1;
	char st[32];
	int fd;
	ssize_t n;

	if (!batt_cached) {
		DIR *d = opendir("/sys/class/power_supply");
		struct dirent *ent;
		int found = 0;

		if (d) {
			while ((ent = readdir(d))) {
				char path[192], typ[32];

				if (ent->d_name[0] == '.')
					continue;
				snprintf(path, sizeof(path), "/sys/class/power_supply/%s/type",
					 ent->d_name);
				fd = open(path, O_RDONLY | O_CLOEXEC);
				if (fd < 0)
					continue;
				n = read(fd, typ, sizeof(typ) - 1);
				close(fd);
				if (n <= 0)
					continue;
				typ[n] = 0;
				if (strncmp(typ, "Battery", 7))
					continue;
				snprintf(batt_ua_path, sizeof(batt_ua_path),
					 "/sys/class/power_supply/%s/current_now", ent->d_name);
				snprintf(batt_uv_path, sizeof(batt_uv_path),
					 "/sys/class/power_supply/%s/voltage_now", ent->d_name);
				snprintf(batt_cap_path, sizeof(batt_cap_path),
					 "/sys/class/power_supply/%s/capacity", ent->d_name);
				snprintf(batt_st_path, sizeof(batt_st_path),
					 "/sys/class/power_supply/%s/status", ent->d_name);
				found = 1;
				break;
			}
			closedir(d);
		}
		batt_cached = 1;
		if (!found) {
			batt_ua_path[0] = 0;
			batt_uv_path[0] = 0;
		}
	}
	if (batt_cap_path[0]) {
		cap = read_long(batt_cap_path);
		if (cap >= 0 && cap <= 100)
			batt_pct = (int)cap;
	}
	if (batt_st_path[0]) {
		fd = open(batt_st_path, O_RDONLY | O_CLOEXEC);
		if (fd >= 0) {
			n = read(fd, st, sizeof(st) - 1);
			close(fd);
			if (n > 0) {
				st[n] = 0;
				batt_charging = (strncmp(st, "Charging", 8) == 0);
			}
		}
	}
	if (batt_ua_path[0] && batt_uv_path[0]) {
		ua = read_long(batt_ua_path);
		uv = read_long(batt_uv_path);
		if (uv >= 1000000 && ua != -1) {
			batt_mw = (int)((labs(ua) * (unsigned long long)uv) /
					1000000000ull);
			batt_ok = 1;
			return;
		}
	}
	estimate_power();
}

static void sample_ram(void)
{
	FILE *f = fopen("/proc/meminfo", "r");
	char k[64];
	unsigned long v, total = 0, avail = 0;

	if (!f)
		return;
	while (fscanf(f, "%63s %lu %*s", k, &v) == 2) {
		if (!strcmp(k, "MemTotal:"))
			total = v;
		else if (!strcmp(k, "MemAvailable:"))
			avail = v;
		if (total && avail)
			break;
	}
	fclose(f);
	if (total) {
		ram_total_kb = total;
		ram_used_kb = (avail && avail < total) ? total - avail : 0;
		ram_t = clampf(100.0f * (float)ram_used_kb / (float)total, 0, 100);
	}
}

static int try_gpu_path(const char *path, float *pct)
{
	long v = read_long(path);

	if (v < 0)
		return 0;
	*pct = clampf((float)v, 0, 100);
	return 1;
}

static void ensure_gpu_open(void)
{
	if (drm_fd >= 0)
		return;
	/* Occupancy only. Never grab card0 — the GLES path needs DRM master. */
	drm_fd = open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC);
}

static int path_looks_gpu(const char *s)
{
	if (!s || strstr(s, "gpu_mem") || strstr(s, "zap"))
		return 0;
	return strstr(s, "b00000.gpu") || strstr(s, "kgsl") || strstr(s, "adreno-3d") ||
	       (strstr(s, ".gpu") && !strstr(s, "90f") && !strstr(s, "923"));
}

static void take_gpu_freq(long freq)
{
	int mhz;

	if (freq >= 1000000L)
		mhz = (int)(freq / 1000000L);
	else if (freq >= 10000L)
		mhz = (int)(freq / 1000L);
	else
		return;
	/* Adreno 610 OPP is 320–950 MHz. Drop XO / misc clocks like 27 MHz. */
	if (mhz < 100 || mhz > 1200)
		return;
	if (mhz > gpu_mhz)
		gpu_mhz = mhz;
}

static char gpu_busy_path[320];
static char gpu_freq_path[320];
static int gpu_paths_done;

static void gpu_set_busy(const char *path, float pct, int *found)
{
	if (!gpu_busy_path[0])
		snprintf(gpu_busy_path, sizeof(gpu_busy_path), "%s", path);
	*found = 1;
	(void)pct;
}

static void gpu_set_freq(const char *path, long freq)
{
	if (freq <= 0)
		return;
	if (!gpu_freq_path[0])
		snprintf(gpu_freq_path, sizeof(gpu_freq_path), "%s", path);
	take_gpu_freq(freq);
}

static void discover_gpu_paths(float *pct, int *found)
{
	static const char *busy_try[] = {
		"/sys/class/drm/card0/device/gpu_busy_percent",
		"/sys/bus/platform/devices/5900000.gpu/gpu_busy_percent",
		"/sys/bus/platform/devices/b00000.gpu/gpu_busy_percent",
		"/sys/kernel/debug/dri/0/devfreq/busy_percent",
		"/sys/kernel/debug/dri/128/devfreq/busy_percent",
		"/sys/class/kgsl/kgsl-3d0/gpu_busy_percentage",
		"/sys/class/kgsl/kgsl-3d0/gpu_busy",
		NULL
	};
	DIR *d;
	struct dirent *ent;
	char path[320], link[256];
	ssize_t n;
	int i;

	for (i = 0; busy_try[i]; i++) {
		if (try_gpu_path(busy_try[i], pct)) {
			gpu_set_busy(busy_try[i], *pct, found);
			break;
		}
	}
	if (!*found) {
		DIR *pd = opendir("/sys/bus/platform/devices");
		if (pd) {
			while ((ent = readdir(pd))) {
				if (!path_looks_gpu(ent->d_name))
					continue;
				snprintf(path, sizeof(path),
					 "/sys/bus/platform/devices/%s/gpu_busy_percent",
					 ent->d_name);
				if (try_gpu_path(path, pct)) {
					gpu_set_busy(path, *pct, found);
					break;
				}
			}
			closedir(pd);
		}
	}

	d = opendir("/sys/class/devfreq");
	if (d) {
		while ((ent = readdir(d))) {
			long freq, load;
			int is_gpu;

			if (ent->d_name[0] == '.')
				continue;
			is_gpu = path_looks_gpu(ent->d_name);
			if (!is_gpu) {
				snprintf(path, sizeof(path), "/sys/class/devfreq/%s/device",
					 ent->d_name);
				n = readlink(path, link, sizeof(link) - 1);
				if (n > 0) {
					link[n] = 0;
					is_gpu = path_looks_gpu(link);
				}
			}
			if (!is_gpu)
				continue;
			snprintf(path, sizeof(path), "/sys/class/devfreq/%s/cur_freq",
				 ent->d_name);
			freq = read_long(path);
			gpu_set_freq(path, freq);
			snprintf(path, sizeof(path), "/sys/class/devfreq/%s/gpu_load",
				 ent->d_name);
			load = read_long(path);
			if (load < 0) {
				snprintf(path, sizeof(path), "/sys/class/devfreq/%s/load",
					 ent->d_name);
				load = read_long(path);
			}
			if (load >= 0) {
				*pct = clampf((float)load, 0, 100);
				gpu_set_busy(path, *pct, found);
			}
		}
		closedir(d);
	}

	d = opendir("/sys/bus/platform/devices");
	if (d) {
		while ((ent = readdir(d))) {
			DIR *gd;
			struct dirent *ge;

			if (!path_looks_gpu(ent->d_name))
				continue;
			snprintf(path, sizeof(path),
				 "/sys/bus/platform/devices/%s/devfreq", ent->d_name);
			gd = opendir(path);
			if (!gd)
				continue;
			while ((ge = readdir(gd))) {
				char gpath[320];
				long freq, load;

				if (ge->d_name[0] == '.')
					continue;
				snprintf(gpath, sizeof(gpath), "%s/%s/cur_freq", path,
					 ge->d_name);
				freq = read_long(gpath);
				gpu_set_freq(gpath, freq);
				snprintf(gpath, sizeof(gpath), "%s/%s/gpu_load", path,
					 ge->d_name);
				load = read_long(gpath);
				if (load >= 0) {
					*pct = clampf((float)load, 0, 100);
					gpu_set_busy(gpath, *pct, found);
				}
			}
			closedir(gd);
		}
		closedir(d);
	}
	gpu_paths_done = 1;
}

static void sample_gpu(void)
{
	float pct = 0;
	int found = 0;

	ensure_gpu_open();
	gpu_mhz = 0;
	if (!gpu_paths_done)
		discover_gpu_paths(&pct, &found);
	if (gpu_busy_path[0] && try_gpu_path(gpu_busy_path, &pct))
		found = 1;
	else if (gpu_busy_path[0]) {
		gpu_busy_path[0] = 0;
		gpu_freq_path[0] = 0;
		gpu_paths_done = 0;
		discover_gpu_paths(&pct, &found);
		if (gpu_busy_path[0] && try_gpu_path(gpu_busy_path, &pct))
			found = 1;
	}
	if (gpu_freq_path[0]) {
		long freq = read_long(gpu_freq_path);

		if (freq > 0)
			take_gpu_freq(freq);
	}
	gpu_ok = (drm_fd >= 0) || !access("/dev/dri/card0", F_OK) || found ||
		 gpu_mhz > 0;
	gpu_t = found ? pct : 0;
}

static void fmt_bps(char *out, int n, unsigned long bps)
{
	if (!net_ready) {
		snprintf(out, n, "—");
		return;
	}
	if (bps < 1000000UL)
		snprintf(out, n, "%.1f KB/s", bps / 1000.0);
	else
		snprintf(out, n, "%.2f MB/s", bps / 1000000.0);
}

static void net_reset(void)
{
	net_ready = 0;
	rx_bps = 0;
	tx_bps = 0;
	t_net = 0;
	net_rx_b = 0;
	net_tx_b = 0;
	net_iface[0] = 0;
}

static void sample_net(void)
{
	FILE *f;
	char line[256], ifn[32];
	unsigned long long rx = 0, tx = 0, r, t;
	uint64_t now = nsec_now();
	int got = 0;
	const char *want = wifi.iface[0] ? wifi.iface : NULL;

	if (!wifi.connected || !want) {
		if (net_ready || net_iface[0])
			net_reset();
		return;
	}
	f = fopen("/proc/net/dev", "r");
	if (!f)
		return;
	while (fgets(line, sizeof(line), f)) {
		char *colon = strchr(line, ':');
		if (!colon)
			continue;
		*colon = 0;
		if (sscanf(line, " %31s", ifn) != 1)
			continue;
		if (strcmp(ifn, want))
			continue;
		if (sscanf(colon + 1, "%llu %*u %*u %*u %*u %*u %*u %*u %llu",
			   &r, &t) != 2)
			continue;
		rx = r;
		tx = t;
		got = 1;
		break;
	}
	fclose(f);
	if (!got)
		return;
	if (strcmp(net_iface, want) || !t_net || rx < net_rx_b || tx < net_tx_b) {
		snprintf(net_iface, sizeof(net_iface), "%s", want);
		net_rx_b = rx;
		net_tx_b = tx;
		t_net = now;
		return;
	}
	{
		double dt = (double)(now - t_net) / 1e9;

		if (dt < 0.80)
			return;
		rx_bps = (unsigned long)((rx - net_rx_b) / dt);
		tx_bps = (unsigned long)((tx - net_tx_b) / dt);
		net_rx_b = rx;
		net_tx_b = tx;
		t_net = now;
		net_ready = 1;
	}
}

static void close_job(void)
{
	if (job_out[0] >= 0)
		close(job_out[0]);
	if (job_out[1] >= 0)
		close(job_out[1]);
	job_out[0] = job_out[1] = -1;
	job_pid = -1;
	job_n = 0;
}

static void abort_job(void)
{
	if (job_pid <= 0)
		return;
	if (kill(-job_pid, SIGTERM) < 0)
		kill(job_pid, SIGTERM);
	waitpid(job_pid, NULL, 0);
	close_job();
}

static int start_nmcli(int kind, char *const argv[])
{
	int p[2];

	if (job_pid > 0)
		return -1;
	if (access("/usr/bin/nmcli", X_OK)) {
		snprintf(status_line, sizeof(status_line), "%s",
			 T("未找到 nmcli", "nmcli not found"));
		return -1;
	}
	if (pipe(p))
		return -1;
	job_pid = fork();
	if (job_pid < 0) {
		close(p[0]);
		close(p[1]);
		return -1;
	}
	if (job_pid == 0) {
		int null;

		setpgid(0, 0);
		null = open("/dev/null", O_RDONLY);
		if (null >= 0) {
			dup2(null, 0);
			close(null);
		}
		dup2(p[1], 1);
		dup2(p[1], 2);
		close(p[0]);
		close(p[1]);
		setenv("LC_ALL", "C", 1);
		setenv("PATH",
		       "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin",
		       0);
		setenv("DBUS_SYSTEM_BUS_ADDRESS",
		       "unix:path=/run/dbus/system_bus_socket", 0);
		execvp(argv[0], argv);
		_exit(127);
	}
	setpgid(job_pid, job_pid);
	close(p[1]);
	fcntl(p[0], F_SETFL, O_NONBLOCK);
	job_out[0] = p[0];
	job_out[1] = -1;
	job_kind = kind;
	job_n = 0;
	job_buf[0] = 0;
	return 0;
}

static void fmt_uptime(char *out, int n, unsigned sec)
{
	unsigned d = sec / 86400u, h = (sec / 3600u) % 24u, m = (sec / 60u) % 60u;

	if (lang_en) {
		if (d)
			snprintf(out, n, "%ud %uh %um", d, h, m);
		else if (h)
			snprintf(out, n, "%uh %um", h, m);
		else if (m)
			snprintf(out, n, "%um", m);
		else
			snprintf(out, n, "%us", sec);
	} else if (d)
		snprintf(out, n, "%u天 %u小时 %u分", d, h, m);
	else if (h)
		snprintf(out, n, "%u小时 %u分", h, m);
	else if (m)
		snprintf(out, n, "%u分", m);
	else
		snprintf(out, n, "%u秒", sec);
}

static void fmt_band(char *out, int n, const char *freq)
{
	int mhz = atoi(freq ? freq : "");

	if (mhz >= 4900)
		snprintf(out, n, "5 GHz");
	else if (mhz >= 2000)
		snprintf(out, n, "2.4 GHz");
	else if (freq && freq[0])
		snprintf(out, n, "%s", freq);
	else
		out[0] = 0;
}

static int wifi_is_joining(void)
{
	return connecting || join_wait_live || (wifi.busy && !wifi.connected);
}

static int wifi_is_assoc(void)
{
	return connecting || wifi.busy;
}

static int wifi_sec_enterprise(const char *sec)
{
	if (!sec || !sec[0])
		return 0;
	return strstr(sec, "802.1") || strstr(sec, "EAP") ||
	       strstr(sec, "Enterprise") || strstr(sec, "enterprise");
}

static void mark_ap_active(const char *ssid)
{
	int i;

	if (!ssid || !ssid[0])
		return;
	for (i = 0; i < nap; i++)
		aps[i].active = (strcmp(aps[i].ssid, ssid) == 0);
}

static void mark_ap_inactive(const char *ssid)
{
	int i;

	for (i = 0; i < nap; i++) {
		if (!ssid || !ssid[0] || !strcmp(aps[i].ssid, ssid))
			aps[i].active = 0;
	}
	if (!ssid || !ssid[0] || gw_ssid_eq(selected_ssid, ssid))
		selected_active = 0;
}

static int find_ap(const char *ssid)
{
	int i;

	if (!ssid || !ssid[0])
		return -1;
	for (i = 0; i < nap; i++) {
		if (!strcmp(aps[i].ssid, ssid))
			return i;
	}
	return -1;
}

static void select_ap_idx(int i)
{
	if (i < 0 || i >= nap)
		return;
	hover_ap = i;
	selected_ap = i;
	snprintf(selected_ssid, sizeof(selected_ssid), "%s", aps[i].ssid);
	snprintf(selected_sec, sizeof(selected_sec), "%s", aps[i].security);
	snprintf(selected_freq, sizeof(selected_freq), "%s", aps[i].freq);
	selected_signal = aps[i].signal;
	selected_secure = aps[i].secure;
	selected_active = aps[i].active;
}

static void sync_selected_ap(void)
{
	int i = find_ap(selected_ssid);

	if (i >= 0)
		select_ap_idx(i);
}

static int ap_is_current(const char *ssid)
{
	if (gw_ssid_eq(forget_ssid, ssid))
		return 0;
	if (gw_already_on(wifi.connected, wifi.ssid, ssid))
		return 1;
	if (!ssid || !ssid[0] || !wifi.connected)
		return 0;
	{
		int i = find_ap(ssid);

		return i >= 0 && aps[i].active;
	}
}

static const char *picked_ssid(void)
{
	if (connecting && connect_ssid[0])
		return connect_ssid;
	if (selected_ssid[0])
		return selected_ssid;
	if (selected_ap >= 0 && selected_ap < nap)
		return aps[selected_ap].ssid;
	return "";
}

static const char *picked_sec(void)
{
	int i = find_ap(picked_ssid());

	if (i >= 0)
		return aps[i].security;
	if (selected_sec[0])
		return selected_sec;
	return "";
}

static void fill_fail_status(enum gw_fail_kind k, const char *back)
{
	const char *why;

	switch (k) {
	case GW_FAIL_WRONG_PW:
		why = T("密码不对，请再试一次", "Wrong password, try again");
		break;
	case GW_FAIL_NEED_PW:
		why = T("需要密码", "Password required");
		break;
	case GW_FAIL_NOT_FOUND:
		why = T("找不到这个网络", "Network not found");
		break;
	case GW_FAIL_TIMEOUT:
		why = T("连接超时", "Connection timed out");
		break;
	case GW_FAIL_NO_DEVICE:
		why = T("网卡不匹配", "Wi-Fi device mismatch");
		break;
	case GW_FAIL_DHCP:
		why = T("没拿到地址", "No IP address");
		break;
	default:
		why = T("未能加入此网络", "Could not join this network");
		break;
	}
	if (back && back[0])
		snprintf(status_line, sizeof(status_line), "%s · %s %s",
			 why, T("已回到", "back on"), back);
	else
		snprintf(status_line, sizeof(status_line), "%s", why);
}

static void set_connect_status(const char *raw)
{
	if (raw && strstr(raw, "企业")) {
		snprintf(status_line, sizeof(status_line), "%s",
			 T("暂不支持企业级网络", "Enterprise Wi-Fi is not supported"));
		return;
	}
	fill_fail_status(gw_classify_fail(raw), NULL);
}

static void sample_uptime(void)
{
	FILE *f = fopen("/proc/uptime", "r");
	double s = 0;

	if (!f)
		return;
	if (fscanf(f, "%lf", &s) == 1 && s >= 0)
		uptime_sec = (unsigned)s;
	fclose(f);
}

static int iface_ipv4(const char *ifn, char *out, int n)
{
	int fd;
	struct ifreq ifr;
	struct sockaddr_in *in;

	if (!ifn || !ifn[0] || n < 8)
		return 0;
	fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	if (fd < 0)
		return 0;
	memset(&ifr, 0, sizeof(ifr));
	snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", ifn);
	if (ioctl(fd, SIOCGIFADDR, &ifr) < 0) {
		close(fd);
		return 0;
	}
	close(fd);
	in = (struct sockaddr_in *)&ifr.ifr_addr;
	if (ntohl(in->sin_addr.s_addr) == INADDR_LOOPBACK)
		return 0;
	inet_ntop(AF_INET, &in->sin_addr, out, (socklen_t)n);
	return out[0] && strcmp(out, "0.0.0.0") != 0;
}

static void sample_ip(void)
{
	char path[160], line[160], ifn[32];
	unsigned dest, gw;
	int fd;
	ssize_t n;
	FILE *f;

	if (!wifi.iface[0])
		return;
	snprintf(path, sizeof(path), "/sys/class/net/%s/address", wifi.iface);
	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd >= 0) {
		n = read(fd, wifi.hwaddr, sizeof(wifi.hwaddr) - 1);
		close(fd);
		if (n > 0) {
			wifi.hwaddr[n] = 0;
			if (wifi.hwaddr[n - 1] == '\n')
				wifi.hwaddr[n - 1] = 0;
		}
	}
	iface_ipv4(wifi.iface, wifi.ip, sizeof(wifi.ip));
	f = fopen("/proc/net/route", "r");
	if (!f)
		return;
	if (fgets(line, sizeof(line), f)) {
		/* skip header */
	}
	while (fgets(line, sizeof(line), f)) {
		if (sscanf(line, "%31s %x %x", ifn, &dest, &gw) < 3)
			continue;
		if (strcmp(ifn, wifi.iface) || dest != 0 || gw == 0)
			continue;
		snprintf(wifi.gw, sizeof(wifi.gw), "%u.%u.%u.%u",
			 gw & 0xffu, (gw >> 8) & 0xffu,
			 (gw >> 16) & 0xffu, (gw >> 24) & 0xffu);
		break;
	}
	fclose(f);
}

static void apply_wifi_ssid(const char *s);

static void apply_active_ap(void)
{
	int i;

	for (i = 0; i < nap; i++) {
		int match = aps[i].active;

		if (!match && wifi.ssid[0] && !strcmp(aps[i].ssid, wifi.ssid))
			match = 1;
		if (!match || gw_ssid_eq(forget_ssid, aps[i].ssid))
			continue;
		/* Live iw RSSI wins. Scan SIGNAL is a different scale and
		 * was overwriting -84 dBm (~36%) with a stale 10%/36% fight. */
		if (aps[i].ssid[0])
			apply_wifi_ssid(aps[i].ssid);
		if (wifi.signal_dbm >= 0 && aps[i].signal > 0)
			wifi.signal = aps[i].signal;
		if (aps[i].freq[0] && !wifi.freq[0])
			snprintf(wifi.freq, sizeof(wifi.freq), "%s", aps[i].freq);
		break;
	}
}

static int rssi_to_pct(int dbm)
{
	/* Android WifiManager / nmcli: -100 dBm = 0%, -55 dBm = 100%.
	 * Old map (-90..-30) turned -84 dBm into 10% while nmcli showed 36%. */
	if (dbm >= -55)
		return 100;
	if (dbm <= -100)
		return 0;
	return (dbm + 100) * 100 / 45;
}

static void apply_signal_dbm(int dbm)
{
	if (dbm >= 0 || dbm < -120)
		return;
	wifi.signal_dbm = dbm;
	wifi.signal = rssi_to_pct(dbm);
}

static void sample_signal(void)
{
	FILE *f;
	char line[256];

	apply_active_ap();
	if (!wifi.iface[0])
		return;
	f = fopen("/proc/net/wireless", "r");
	if (!f)
		return;
	while (fgets(line, sizeof(line), f)) {
		char *colon, *name;
		int status = 0, link = 0, level = 0;

		colon = strchr(line, ':');
		if (!colon)
			continue;
		*colon = 0;
		name = line;
		while (*name == ' ' || *name == '\t')
			name++;
		if (strcmp(name, wifi.iface))
			continue;
		if (sscanf(colon + 1, "%x %d. %d.", &status, &link, &level) < 2)
			break;
		if (level <= -20 && level >= -120)
			apply_signal_dbm(level);
		else if (wifi.signal_dbm >= 0 && link > 0) {
			if (link <= 70)
				wifi.signal = clampi(link * 100 / 70, 0, 100);
			else
				wifi.signal = clampi(link, 0, 100);
		}
		break;
	}
	fclose(f);
}

static void save_hud_conf(void)
{
	FILE *f;

	mkdir("/var/lib", 0755);
	mkdir("/var/lib/ginkgo", 0755);
	f = fopen(HUD_CONF, "w");
	if (!f)
		return;
	fprintf(f, "lang=%s\nbl=%d\n", lang_en ? "en" : "zh",
		bl_keep < 1 ? 80 : bl_keep);
	fclose(f);
}

static void load_hud_conf(void)
{
	FILE *f = fopen(HUD_CONF, "r");
	char line[128];

	if (!f)
		return;
	while (fgets(line, sizeof(line), f)) {
		if (!strncmp(line, "lang=", 5))
			lang_en = (strstr(line + 5, "en") != NULL);
		else if (!strncmp(line, "bl=", 3)) {
			bl_pct = clampi(atoi(line + 3), 1, 100);
			bl_keep = bl_pct;
		}
	}
	fclose(f);
}

static void commit_bl(void)
{
	if (bl_pct >= 1)
		bl_keep = bl_pct;
	save_hud_conf();
}

static void apply_bl_pct(int pct)
{
	char b[32];
	int fd, v, n;

	bl_pct = clampi(pct, 1, 100);
	if (bl_max < 1 || !bl_path[0])
		return;
	v = 1 + (bl_pct * (bl_max - 1)) / 100;
	if (v < 1)
		v = 1;
	if (v > bl_max)
		v = bl_max;
	if (v == bl_cur)
		return;
	bl_cur = v;
	dirty();
	fd = open(bl_path, O_WRONLY | O_CLOEXEC);
	if (fd < 0)
		return;
	n = snprintf(b, sizeof(b), "%d\n", v);
	(void)!write(fd, b, (size_t)n);
	close(fd);
}

static void write_sysfs(const char *path, const char *s)
{
	int fd;

	if (!path || !path[0] || !s)
		return;
	fd = open(path, O_WRONLY | O_CLOEXEC);
	if (fd < 0)
		return;
	(void)!write(fd, s, strlen(s));
	close(fd);
}

static void reset_touch(void)
{
	int s;

	slot_id = -1;
	dragging = 0;
	dragging_bl = 0;
	contact_armed = 0;
	hover_ap = -1;
	mt_cur = 0;
	mt_got_pos = 0;
	t_contact = 0;
	t_down = 0;
	last_raw_ok = 0;
	for (s = 0; s < MAX_SLOTS; s++) {
		mt_id[s] = -1;
		mt_have_x[s] = 0;
		mt_have_y[s] = 0;
		mt_major[s] = 0;
		mt_z[s] = 0;
		mt_fresh[s] = 0;
	}
	mt_bind_slot = -1;
}

static void set_screen(int on)
{
	if (on) {
		if (!screen_off)
			return;
		screen_off = 0;
		reset_touch();
		t_unblank = nsec_now();
		dragging = 0;
		dragging_bl = 0;
		contact_armed = 0;
		if (use_gl)
			hud_gl_blank(0);
		if (!use_gl) {
			write_sysfs("/sys/class/graphics/fb0/blank", "0\n");
			if (fb_fd >= 0)
				ioctl(fb_fd, FBIOBLANK, FB_BLANK_UNBLANK);
		}
		if (bl_power_path[0])
			write_sysfs(bl_power_path, "0\n");
		bl_cur = 0;
		apply_bl_pct(bl_keep > 0 ? bl_keep : 80);
		dirty();
		logmsg("screen on");
		return;
	}
	if (screen_off)
		return;
	screen_off = 1;
	if (dragging_bl)
		bl_pct = bl_keep;
	reset_touch();
	if (bl_path[0])
		write_sysfs(bl_path, "0\n");
	bl_cur = 0;
	if (bl_power_path[0])
		write_sysfs(bl_power_path, "4\n");
	if (!use_gl) {
		write_sysfs("/sys/class/graphics/fb0/blank", "4\n");
		if (fb_fd >= 0)
			ioctl(fb_fd, FBIOBLANK, FB_BLANK_POWERDOWN);
	}
	if (use_gl)
		hud_gl_blank(1);
	logmsg("screen off");
}

static int bl_name_ok(const char *name)
{
	if (!name || name[0] == '.')
		return 0;
	if (strstr(name, "button") || strstr(name, "kbd") ||
	    strstr(name, "keyboard"))
		return 0;
	return 1;
}

static void find_backlight(void)
{
	DIR *d = opendir("/sys/class/backlight");
	struct dirent *ent;
	char best[192] = "";
	int best_score = -1;

	if (!d)
		return;
	while ((ent = readdir(d))) {
		char path[192], br[192];
		int score = 0;
		long mx, cur;

		if (!bl_name_ok(ent->d_name))
			continue;
		if (strstr(ent->d_name, "wled") || strstr(ent->d_name, "panel"))
			score = 2;
		else
			score = 1;
		snprintf(path, sizeof(path), "/sys/class/backlight/%s/max_brightness",
			 ent->d_name);
		mx = read_long(path);
		if (mx < 1)
			continue;
		if (score > best_score) {
			best_score = score;
			snprintf(best, sizeof(best), "%s", ent->d_name);
			bl_max = (int)mx;
			snprintf(br, sizeof(br), "/sys/class/backlight/%s/brightness",
				 ent->d_name);
			snprintf(bl_path, sizeof(bl_path), "%s", br);
			cur = read_long(br);
			if (cur > 0)
				bl_cur = (int)cur;
		}
	}
	closedir(d);
	if (best_score < 0)
		return;
	{
		char pwr[192];
		int fd;

		snprintf(pwr, sizeof(pwr), "/sys/class/backlight/%s/bl_power", best);
		snprintf(bl_power_path, sizeof(bl_power_path), "%s", pwr);
		fd = open(pwr, O_WRONLY | O_CLOEXEC);
		if (fd >= 0) {
			(void)!write(fd, "0\n", 2);
			close(fd);
		}
	}
	if (bl_pct > 0) {
		bl_keep = bl_pct;
		apply_bl_pct(bl_pct);
	} else if (bl_max > 0 && bl_cur > 0) {
		bl_pct = clampi((bl_cur * 100 + bl_max / 2) / bl_max, 1, 100);
		bl_keep = bl_pct;
	} else {
		bl_pct = 80;
		bl_keep = 80;
	}
}

static void sysfs_wifi(void)
{
	DIR *d;
	struct dirent *ent;
	char path[160];

	if (wifi.iface[0]) {
		snprintf(path, sizeof(path), "/sys/class/net/%s/phy80211", wifi.iface);
		if (access(path, F_OK) != 0) {
			snprintf(path, sizeof(path), "/sys/class/net/%s/wireless", wifi.iface);
			if (access(path, F_OK) != 0) {
				wifi.have_dev = 0;
				wifi.iface[0] = 0;
			}
		}
		if (wifi.iface[0]) {
			wifi.have_dev = 1;
			return;
		}
	}
	d = opendir("/sys/class/net");
	if (!d)
		return;
	while ((ent = readdir(d))) {
		if (ent->d_name[0] == '.')
			continue;
		snprintf(path, sizeof(path), "/sys/class/net/%s/phy80211", ent->d_name);
		if (access(path, F_OK) != 0) {
			snprintf(path, sizeof(path), "/sys/class/net/%s/wireless", ent->d_name);
			if (access(path, F_OK) != 0)
				continue;
		}
		wifi.have_dev = 1;
		if (!wifi.iface[0])
			snprintf(wifi.iface, sizeof(wifi.iface), "%s", ent->d_name);
		break;
	}
	closedir(d);
}

static void rfkill_unblock(void)
{
	DIR *d = opendir("/sys/class/rfkill");
	struct dirent *ent;
	char path[128];
	int fd;

	if (!d)
		return;
	while ((ent = readdir(d))) {
		if (ent->d_name[0] == '.')
			continue;
		snprintf(path, sizeof(path), "/sys/class/rfkill/%s/state", ent->d_name);
		fd = open(path, O_WRONLY);
		if (fd >= 0) {
			(void)!write(fd, "1\n", 2);
			close(fd);
		}
	}
	closedir(d);
}

static void wifi_radio(void)
{
	char *argv[] = { "nmcli", "-c", "no", "radio", "wifi", "on", NULL };

	if (start_nmcli(0, argv) < 0)
		want_info = 1;
}

static void wifi_info(void)
{
	char *argv[] = { "nmcli", "-c", "no", "-t", "-f",
			 "DEVICE,TYPE,STATE,CONNECTION", "device", NULL };

	if (start_nmcli(1, argv) < 0)
		want_info = 1;
}

static void wifi_scan(void)
{
	char *argv[] = {
		"nmcli", "-c", "no", "-w", "25", "-t", "-f", "SSID,SIGNAL,SECURITY,FREQ,ACTIVE",
		"device", "wifi", "list", "--rescan", "yes", NULL
	};

	if (start_nmcli(2, argv) < 0)
		want_scan = 1;
}

static void wifi_link(void)
{
	static char ifn[32];
	char *argv_iw[] = { "iw", "dev", ifn, "link", NULL };
	char *argv_nm[] = {
		"nmcli", "-c", "no", "-t", "-f", "IN-USE,SIGNAL,FREQ,SSID",
		"device", "wifi", "list", "--rescan", "no", NULL
	};

	if (wifi.iface[0]) {
		snprintf(ifn, sizeof(ifn), "%s", wifi.iface);
		if (!access("/usr/sbin/iw", X_OK) || !access("/usr/bin/iw", X_OK)) {
			if (start_nmcli(4, argv_iw) < 0)
				want_link = 1;
			return;
		}
	}
	if (start_nmcli(4, argv_nm) < 0)
		want_link = 1;
}

static void remember_psk(const char *ssid, const char *pass)
{
	if (!ssid || !ssid[0] || !pass || !pass[0])
		return;
	snprintf(known_psk_ssid, sizeof(known_psk_ssid), "%s", ssid);
	snprintf(known_psk, sizeof(known_psk), "%s", pass);
}

static void forget_psk(const char *ssid)
{
	if (!gw_ssid_eq(known_psk_ssid, ssid))
		return;
	known_psk_ssid[0] = 0;
	known_psk[0] = 0;
}

static void wifi_connect(const char *ssid, const char *pass, const char *sec)
{
	static char s1[96], s2[PW_MAX], s3[32], s4[40];
	char *argv[8];

	if (!ssid || !ssid[0])
		return;
	forget_ssid[0] = 0;
	connecting = 1;
	connect_fail = 0;
	join_wait_live = 0;
	wifi.busy = 1;
	snprintf(connect_ssid, sizeof(connect_ssid), "%s", ssid);
	snprintf(status_line, sizeof(status_line), "%s",
		 T("正在连接…", "Connecting…"));
	/* Keep the sheet open until the new SSID is actually up (iOS join).
	 * Do not clobber the live SSID; the previous AP stays associated
	 * until NetworkManager finishes switching. */
	if (job_pid > 0 && job_kind != 3)
		abort_job();
	if (job_pid > 0) {
		if (gw_keep_pending_psk(!(pass && pass[0]), want_connect,
					pend_ssid, pend_pass, ssid))
			return;
		snprintf(pend_ssid, sizeof(pend_ssid), "%s", ssid);
		snprintf(pend_pass, sizeof(pend_pass), "%s", pass ? pass : "");
		snprintf(pend_sec, sizeof(pend_sec), "%s", sec ? sec : "");
		want_connect = 1;
		return;
	}
	snprintf(s1, sizeof(s1), "%s", ssid);
	snprintf(s2, sizeof(s2), "%s", pass ? pass : "");
	snprintf(s3, sizeof(s3), "%s", wifi.iface);
	snprintf(s4, sizeof(s4), "%s", sec ? sec : "");
	argv[0] = "/bin/sh";
	argv[1] = "/usr/local/sbin/ginkgo-wifi-connect.sh";
	argv[2] = s1;
	argv[3] = s2;
	argv[4] = s3;
	argv[5] = s4;
	argv[6] = NULL;
	start_nmcli(3, argv);
}

static void wifi_submit_pass(void)
{
	const char *ssid = gw_pass_ssid(connect_ssid, selected_ssid);
	const char *sec = picked_sec();

	if (!ssid[0] || forgetting)
		return;
	if (wifi_sec_enterprise(sec)) {
		connect_fail = 1;
		snprintf(status_line, sizeof(status_line), "%s",
			 T("暂不支持企业级网络", "Enterprise Wi-Fi is not supported"));
		return;
	}
	if (pw_len < 8) {
		connect_fail = 1;
		snprintf(status_line, sizeof(status_line), "%s",
			 T("密码至少 8 位", "Password needs 8+ characters"));
		return;
	}
	connect_need_pass = 1;
	remember_psk(ssid, password);
	if (connecting) {
		snprintf(pend_ssid, sizeof(pend_ssid), "%s", ssid);
		snprintf(pend_pass, sizeof(pend_pass), "%s", password);
		snprintf(pend_sec, sizeof(pend_sec), "%s", sec ? sec : "");
		want_connect = 1;
		return;
	}
	wifi_connect(ssid, password, sec);
}

static void wifi_forget(const char *ssid)
{
	static char s1[96], s2[32];
	char *argv[7];

	if (!ssid || !ssid[0])
		return;
	if (!gw_forget_can_start(forgetting, job_pid > 0, job_kind == 5))
		return;
	join_wait_live = 0;
	connecting = 0;
	wifi.busy = 0;
	want_connect = 0;
	want_scan = 0;
	want_info = 0;
	want_link = 0;
	forgetting = 1;
	snprintf(forget_ssid, sizeof(forget_ssid), "%s", ssid);
	mark_ap_inactive(ssid);
	if (gw_ssid_eq(wifi.ssid, ssid)) {
		wifi.connected = 0;
		wifi.busy = 0;
		wifi.ssid[0] = 0;
		wifi.ip[0] = 0;
		wifi.gw[0] = 0;
		t_assoc = 0;
	}
	snprintf(status_line, sizeof(status_line), "%s",
		 T("正在忘记…", "Forgetting…"));
	if (job_pid > 0 && job_kind != 5)
		abort_job();
	if (job_pid > 0)
		return;
	snprintf(s1, sizeof(s1), "%s", ssid);
	snprintf(s2, sizeof(s2), "%s", wifi.iface);
	argv[0] = "/bin/sh";
	argv[1] = "/usr/local/sbin/ginkgo-wifi-connect.sh";
	argv[2] = "forget";
	argv[3] = s1;
	argv[4] = s2;
	argv[5] = NULL;
	if (start_nmcli(5, argv) < 0)
		forgetting = 0;
}

static void wifi_join_picked(void)
{
	const char *ssid = selected_ssid;
	const char *sec = picked_sec();

	if (!ssid[0] || connecting || forgetting)
		return;
	if (!gw_allow_join(connecting, forgetting, ap_is_current(ssid)))
		return;
	if (wifi_sec_enterprise(sec)) {
		snprintf(status_line, sizeof(status_line), "%s",
			 T("暂不支持企业级网络", "Enterprise Wi-Fi is not supported"));
		return;
	}
	if (gw_join_use_session_psk(ssid, known_psk_ssid, known_psk)) {
		connect_need_pass = 1;
		wifi_connect(ssid, known_psk, sec);
		return;
	}
	connect_need_pass = 0;
	wifi_connect(ssid, "", sec);
}

static void kick_pending(void)
{
	if (job_pid > 0)
		return;
	if (want_forget) {
		want_forget = 0;
		wifi_forget(pend_forget);
		return;
	}
	if (want_connect) {
		want_connect = 0;
		wifi_connect(pend_ssid, pend_pass, pend_sec);
		return;
	}
	if (want_scan) {
		want_scan = 0;
		wifi_scan();
		return;
	}
	if (want_link) {
		want_link = 0;
		wifi_link();
		return;
	}
	if (want_info) {
		want_info = 0;
		wifi_info();
	}
}

static void apply_wifi_ssid(const char *s)
{
	char *nl;
	char tmp[96];
	size_t cur_n, inc_n;

	if (!s)
		return;
	while (*s == ' ' || *s == '\t')
		s++;
	if (!s[0] || !strcmp(s, "--"))
		return;
	/* Old connect.sh used NM profile id gemini-<SSID>. Never treat that
	 * id as the broadcast name. */
	if (!strncmp(s, "gemini-", 7) && s[7])
		s += 7;
	snprintf(tmp, sizeof(tmp), "%s", s);
	nl = strchr(tmp, '\n');
	if (nl)
		*nl = 0;
	nl = strchr(tmp, '\r');
	if (nl)
		*nl = 0;
	inc_n = strlen(tmp);
	cur_n = strlen(wifi.ssid);
	/* Ignore a 2.4 GHz sibling of the network we are joining, but DO
	 * apply a different SSID so we can see NM fall back to the old AP. */
	if (connect_ssid[0] && strcmp(tmp, connect_ssid)) {
		size_t want = strlen(connect_ssid);

		if (inc_n < want && !strncmp(connect_ssid, tmp, inc_n) &&
		    (connect_ssid[inc_n] == '-' || connect_ssid[inc_n] == '_'))
			return;
		if (want < inc_n && !strncmp(tmp, connect_ssid, want) &&
		    (tmp[want] == '-' || tmp[want] == '_'))
			return;
	}
	if (gw_ssid_eq(forget_ssid, tmp))
		return;
	if (forget_ssid[0] && tmp[0])
		forget_ssid[0] = 0;
	if (cur_n > inc_n && !strncmp(wifi.ssid, tmp, inc_n) &&
	    (wifi.ssid[inc_n] == '-' || wifi.ssid[inc_n] == '_'))
		return;
	snprintf(wifi.ssid, sizeof(wifi.ssid), "%s", tmp);
}

static const char *display_ssid(void)
{
	int i;
	const char *live = gw_display_ssid(wifi_is_joining(), connect_ssid,
					   wifi.ssid);

	if (live && live[0])
		return live;
	for (i = 0; i < nap; i++) {
		if (aps[i].active && aps[i].ssid[0] &&
		    !gw_ssid_eq(forget_ssid, aps[i].ssid))
			return aps[i].ssid;
	}
	return "Wi-Fi";
}

static void maybe_confirm_join(void)
{
	int timed_out;

	if (!join_wait_live || !connect_ssid[0])
		return;
	if (gw_join_confirm_ok(wifi.connected, wifi.ssid, connect_ssid)) {
		join_wait_live = 0;
		connecting = 0;
		wifi.busy = 0;
		connect_fail = 0;
		mark_ap_active(connect_ssid);
		sheet_tgt = SHEET_NONE;
		snprintf(status_line, sizeof(status_line), "%s",
			 T("已连接", "Connected"));
		want_scan = 1;
		dirty();
		return;
	}
	timed_out = t_join_wait &&
		    nsec_now() - t_join_wait > 12000000000ull;
	if (!gw_join_confirm_fail(timed_out, wifi.connected, wifi.ssid,
				  connect_ssid))
		return;
	join_wait_live = 0;
	connecting = 0;
	wifi.busy = 0;
	connect_fail = 1;
	if (last_fail_kind == GW_FAIL_UNKNOWN)
		last_fail_kind = GW_FAIL_TIMEOUT;
	fill_fail_status(last_fail_kind,
			 gw_join_fell_back(wifi.connected, wifi.ssid,
					   connect_ssid)
			 ? wifi.ssid : NULL);
	if (sheet_tgt == SHEET_NONE)
		sheet_tgt = SHEET_DETAIL;
	dirty();
}

static void parse_info(char *buf)
{
	char *line, *save;
	int saw_wifi = 0, wifi_up = 0, wifi_busy = 0;
	char iface[32] = "";

	if (strstr(buf, "Error") || strstr(buf, "error:")) {
		char *nl = strchr(buf, '\n');
		if (nl)
			*nl = 0;
		snprintf(status_line, sizeof(status_line), "%s", buf);
		return;
	}
	line = strtok_r(buf, "\n", &save);
	while (line) {
		char *fields[6];
		int nf = 0;
		char *p = line, *start = line;

		while (*p && nf < 5) {
			if (*p == ':') {
				*p = 0;
				fields[nf++] = start;
				start = p + 1;
			}
			p++;
		}
		fields[nf++] = start;
		/* TYPE must be exactly "wifi". "wifi-p2p" used to match
		 * strstr and then overwrite connected with disconnected. */
		if (nf >= 3 && !strcmp(fields[1], "wifi") &&
		    strncmp(fields[0], "p2p-", 4)) {
			const char *st = fields[2];

			saw_wifi = 1;
			snprintf(iface, sizeof(iface), "%s", fields[0]);
			if (strstr(st, "disconnected") ||
			    strstr(st, "unavailable") ||
			    strstr(st, "unmanaged")) {
				wifi_up = 0;
				wifi_busy = 0;
			} else if (strstr(st, "connecting") ||
				   strstr(st, "preparing") ||
				   strstr(st, "configuring") ||
				   strstr(st, "getting IP") ||
				   strstr(st, "checking IP")) {
				wifi_up = 0;
				wifi_busy = 1;
			} else if (strstr(st, "connected")) {
				wifi_up = 1;
				wifi_busy = 0;
			}
		}
		line = strtok_r(NULL, "\n", &save);
	}
	if (saw_wifi) {
		wifi.have_dev = 1;
		if (iface[0])
			snprintf(wifi.iface, sizeof(wifi.iface), "%s", iface);
		wifi.connected = wifi_up;
		wifi.busy = wifi_busy;
		if (gw_forget_hold_connected(forget_ssid, wifi.connected,
					     wifi.ssid)) {
			wifi.connected = 0;
			wifi.busy = 0;
			wifi.ssid[0] = 0;
			wifi.ip[0] = 0;
			wifi.gw[0] = 0;
			t_assoc = 0;
		} else if (!wifi_up && !wifi_busy && forget_ssid[0])
			forget_ssid[0] = 0;
		if (wifi.connected) {
			if (!t_assoc)
				t_assoc = nsec_now();
		} else if (!wifi.busy && !connecting && !join_wait_live) {
			t_assoc = 0;
			wifi.signal_dbm = 0;
		}
	}
}

static void sample_link(void)
{
	char path[160], st[16], ip[64];
	int fd;
	ssize_t n;

	if (!wifi.iface[0])
		return;
	snprintf(path, sizeof(path), "/sys/class/net/%s/operstate", wifi.iface);
	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return;
	n = read(fd, st, sizeof(st) - 1);
	close(fd);
	if (n <= 0)
		return;
	st[n] = 0;
	if (strncmp(st, "up", 2))
		return;
	if (gw_forget_hold_connected(forget_ssid, 1, wifi.ssid))
		return;
	if (iface_ipv4(wifi.iface, ip, sizeof(ip)))
		snprintf(wifi.ip, sizeof(wifi.ip), "%s", ip);
}

static void unescape_field(char *s)
{
	char *r = s, *w = s;
	while (*r) {
		if (*r == '\\' && r[1]) {
			r++;
			*w++ = *r++;
		} else
			*w++ = *r++;
	}
	*w = 0;
}

static void parse_scan(char *buf)
{
	char *line, *save;

	nap = 0;
	if (strstr(buf, "Error") || strstr(buf, "not running") ||
	    strstr(buf, "No such")) {
		char tmp[96];
		char *nl = strchr(buf, '\n');
		if (nl)
			*nl = 0;
		snprintf(tmp, sizeof(tmp), "%s", buf);
		snprintf(status_line, sizeof(status_line), "%s", tmp);
	}
	line = strtok_r(buf, "\n", &save);
	while (line && nap < MAX_AP) {
		char *p = line;
		char *fields[8];
		int nf = 0;
		char *start = p;
		while (*p && nf < 8) {
			if (*p == '\\' && p[1]) {
				p += 2;
				continue;
			}
			if (*p == ':') {
				*p = 0;
				fields[nf++] = start;
				start = p + 1;
			}
			p++;
		}
		fields[nf++] = start;
		if (nf >= 4 && fields[0][0]) {
			const char *sec = fields[2];
			const char *act = (nf >= 5) ? fields[4] : fields[3];

			unescape_field(fields[0]);
			snprintf(aps[nap].ssid, sizeof(aps[nap].ssid), "%s", fields[0]);
			aps[nap].signal = atoi(fields[1]);
			snprintf(aps[nap].security, sizeof(aps[nap].security), "%s", sec);
			aps[nap].freq[0] = 0;
			if (nf >= 5)
				snprintf(aps[nap].freq, sizeof(aps[nap].freq), "%s", fields[3]);
			aps[nap].secure = (sec[0] && strcmp(sec, "--"));
			aps[nap].active = (act[0] == 'y' || act[0] == 'Y') &&
					  !gw_ssid_eq(forget_ssid, fields[0]);
			nap++;
		}
		line = strtok_r(NULL, "\n", &save);
	}
	if (nap > 0) {
		if (hover_ap < 0 || hover_ap >= nap)
			hover_ap = 0;
		sync_selected_ap();
		if (selected_ap < 0 || selected_ap >= nap)
			selected_ap = hover_ap;
	}
	apply_active_ap();
}

static void parse_link(char *buf)
{
	char *line, *save, *p;
	int dbm = 0, mhz = 0;

	if ((p = strstr(buf, "SSID:")))
		apply_wifi_ssid(p + 5);
	if ((p = strstr(buf, "signal avg:"))) {
		if (sscanf(p, "signal avg: %d", &dbm) == 1)
			apply_signal_dbm(dbm);
	} else if ((p = strstr(buf, "signal:"))) {
		if (sscanf(p, "signal: %d", &dbm) == 1)
			apply_signal_dbm(dbm);
	}
	if ((p = strstr(buf, "\tfreq:"))) {
		if (sscanf(p, " freq: %d", &mhz) == 1 && mhz > 0)
			snprintf(wifi.freq, sizeof(wifi.freq), "%d", mhz);
	} else if ((p = strstr(buf, "freq:"))) {
		if (sscanf(p, "freq: %d", &mhz) == 1 && mhz > 0)
			snprintf(wifi.freq, sizeof(wifi.freq), "%d", mhz);
	}
	line = strtok_r(buf, "\n", &save);
	while (line) {
		char *fields[6];
		int nf = 0;
		char *q = line, *start = line;

		while (*q && nf < 4) {
			if (*q == '\\' && q[1]) {
				q += 2;
				continue;
			}
			if (*q == ':') {
				*q = 0;
				fields[nf++] = start;
				start = q + 1;
			}
			q++;
		}
		fields[nf++] = start;
		if (nf >= 3 && (fields[0][0] == '*' ||
				!strcmp(fields[0], "yes") ||
				!strcmp(fields[0], "Yes"))) {
			int sig = atoi(fields[1]);
			if (wifi.signal_dbm >= 0 && sig > 0)
				wifi.signal = clampi(sig, 0, 100);
			if (fields[2][0] && strcmp(fields[2], "--"))
				snprintf(wifi.freq, sizeof(wifi.freq), "%s", fields[2]);
			if (nf >= 4 && fields[3][0] && strcmp(fields[3], "--")) {
				unescape_field(fields[3]);
				apply_wifi_ssid(fields[3]);
			}
			break;
		}
		line = strtok_r(NULL, "\n", &save);
	}
}

static void poll_job(void)
{
	int st;
	ssize_t n;

	if (job_pid <= 0)
		return;
	if (job_out[0] >= 0) {
		n = read(job_out[0], job_buf + job_n, sizeof(job_buf) - 1 - job_n);
		if (n > 0)
			job_n += (size_t)n;
	}
	if (waitpid(job_pid, &st, WNOHANG) == 0)
		return;
	if (job_out[0] >= 0) {
		while ((n = read(job_out[0], job_buf + job_n, sizeof(job_buf) - 1 - job_n)) > 0)
			job_n += (size_t)n;
	}
	job_buf[job_n] = 0;
	{
		int kind = job_kind;

		close_job();
		dirty();
		if (kind == 1) {
			parse_info(job_buf);
			if ((wifi.connected || wifi.busy || join_wait_live))
				want_link = 1;
			maybe_confirm_join();
		} else if (kind == 2) {
			parse_scan(job_buf);
			if (nap == 0 && !status_line[0] && !connect_fail &&
			    !join_wait_live)
				snprintf(status_line, sizeof(status_line), "%s",
					 T("没有扫描到网络，请再点刷新",
					   "No networks found, tap Refresh"));
			else if (nap > 0 && !connect_fail && !join_wait_live)
				snprintf(status_line, sizeof(status_line),
					 T("找到 %d 个网络", "Found %d networks"), nap);
			maybe_confirm_join();
		} else if (kind == 4) {
			parse_link(job_buf);
			maybe_confirm_join();
			if (join_wait_live)
				want_info = 1;
		} else if (kind == 0) {
			want_info = 1;
			want_scan = 1;
		} else if (kind == 3) {
			int rc = WIFEXITED(st) ? WEXITSTATUS(st) : 1;
			struct gw_connect_in cin;
			enum gw_connect_act act;
			int left_home = (sheet_tgt == SHEET_NONE);

			memset(&cin, 0, sizeof(cin));
			cin.rc = rc;
			cin.out = job_buf;
			cin.connected = wifi.connected;
			cin.live_ssid = wifi.ssid;
			cin.connect_ssid = connect_ssid;
			cin.left_home = left_home;
			cin.want_connect = want_connect;
			cin.pend_ssid = pend_ssid;
			cin.pend_has_pass = pend_pass[0] != 0;
			cin.used_pass = connect_need_pass;
			cin.have_session_psk =
				gw_join_use_session_psk(connect_ssid,
							known_psk_ssid,
							known_psk);
			act = gw_on_connect_job(&cin);
			snprintf(last_fail_raw, sizeof(last_fail_raw), "%s",
				 job_buf);
			last_fail_kind = gw_classify_fail(job_buf);

			connecting = 0;
			wifi.busy = 0;
			if (act == GW_CONN_SUCCESS) {
				connect_fail = 0;
				join_wait_live = 1;
				t_join_wait = nsec_now();
				connecting = 1;
				wifi.busy = 1;
				if (gw_drop_pending(1, connect_ssid, pend_ssid))
					want_connect = 0;
				snprintf(status_line, sizeof(status_line), "%s",
					 T("正在确认连接…", "Confirming…"));
				if (sheet_tgt == SHEET_PASS)
					sheet_tgt = SHEET_DETAIL;
				want_info = 1;
				want_link = 1;
				maybe_confirm_join();
			} else if (act == GW_CONN_RETRY_PSK) {
				connect_fail = 0;
				if (gw_ssid_eq(pend_ssid, connect_ssid) &&
				    !pend_pass[0])
					want_connect = 0;
				wifi_connect(connect_ssid, known_psk,
					     picked_sec());
			} else if (act == GW_CONN_WRONG_PASS) {
				forget_psk(connect_ssid);
				connect_fail = 1;
				fill_fail_status(GW_FAIL_WRONG_PW, NULL);
				if (connect_ssid[0]) {
					snprintf(selected_ssid,
						 sizeof(selected_ssid), "%s",
						 connect_ssid);
					sync_selected_ap();
				}
				sheet_tgt = SHEET_PASS;
			} else if (act == GW_CONN_OPEN_PASS) {
				connect_fail = connect_need_pass ? 1 : 0;
				set_connect_status(job_buf[0] ? job_buf
							      : "NEED_PASSWORD");
				if (connect_ssid[0]) {
					snprintf(selected_ssid,
						 sizeof(selected_ssid), "%s",
						 connect_ssid);
					sync_selected_ap();
				}
				sheet_tgt = SHEET_PASS;
				/* Keep what the user already typed for this SSID. */
				if (!connect_need_pass && !pw_len) {
					pw_len = 0;
					password[0] = 0;
					shift_on = 0;
					sym_on = 0;
					osk_sym2 = 0;
				}
			} else if (act == GW_CONN_WAIT_PENDING) {
				connect_fail = 0;
			} else {
				connect_fail = 1;
				fill_fail_status(last_fail_kind,
						 gw_join_fell_back(wifi.connected,
								   wifi.ssid,
								   connect_ssid)
						 ? wifi.ssid : NULL);
				if (!left_home)
					sheet_tgt = connect_need_pass ? SHEET_PASS
								      : SHEET_DETAIL;
			}
		} else if (kind == 5) {
			int rc = WIFEXITED(st) ? WEXITSTATUS(st) : 1;

			forgetting = 0;
			if (rc == 0) {
				mark_ap_inactive(forget_ssid[0] ? forget_ssid
								 : selected_ssid);
				if (gw_ssid_eq(wifi.ssid, selected_ssid) ||
				    gw_ssid_eq(wifi.ssid, forget_ssid) ||
				    gw_forget_hold_connected(forget_ssid, 1,
							     wifi.ssid)) {
					wifi.connected = 0;
					wifi.ssid[0] = 0;
					wifi.ip[0] = 0;
					wifi.gw[0] = 0;
					t_assoc = 0;
				}
				forget_psk(selected_ssid);
				snprintf(status_line, sizeof(status_line), "%s",
					 T("已忘记此网络", "Forgot this network"));
				sheet_tgt = SHEET_LIST;
				want_info = 1;
				want_scan = 1;
			} else {
				set_connect_status(job_buf[0] ? job_buf
							      : T("忘记失败", "Forget failed"));
				sheet_tgt = SHEET_DETAIL;
			}
		}
		kick_pending();
	}
}

static int test_bit(int bit, const unsigned char *arr)
{
	return (arr[bit / 8] >> (bit % 8)) & 1;
}

static void open_event(const char *path, int num)
{
	int fd = open(path, O_RDONLY | O_NONBLOCK);
	int one = 1;
	unsigned char evbit[(EV_MAX + 7) / 8];
	unsigned char keybit[(KEY_MAX + 7) / 8];
	unsigned char absbit[(ABS_MAX + 7) / 8];
	struct input_absinfo ax, ay;
	int useful_key = 0, useful_touch = 0, mt = 0;

	if (fd < 0)
		return;
	memset(evbit, 0, sizeof(evbit));
	memset(keybit, 0, sizeof(keybit));
	memset(absbit, 0, sizeof(absbit));
	ioctl(fd, EVIOCGBIT(0, sizeof(evbit)), evbit);
	if (test_bit(EV_KEY, evbit))
		ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybit)), keybit);
	if (test_bit(EV_ABS, evbit))
		ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(absbit)), absbit);
	useful_key = test_bit(KEY_POWER, keybit) ||
		     test_bit(KEY_VOLUMEUP, keybit) ||
		     test_bit(KEY_VOLUMEDOWN, keybit);
	mt = test_bit(ABS_MT_POSITION_X, absbit) &&
	     test_bit(ABS_MT_POSITION_Y, absbit);
	useful_touch = mt || (test_bit(ABS_X, absbit) && test_bit(ABS_Y, absbit) &&
			      test_bit(BTN_TOUCH, keybit));
	/* Skip haptics / FF-only and anything we do not need. */
	if (!useful_key && !useful_touch) {
		close(fd);
		return;
	}
	if (ioctl(fd, EVIOCGRAB, &one) < 0)
		logmsg("input grab failed, reading anyway");
	pfds[npfd].fd = fd;
	pfds[npfd].events = POLLIN;
	is_touch[npfd] = 0;
	has_mt[npfd] = 0;
	if (mt && ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &ax) == 0 &&
	    ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &ay) == 0 &&
	    ax.maximum > ax.minimum && ay.maximum > ay.minimum) {
		struct input_absinfo sl;

		is_touch[npfd] = 1;
		has_mt[npfd] = 1;
		abs_min_x[npfd] = ax.minimum;
		abs_max_x[npfd] = ax.maximum;
		abs_min_y[npfd] = ay.minimum;
		abs_max_y[npfd] = ay.maximum;
		if (ioctl(fd, EVIOCGABS(ABS_MT_SLOT), &sl) == 0 &&
		    sl.maximum >= 0 && sl.maximum < MAX_SLOTS)
			mt_nslots = sl.maximum + 1;
		{
			char line[96];

			snprintf(line, sizeof(line), "touch %s %d..%d x %d..%d slots=%d",
				 path, ax.minimum, ax.maximum, ay.minimum, ay.maximum,
				 mt_nslots);
			logmsg(line);
		}
	} else if (useful_touch &&
		   ioctl(fd, EVIOCGABS(ABS_X), &ax) == 0 &&
		   ioctl(fd, EVIOCGABS(ABS_Y), &ay) == 0 &&
		   ax.maximum > ax.minimum && ay.maximum > ay.minimum) {
		is_touch[npfd] = 1;
		abs_min_x[npfd] = ax.minimum;
		abs_max_x[npfd] = ax.maximum;
		abs_min_y[npfd] = ay.minimum;
		abs_max_y[npfd] = ay.maximum;
	}
	npfd++;
	if (num >= 0 && num < 32)
		opened_ev |= 1u << num;
}

static void scan_input(void)
{
	DIR *d = opendir("/dev/input");
	struct dirent *ent;

	if (!d)
		return;
	while ((ent = readdir(d)) && npfd < MAX_EV) {
		char path[64];
		int num;
		if (strncmp(ent->d_name, "event", 5))
			continue;
		num = atoi(ent->d_name + 5);
		if (num >= 0 && num < 32 && (opened_ev & (1u << num)))
			continue;
		snprintf(path, sizeof(path), "/dev/input/%s", ent->d_name);
		open_event(path, num);
	}
	closedir(d);
}

static void osk_feed(const char *key);
static void fill_tri_v(int cx, int y, int h, int up, uint32_t c);

static int osk_kh(void) { return 86; }
static int osk_gap(void) { return 10; }
static int osk_top(void)
{
	return H - 4 * (osk_kh() + osk_gap()) - 18;
}

static void osk_add(int kind, const char *lab, char ch, int x, int y, int w, int h)
{
	struct osk_key *k;

	if (nosk >= OSK_MAX || w < 8 || h < 8)
		return;
	k = &osk_keys[nosk++];
	k->lab[0] = 0;
	if (lab)
		snprintf(k->lab, sizeof(k->lab), "%s", lab);
	k->ch = ch;
	k->kind = (unsigned char)kind;
	k->x = (short)x;
	k->y = (short)y;
	k->w = (short)w;
	k->h = (short)h;
}

static void osk_add_row(const char *s, int y, int inset)
{
	int n, i, gap = osk_gap(), side = 10, inner, kw, x, extra;

	if (!s || !s[0])
		return;
	n = (int)strlen(s);
	inner = W - side * 2 - inset * 2;
	kw = (inner - (n - 1) * gap) / n;
	if (kw < 48)
		kw = 48;
	extra = inner - n * kw - (n - 1) * gap;
	x = side + inset + extra / 2;
	for (i = 0; i < n; i++) {
		char lab[2] = { s[i], 0 };

		osk_add(OSK_CHAR, lab, s[i], x, y, kw, osk_kh());
		x += kw + gap;
	}
}

static void osk_layout(void)
{
	int kh = osk_kh(), gap = osk_gap(), side = 10;
	int y, spec, bk, inner, rest, kw, x, i, n;
	const char *r0, *r1, *r2, *punct;

	nosk = 0;
	if (sym_on) {
		if (osk_sym2) {
			r0 = "[]{}#%^*+=";
			r1 = "_\\|~<>$&@*";
		} else {
			r0 = "1234567890";
			r1 = "-/:;()$&@\"";
		}
		r2 = NULL;
		punct = osk_sym2 ? ".,?!'" : ".,?!'<>";
	} else if (shift_on) {
		r0 = "QWERTYUIOP";
		r1 = "ASDFGHJKL";
		r2 = "ZXCVBNM";
		punct = NULL;
	} else {
		r0 = "qwertyuiop";
		r1 = "asdfghjkl";
		r2 = "zxcvbnm";
		punct = NULL;
	}

	y = osk_top();
	osk_add_row(r0, y, 0);
	{
		int kw10 = (W - side * 2 - 9 * gap) / 10;
		int inset = (kw10 + gap) / 2;

		y += kh + gap;
		osk_add_row(r1, y, (r2 && strlen(r1) == 9) ? inset : 0);
	}

	y += kh + gap;
	inner = W - side * 2;
	spec = 132;
	bk = 132;
	if (r2) {
		n = (int)strlen(r2);
		rest = inner - spec - bk - 2 * gap;
		kw = (rest - (n - 1) * gap) / n;
		x = side;
		osk_add(OSK_SHIFT, "", 0, x, y, spec, kh);
		x += spec + gap;
		for (i = 0; i < n; i++) {
			char lab[2] = { r2[i], 0 };

			osk_add(OSK_CHAR, lab, r2[i], x, y, kw, kh);
			x += kw + gap;
		}
		osk_add(OSK_BKSP, T("删", "del"), 0, side + inner - bk, y, bk, kh);
	} else {
		n = (int)strlen(punct);
		rest = inner - spec - bk - 2 * gap;
		kw = (rest - (n - 1) * gap) / n;
		x = side;
		osk_add(OSK_MORE, osk_sym2 ? "123" : "#+=", 0, x, y, spec, kh);
		x += spec + gap;
		for (i = 0; i < n; i++) {
			char lab[2] = { punct[i], 0 };

			osk_add(OSK_CHAR, lab, punct[i], x, y, kw, kh);
			x += kw + gap;
		}
		osk_add(OSK_BKSP, T("删", "del"), 0, side + inner - bk, y, bk, kh);
	}

	y += kh + gap;
	spec = 156;
	bk = 176;
	rest = inner - spec - bk - 2 * gap;
	x = side;
	osk_add(OSK_SYM, sym_on ? "ABC" : "?123", 0, x, y, spec, kh);
	x += spec + gap;
	osk_add(OSK_SPACE, "", ' ', x, y, rest, kh);
	x += rest + gap;
	osk_add(OSK_GO, T("连接", "Join"), 0, x, y, bk, kh);
}

static int osk_key_at(int x, int y)
{
	int i, best = -1, bd = 1 << 30, pad;

	pad = osk_gap() / 2 + 2;
	for (i = 0; i < nosk; i++) {
		const struct osk_key *k = &osk_keys[i];
		int hx = k->x - pad, hy = k->y - pad;
		int hw = k->w + pad * 2, hh = k->h + pad * 2;
		int cx, cy, dx, dy, d;

		if (!hit(x, y, hx, hy, hw, hh))
			continue;
		cx = k->x + k->w / 2;
		cy = k->y + k->h / 2;
		dx = x - cx;
		dy = y - cy;
		d = dx * dx + dy * dy;
		if (d < bd) {
			bd = d;
			best = i;
		}
	}
	return best;
}

static void osk_activate(const struct osk_key *k)
{
	char s[2];

	if (!k)
		return;
	switch (k->kind) {
	case OSK_CHAR:
		s[0] = k->ch;
		s[1] = 0;
		osk_feed(s);
		if (shift_on && !sym_on)
			shift_on = 0;
		break;
	case OSK_SHIFT:
		shift_on = !shift_on;
		break;
	case OSK_SYM:
		sym_on = !sym_on;
		osk_sym2 = 0;
		if (sym_on)
			shift_on = 0;
		break;
	case OSK_MORE:
		osk_sym2 = !osk_sym2;
		break;
	case OSK_SPACE:
		osk_feed(" ");
		break;
	case OSK_BKSP:
		osk_feed("bk");
		break;
	case OSK_GO:
		wifi_submit_pass();
		break;
	default:
		break;
	}
}

static int osk_on_down(int x, int y)
{
	if (sheet_tgt != SHEET_PASS || sheet_t < 0.45f)
		return 0;
	osk_layout();
	if (y < osk_top() - 12)
		return 0;
	osk_held = 1;
	osk_hi = osk_key_at(x, y);
	if (osk_hi >= 0)
		osk_anim = osk_hi;
	return 1;
}

static void osk_on_move(int x, int y)
{
	int i;

	if (!osk_held)
		return;
	osk_layout();
	i = osk_key_at(x, y);
	if (i != osk_hi) {
		osk_hi = i;
		if (i >= 0)
			osk_anim = i;
	}
}

static int osk_on_up(int x, int y)
{
	int i;

	if (!osk_held)
		return 0;
	osk_layout();
	i = osk_key_at(x, y);
	if (i < 0)
		i = osk_hi;
	if (i >= 0)
		osk_activate(&osk_keys[i]);
	osk_held = 0;
	osk_hi = -1;
	return 1;
}

static void draw_osk_shift(int x, int y, int w, int h, uint32_t c)
{
	int cx = x + w / 2;
	int cy = y + h / 2;

	fill_tri_v(cx, cy - 18, 18, 1, c);
	fill_round(cx - 4, cy - 4, 8, 16, 3, c);
}

static void draw_osk_key(int i)
{
	const struct osk_key *k = &osk_keys[i];
	float u = (i == osk_anim) ? osk_u : 0.f;
	int special = (k->kind != OSK_CHAR && k->kind != OSK_SPACE);
	int go = (k->kind == OSK_GO);
	int shift_lit = (k->kind == OSK_SHIFT && shift_on);
	uint32_t idle, lit, bg, fg;
	float sc;
	int x, y, w, h, r, a;

	a = (int)(u * 255.0f + 0.5f);
	if (go) {
		idle = px(91, 140, 255);
		lit = px(168, 196, 255);
	} else if (shift_lit) {
		idle = px(232, 236, 244);
		lit = px(255, 255, 255);
	} else if (special) {
		idle = px(46, 50, 60);
		lit = px(196, 200, 210);
	} else {
		idle = px(78, 82, 94);
		lit = px(236, 238, 244);
	}
	bg = mix(idle, lit, a);
	sc = 1.0f - 0.09f * u;
	w = (int)((float)k->w * sc + 0.5f);
	h = (int)((float)k->h * sc + 0.5f);
	x = k->x + (k->w - w) / 2;
	y = k->y + (k->h - h) / 2 + (int)(3.5f * u);
	r = h / 5;
	if (r < 10)
		r = 10;
	if (r > 16)
		r = 16;
	if (u > 0.04f)
		fill_round(x - 2, y + 5, w + 4, h + 3, r + 2,
			   mix(px(0, 0, 0), px(20, 22, 28), (int)(u * 90.0f)));
	fill_round(x, y, w, h, r, bg);
	if (shift_lit)
		fg = mix(px(28, 32, 40), px(20, 22, 28), a);
	else if (k->kind == OSK_CHAR)
		fg = mix(px(245, 248, 252), px(22, 24, 30), a);
	else
		fg = mix(px(230, 235, 245), px(28, 32, 40), a / 2);
	if (k->kind == OSK_SHIFT)
		draw_osk_shift(x, y, w, h, fg);
	else if (k->lab[0])
		draw_text_centered(special ? &font_body : &font_title, x, y, w, h,
				   k->lab, fg);
}

static void draw_osk(void)
{
	int y = osk_top();

	osk_layout();
	fill_rect(0, y - 16, W, H - (y - 16), px(16, 18, 24));
	fill_rect(0, y - 16, W, 1, px(40, 44, 54));
	{
		int i;

		for (i = 0; i < nosk; i++)
			draw_osk_key(i);
	}
}

static int wifi_card_y(void) { return 324; }
static int wifi_card_h(void)
{
	if (wifi_is_joining())
		return 380;
	if (wifi.connected)
		return 488;
	return 260;
}
static int net_card_h(void) { return 156; }
static int net_card_y(void)
{
	return wifi_card_y() + wifi_card_h() + 20;
}
static int net_speed_shown(void)
{
	return wifi.connected;
}
static int lang_x(void) { return W - 48 - 216; }
static int lang_y(void) { return 20; }
static int lang_w(void) { return 216; }
static int lang_h(void) { return 76; }
static int bl_y(void) { return 192; }
static int bl_h(void) { return 112; }
static int bl_bar_x(void) { return 220; }
static int bl_bar_h(void) { return 36; }
static int bl_bar_y(void) { return bl_y() + (bl_h() - bl_bar_h()) / 2; }
static int bl_bar_w(void) { return W - 220 - 180; }

static int hit_lang(int x, int y)
{
	return hit(x, y, lang_x(), lang_y(), lang_w(), lang_h());
}

static int hit_bl(int x, int y)
{
	return hit(x, y, 40, bl_y(), W - 80, bl_h());
}

static void apply_bl_from_x(int x)
{
	int bx = bl_bar_x(), bw = bl_bar_w();
	float t;

	if (bw < 1)
		return;
	t = (float)(x - bx) / (float)bw;
	apply_bl_pct((int)(clampf(t, 0, 1) * 100.0f + 0.5f));
}
static int meter_core_n(void)
{
	int cores = ncpu > 0 ? ncpu : 4;

	if (cores > MAX_CPU)
		cores = MAX_CPU;
	return cores;
}
static int meter_core_cols(void)
{
	return meter_core_n() > 4 ? 2 : 1;
}
static int meter_core_rows(void)
{
	int cols = meter_core_cols();
	int rows = (meter_core_n() + cols - 1) / cols;

	return rows < 1 ? 1 : rows;
}
static int meter_core_row_h(void) { return 64; }
static int meters_h(void)
{
	return 212 + meter_core_rows() * meter_core_row_h() + 24 + 120 + 36;
}
static int meters_y(void)
{
	int y = H - 72 - meters_h();
	int above = net_speed_shown() ? net_card_y() + net_card_h() :
		    wifi_card_y() + wifi_card_h();
	int min_y = above + 24;

	if (y < min_y)
		y = min_y;
	return y;
}
static int list_row_h(void) { return 168; }
static int sheet_peek(void)
{
	int p = H / 15;

	if (p < 100)
		p = 100;
	if (p > 168)
		p = 168;
	return p;
}

static int sheet_card_x(void) { return 16; }
static int sheet_card_w(void) { return W - 32; }
static int sheet_card_h(void)
{
	int h = H - sheet_peek() + 28;

	if (h < 520)
		h = 520;
	return h;
}
static int list_top_from(int y0) { return y0 + 280; }
static int sheet_y0(void);

static void hover_from_point(int x, int y)
{
	int y0 = sheet_y0();
	int top = list_top_from(y0);
	int i, rh = list_row_h();
	int cx = sheet_card_x() + 16;
	int cw = sheet_card_w() - 32;

	if (sheet_tgt != SHEET_LIST)
		return;
	for (i = 0; i < nap; i++) {
		int yy = top + i * rh + list_scroll;
		if (hit(x, y, cx, yy, cw, rh - 12)) {
			if (hover_ap != i)
				dirty();
			hover_ap = i;
			return;
		}
	}
}

static int sheet_y0(void)
{
	int open_y = H - sheet_card_h();
	int closed_y = H + 40;

	return (int)((float)closed_y + ((float)open_y - (float)closed_y) * sheet_t);
}

static void tap(int x, int y)
{
	int i, row_h, list_top, sh, y0 = sheet_y0();
	float shown = sheet_t;

	dirty();
	if (shown < 0.45f) {
		if (hit_lang(x, y)) {
			lang_en = !lang_en;
			save_hud_conf();
			if (nap > 0)
				snprintf(status_line, sizeof(status_line),
					 T("找到 %d 个网络", "Found %d networks"), nap);
			return;
		}
	}
	if (shown > 0.5f && sheet_tgt == SHEET_PASS) {
		if (hit(x, y, 40, y0 + 304, 200, 80)) {
			sheet_tgt = selected_ssid[0] ? SHEET_DETAIL : SHEET_LIST;
			connect_fail = 0;
			osk_held = 0;
			osk_hi = -1;
			osk_sym2 = 0;
			return;
		}
		if (hit(x, y, W - 280, y0 + 304, 240, 80)) {
			wifi_submit_pass();
			return;
		}
		return;
	}
	if (shown > 0.5f && sheet_tgt == SHEET_DETAIL) {
		int cx = sheet_card_x(), cw = sheet_card_w(), ch = sheet_card_h();
		int join_y = y0 + ch - 280;
		int forget_y = y0 + ch - 176;

		if (hit(x, y, cx + 24, y0 + 108, 220, 80)) {
			if (!forgetting)
				sheet_tgt = SHEET_LIST;
			return;
		}
		if (hit(x, y, cx + 24, join_y, cw - 48, 80)) {
			wifi_join_picked();
			return;
		}
		if (hit(x, y, cx + 24, forget_y, cw - 48, 80)) {
			if (selected_ssid[0])
				wifi_forget(selected_ssid);
			return;
		}
		return;
	}
	if (shown > 0.45f && sheet_tgt == SHEET_LIST) {
		int cx = sheet_card_x(), cw = sheet_card_w(), ch = sheet_card_h();

		if (sheet_tgt == SHEET_NONE)
			return;
		row_h = list_row_h();
		list_top = list_top_from(y0);
		sh = y0 + ch - 36 - list_top;
		if (!hit(x, y, cx, y0, cw, ch)) {
			if (!connecting)
				sheet_tgt = SHEET_NONE;
			return;
		}
		if (hit(x, y, cx + 20, y0 + 108, 220, 80)) {
			if (!connecting)
				sheet_tgt = SHEET_NONE;
			return;
		}
		if (hit(x, y, cx + cw - 240, y0 + 108, 220, 80)) {
			if (connecting)
				return;
			wifi_scan();
			snprintf(status_line, sizeof(status_line), "%s",
				 T("正在扫描…", "Scanning…"));
			return;
		}
		for (i = 0; i < nap; i++) {
			int yy = list_top + i * row_h + list_scroll;
			if (yy + row_h < list_top || yy > list_top + sh)
				continue;
			if (hit(x, y, cx + 16, yy, cw - 32, row_h - 12)) {
				select_ap_idx(i);
				if (connecting || forgetting)
					return;
				connect_fail = 0;
				sheet_tgt = SHEET_DETAIL;
				return;
			}
		}
		return;
	}
	if (hit(x, y, 40, wifi_card_y(), W - 80, wifi_card_h())) {
		if (wifi_is_assoc() && !wifi.connected)
			return;
		sheet_tgt = SHEET_LIST;
		if (nap == 0)
			wifi_scan();
		return;
	}
}

static void osk_feed(const char *key)
{
	if (!strcmp(key, "bk")) {
		if (pw_len > 0)
			password[--pw_len] = 0;
		return;
	}
	if (pw_len >= PW_MAX - 1)
		return;
	password[pw_len++] = key[0];
	password[pw_len] = 0;
}

static void map_xy(int idx, int rawx, int rawy, int *x, int *y)
{
	int dx = abs_max_x[idx] - abs_min_x[idx];
	int dy = abs_max_y[idx] - abs_min_y[idx];

	if (dx < 1)
		dx = 1;
	if (dy < 1)
		dy = 1;
	*x = clampi((rawx - abs_min_x[idx]) * W / dx, 0, W - 1);
	*y = clampi((rawy - abs_min_y[idx]) * H / dy, 0, H - 1);
}

static void map_touch(int idx, int *x, int *y)
{
	map_xy(idx, slot_x, slot_y, x, y);
}

static void mt_note_pos(int s, int yaxis, int value)
{
	if (s < 0 || s >= MAX_SLOTS)
		return;
	if (yaxis) {
		mt_y[s] = value;
		mt_have_y[s] = 1;
	} else {
		mt_x[s] = value;
		mt_have_x[s] = 1;
	}
	mt_got_pos = 1;
	mt_fresh[s]++;
}

static void mt_tracking(int s, int value)
{
	if (s < 0 || s >= MAX_SLOTS)
		return;
	if (value < 0) {
		mt_id[s] = -1;
		mt_have_x[s] = 0;
		mt_have_y[s] = 0;
		mt_major[s] = 0;
		return;
	}
	mt_id[s] = value;
}

static int mt_nraw;

static int mt_ioctl_slots(int fd, unsigned int code, int32_t *out)
{
	int n = mt_nslots;
	char buf[4 + MAX_SLOTS * 4];
	size_t sz;
	int i;

	if (n < 1 || n > MAX_SLOTS)
		n = MAX_SLOTS;
	sz = 4 + (size_t)n * 4;
	memset(buf, 0, sizeof(buf));
	memcpy(buf, &code, 4);
	if (ioctl(fd, EVIOCGMTSLOTS(sz), buf) < 0)
		return -1;
	memcpy(out, buf + 4, (size_t)n * 4);
	for (i = n; i < MAX_SLOTS; i++)
		out[i] = (code == ABS_MT_TRACKING_ID) ? -1 : 0;
	return 0;
}

static void mt_sync_from_kernel(int fd)
{
	int32_t xs[MAX_SLOTS], ys[MAX_SLOTS], ids[MAX_SLOTS];
	int32_t maj[MAX_SLOTS], z[MAX_SLOTS];
	int s, n = mt_nslots;

	memset(xs, 0, sizeof(xs));
	memset(ys, 0, sizeof(ys));
	memset(ids, 0xff, sizeof(ids)); /* tracking id -1 */
	memset(maj, 0, sizeof(maj));
	memset(z, 0, sizeof(z));
	if (n < 1 || n > MAX_SLOTS)
		n = MAX_SLOTS;
	if (mt_ioctl_slots(fd, ABS_MT_TRACKING_ID, ids) < 0)
		return;
	(void)mt_ioctl_slots(fd, ABS_MT_POSITION_X, xs);
	(void)mt_ioctl_slots(fd, ABS_MT_POSITION_Y, ys);
	(void)mt_ioctl_slots(fd, ABS_MT_TOUCH_MAJOR, maj);
	(void)mt_ioctl_slots(fd, ABS_MT_PRESSURE, z);
	for (s = 0; s < n; s++) {
		if (ids[s] >= 0 && ids[s] != mt_id[s])
			mt_fresh[s] = 1;
		mt_id[s] = ids[s];
		if (!mt_have_x[s]) {
			mt_x[s] = xs[s];
			if (xs[s])
				mt_have_x[s] = 1;
		}
		if (!mt_have_y[s]) {
			mt_y[s] = ys[s];
			if (ys[s])
				mt_have_y[s] = 1;
		}
		if (!mt_major[s] && maj[s])
			mt_major[s] = maj[s];
		if (!mt_z[s] && z[s])
			mt_z[s] = z[s];
		if (ids[s] < 0) {
			mt_have_x[s] = 0;
			mt_have_y[s] = 0;
			mt_major[s] = 0;
			mt_z[s] = 0;
		}
	}
	for (; s < MAX_SLOTS; s++) {
		mt_id[s] = -1;
		mt_have_x[s] = 0;
		mt_have_y[s] = 0;
	}
}

static void mt_clear_fresh(void)
{
	int s;

	for (s = 0; s < MAX_SLOTS; s++)
		mt_fresh[s] = 0;
}

static int mt_finger(int idx)
{
	int s, n = 0, hit = -1;

	mt_nraw = 0;
	for (s = 0; s < mt_nslots && s < MAX_SLOTS; s++) {
		if (mt_id[s] < 0)
			continue;
		mt_nraw++;
		if (!mt_have_x[s] || !mt_have_y[s])
			continue;
		if (mt_x[s] < abs_min_x[idx] || mt_x[s] > abs_max_x[idx])
			continue;
		if (mt_y[s] < abs_min_y[idx] || mt_y[s] > abs_max_y[idx])
			continue;
		if (!mt_fresh[s])
			continue;
		n++;
		hit = s;
	}
	if (n != 1)
		return -1;
	return hit;
}

static void mt_bind_primary(int idx)
{
	int s = -1;

	if (contact_armed && mt_bind_slot >= 0 && mt_bind_slot < mt_nslots) {
		s = mt_bind_slot;
		if (mt_id[s] < 0 || !mt_have_y[s])
			s = -1;
	} else {
		s = mt_finger(idx);
	}
	if (s < 0) {
		slot_id = -1;
		mt_bind_slot = -1;
		return;
	}
	mt_bind_slot = s;
	slot_id = mt_id[s];
	slot_x = mt_x[s];
	slot_y = mt_y[s];
}

static int tap_ok(int idx, uint64_t now)
{
	int range, dist;
	uint64_t dur;

	if (!t_down)
		return 0;
	dur = now - t_down;
	if (dur < 8000000ull || dur > 2000000000ull)
		return 0;
	range = abs_max_x[idx] - abs_min_x[idx];
	if (abs_max_y[idx] - abs_min_y[idx] > range)
		range = abs_max_y[idx] - abs_min_y[idx];
	if (range < 1)
		range = 1;
	dist = abs(slot_x - down_x) + abs(slot_y - down_y);
	return dist < range / 8;
}

static void handle_ev(int idx, uint16_t type, uint16_t code, int32_t value)
{
	int x, y, s, prev;
	uint64_t now = nsec_now();

	if (type == EV_KEY && value == 1 && code == KEY_VOLUMEUP) {
		do_reboot_fastboot();
		return;
	}
	if (type == EV_KEY && value == 1 && code == KEY_POWER) {
		if (now - t_power_key < 400000000ull)
			return;
		t_power_key = now;
		set_screen(screen_off);
		return;
	}
	if (is_touch[idx] && t_unblank &&
	    now - t_unblank < 250000000ull)
		return;
	if (screen_off) {
		/* Only a real finger (id + x + y) wakes. Pressure
		 * on empty slots and BTN_TOUCH emulation are hover junk. */
		if (is_touch[idx] && has_mt[idx] && type == EV_ABS) {
			if (code == ABS_MT_SLOT && value >= 0 &&
			    value < MAX_SLOTS)
				mt_cur = value;
			else if (code == ABS_MT_TRACKING_ID &&
				 mt_cur >= 0 && mt_cur < MAX_SLOTS)
				mt_tracking(mt_cur, value);
			else if (code == ABS_MT_POSITION_X &&
				 mt_cur >= 0 && mt_cur < MAX_SLOTS)
				mt_note_pos(mt_cur, 0, value);
			else if (code == ABS_MT_POSITION_Y &&
				 mt_cur >= 0 && mt_cur < MAX_SLOTS)
				mt_note_pos(mt_cur, 1, value);
			else if (code == ABS_MT_TOUCH_MAJOR &&
				 mt_cur >= 0 && mt_cur < MAX_SLOTS)
				mt_major[mt_cur] = value;
		}
		if (type == EV_SYN) {
			mt_sync_from_kernel(pfds[idx].fd);
			if (mt_finger(idx) >= 0)
				set_screen(1);
			mt_clear_fresh();
		}
		return;
	}
	if (!is_touch[idx])
		return;
	if (type == EV_ABS) {
		if (!has_mt[idx]) {
			if (code == ABS_X)
				slot_x = value;
			else if (code == ABS_Y)
				slot_y = value;
			return;
		}
		if (code == ABS_MT_SLOT) {
			if (value >= 0 && value < MAX_SLOTS)
				mt_cur = value;
			return;
		}
		if (mt_cur < 0 || mt_cur >= MAX_SLOTS)
			return;
		s = mt_cur;
		if (code == ABS_MT_TRACKING_ID)
			mt_tracking(s, value);
		else if (code == ABS_MT_POSITION_X)
			mt_note_pos(s, 0, value);
		else if (code == ABS_MT_POSITION_Y)
			mt_note_pos(s, 1, value);
		else if (code == ABS_MT_TOUCH_MAJOR)
			mt_major[s] = value;
		else if (code == ABS_MT_PRESSURE)
			mt_z[s] = value;
		return;
	}
	if (type == EV_KEY && (code == BTN_TOUCH || code == BTN_LEFT)) {
		if (has_mt[idx])
			return;
		if (value == 1) {
			slot_id = 1;
			dragging = 0;
			dragging_bl = 0;
			contact_armed = 0;
			t_contact = now;
			t_down = now;
			down_x = slot_x;
			down_y = slot_y;
			last_raw_x = slot_x;
			last_raw_y = slot_y;
			last_raw_ok = 1;
			map_touch(idx, &x, &y);
			osk_on_down(x, y);
		} else if (slot_id >= 0) {
			map_touch(idx, &x, &y);
			if (dragging_bl)
				commit_bl();
			if (osk_held)
				osk_on_up(x, y);
			else if (!dragging && tap_ok(idx, now))
				tap(x, y);
			reset_touch();
		}
		return;
	}
	if (type != EV_SYN)
		return;

	mt_sync_from_kernel(pfds[idx].fd);
	prev = slot_id;
	mt_bind_primary(idx);
	/* Extra leftover slots must not abort a live finger. Kernel
	 * already keeps one ellipse; EVIOCGMTSLOTS can still show a
	 * stale id for one SYN. */
	if (mt_nraw > 1 && !contact_armed && prev < 0) {
		mt_clear_fresh();
		return;
	}
	if (slot_id < 0) {
		if (prev >= 0) {
			map_touch(idx, &x, &y);
			if (dragging_bl)
				commit_bl();
			if (osk_held)
				osk_on_up(x, y);
			else if (!dragging && contact_armed && tap_ok(idx, now))
				tap(x, y);
			dragging = 0;
			dragging_bl = 0;
			contact_armed = 0;
		}
		mt_clear_fresh();
		return;
	}

	if (!contact_armed) {
		down_x = slot_x;
		down_y = slot_y;
		contact_armed = 1;
		dragging = 0;
		dragging_bl = 0;
		t_contact = now;
		t_down = now;
		last_raw_x = slot_x;
		last_raw_y = slot_y;
		last_raw_ok = 1;
		mt_got_pos = 0;
		map_touch(idx, &x, &y);
		osk_on_down(x, y);
		mt_clear_fresh();
		return;
	}
	if (!mt_got_pos) {
		mt_clear_fresh();
		return;
	}
	mt_got_pos = 0;
	last_raw_x = slot_x;
	last_raw_y = slot_y;
	last_raw_ok = 1;

	{
		int dy = slot_y - down_y;
		int dx = slot_x - down_x;
		int range = abs_max_y[idx] - abs_min_y[idx];

		map_touch(idx, &x, &y);
		if (sheet_tgt == SHEET_PASS && !dragging_bl) {
			if (osk_held)
				osk_on_move(x, y);
			else
				osk_on_down(x, y);
			if (osk_held) {
				mt_clear_fresh();
				return;
			}
		}
		if (range < 1)
			range = 1;
		if (!dragging && !dragging_bl && abs(dy) > range / 4) {
			down_x = slot_x;
			down_y = slot_y;
			mt_clear_fresh();
			return;
		}
		t_contact = now;
		{
			int down_sx, down_sy;

			map_xy(idx, down_x, down_y, &down_sx, &down_sy);
			if (!dragging && !dragging_bl &&
			    sheet_tgt == SHEET_NONE &&
			    hit_bl(down_sx, down_sy) && hit_bl(x, y) &&
			    abs(dx) + abs(dy) > 12) {
				dragging_bl = 1;
				dragging = 1;
			}
		}
		if (dragging_bl) {
			apply_bl_from_x(x);
			mt_clear_fresh();
			return;
		}
		if (dragging)
			hover_from_point(x, y);
		if (sheet_tgt == SHEET_LIST && abs(dy) > 64) {
			dragging = 1;
			list_scroll_tgt = list_scroll + dy * H / range;
			down_y = slot_y;
			dirty();
		}
	}
	mt_clear_fresh();
}

static int ev_keep(int idx, uint16_t type, uint16_t code)
{
	if (!is_touch[idx])
		return 1;
	if (type == EV_SYN || type == EV_KEY)
		return 1;
	if (type != EV_ABS)
		return 0;
	return code == ABS_MT_SLOT || code == ABS_MT_TRACKING_ID ||
	       code == ABS_MT_POSITION_X || code == ABS_MT_POSITION_Y ||
	       code == ABS_MT_TOUCH_MAJOR || code == ABS_MT_PRESSURE ||
	       code == ABS_X || code == ABS_Y;
}

static void drain_inputs(void)
{
	int i;

	poll(pfds, npfd, 0);
	for (i = 0; i < npfd; i++) {
		unsigned char buf[4096];
		ssize_t got;

		if (!(pfds[i].revents & POLLIN) &&
		    !(pfds[i].revents & POLLERR))
			continue;
		for (;;) {
			got = read(pfds[i].fd, buf, sizeof(buf));
			if (got < 24)
				break;
			if (got % 24 == 0) {
				ssize_t off;
				for (off = 0; off + 24 <= got; off += 24) {
					uint16_t type, code;
					int32_t value;
					memcpy(&type, buf + off + 16, 2);
					memcpy(&code, buf + off + 18, 2);
					memcpy(&value, buf + off + 20, 4);
					if (ev_keep(i, type, code))
						handle_ev(i, type, code, value);
				}
			} else if (got % 32 == 0) {
				ssize_t off;
				for (off = 0; off + 32 <= got; off += 32) {
					uint16_t type, code;
					int32_t value;
					memcpy(&type, buf + off + 16, 2);
					memcpy(&code, buf + off + 18, 2);
					memcpy(&value, buf + off + 24, 4);
					if (ev_keep(i, type, code))
						handle_ev(i, type, code, value);
				}
			} else {
				break;
			}
		}
	}
}

static void wait_deadline(uint64_t deadline)
{
	struct pollfd keys[MAX_EV];
	struct timespec ts;
	uint64_t now, rem;
	int i, n, wait_ms;

	while (nsec_now() < deadline) {
		now = nsec_now();
		rem = deadline - now;
		wait_ms = (int)(rem / 1000000ull);
		if (wait_ms > 32)
			wait_ms = 32;
		if (wait_ms < 1)
			wait_ms = 1;
		n = 0;
		for (i = 0; i < npfd; i++)
			keys[n++] = pfds[i];
		if (n > 0)
			poll(keys, n, wait_ms);
		else {
			ts.tv_sec = 0;
			ts.tv_nsec = (long)wait_ms * 1000000L;
			nanosleep(&ts, NULL);
		}
		drain_inputs();
	}
}

static ssize_t write_all(int fd, const void *buf, size_t len)
{
	const unsigned char *p = buf;
	size_t off = 0;

	while (off < len) {
		ssize_t n = write(fd, p + off, len - off);
		if (n < 0)
			return -1;
		if (n == 0)
			break;
		off += (size_t)n;
	}
	return (ssize_t)off;
}

static void present(void)
{
	uint32_t z = 0;
	int y;

	if (use_gl) {
		hud_gl_present();
		return;
	}
	/* write() dirties MSM fbdev scanout; mmap alone does not. */
	if (lseek(fb_fd, 0, SEEK_SET) < 0)
		return;
	if (pitch == W * 4) {
		write_all(fb_fd, back, (size_t)W * (size_t)H * 4);
	} else {
		for (y = 0; y < H; y++) {
			if (lseek(fb_fd, (off_t)y * pitch, SEEK_SET) < 0)
				break;
			write_all(fb_fd, back + y * W, (size_t)W * 4);
		}
	}
	ioctl(fb_fd, FBIO_WAITFORVSYNC, &z);
}

static void draw_signal(int x, int y, int sig)
{
	int i, on;

	if (sig <= 0)
		on = 0;
	else if (sig < 25)
		on = 1;
	else if (sig < 50)
		on = 2;
	else if (sig < 75)
		on = 3;
	else
		on = 4;

	for (i = 0; i < 4; i++) {
		int h = 8 + i * 8;
		uint32_t c = i < on ? px(80, 220, 160) : px(50, 58, 70);
		fill_round(x + i * 13, y + 32 - h, 9, h, 3, c);
	}
}

static void draw_loader(int cx, int y)
{
	int i, phase = ((int)(pulse * 3.2f)) % 3;

	for (i = 0; i < 3; i++) {
		int on = (i == phase);
		int s = on ? 22 : 16;
		int ox = cx - 54 + i * 40 + (22 - s) / 2;
		int oy = y + (22 - s) / 2;

		fill_round(ox, oy, s, s, s / 2,
			   on ? px(91, 140, 255) : px(50, 58, 70));
	}
}

static void connecting_label(char *out, int n)
{
	const char *ssid = display_ssid();
	int d = ((int)(pulse * 2.4f) % 4);
	char dots[5];

	memset(dots, '.', (size_t)d);
	dots[d] = 0;
	if (ssid[0])
		snprintf(out, n, "%s  %s%s",
			 T("正在连接", "Connecting"), ssid, dots);
	else
		snprintf(out, n, "%s%s", T("正在连接", "Connecting"), dots);
}

static void draw_bar(int x, int y, int w, int h, float v, uint32_t col)
{
	int fw;

	fill_round(x, y, w, h, h / 2, px(36, 42, 54));
	fw = (int)((float)w * clampf(v, 0, 1) + 0.5f);
	if (fw < 0)
		fw = 0;
	if (fw > w)
		fw = w;
	if (fw >= h)
		fill_round(x, y, fw, h, h / 2, col);
}

/* Title then subtitle, subtitle starts after the actual glyph width so
 * English "Memory" does not paint over "0.30 / 2.63 GB". */
static void draw_meter_head(int y, const char *lab, const char *sub, uint32_t sub_col)
{
	int x;

	draw_text(&font_title, 56, y, lab, px(245, 248, 252));
	if (!sub || !sub[0])
		return;
	x = 56 + text_w(&font_title, lab) + 28;
	if (x < 188)
		x = 188;
	draw_text(&font_body, x, y + 8, sub, sub_col);
}

static void draw_meters(void)
{
	int y = meters_y();
	int i, cores = meter_core_n();
	int cols = meter_core_cols();
	int rows = meter_core_rows();
	int rh = meter_core_row_h();
	int pct_x = W - 168;
	int bar_x = 188;
	int bar_w = pct_x - bar_x - 28;
	int gbar_w = pct_x - 56 - 28;
	int col_gap = 24, col_w, lab_w = 92;
	char line[80], gpu_sub[72];
	float used_gb, total_gb;

	fill_round(32, y, W - 64, meters_h(), 32, px(22, 27, 34));
	if (gpu_ok && gpu_mhz && gpu_temp_c > 0)
		snprintf(gpu_sub, sizeof(gpu_sub), "Adreno 610  ·  %d MHz  ·  %d℃",
			 gpu_mhz, gpu_temp_c);
	else if (gpu_ok && gpu_mhz)
		snprintf(gpu_sub, sizeof(gpu_sub), "Adreno 610  ·  %d MHz", gpu_mhz);
	else if (gpu_ok && gpu_temp_c > 0)
		snprintf(gpu_sub, sizeof(gpu_sub), "Adreno 610  ·  %d℃", gpu_temp_c);
	else if (gpu_ok)
		snprintf(gpu_sub, sizeof(gpu_sub), "Adreno 610");
	else
		snprintf(gpu_sub, sizeof(gpu_sub), "%s", T("未就绪", "Not ready"));
	draw_meter_head(y + 28, "GPU", gpu_sub, px(167, 123, 247));
	draw_bar(56, y + 92, gbar_w, 32, gpu_s / 100.0f, px(167, 123, 247));
	snprintf(line, sizeof(line), "%.0f%%", gpu_t);
	draw_text(&font_body, pct_x, y + 90, line, px(180, 190, 205));

	if (cpu_temp_c > 0 && batt_ok && batt_mw >= 0)
		snprintf(line, sizeof(line), "Kryo  ·  %.0f%%  ·  %d℃  ·  %.1f W",
			 cpu_t, cpu_temp_c, batt_mw / 1000.0f);
	else if (cpu_temp_c > 0)
		snprintf(line, sizeof(line), "Kryo  ·  %.0f%%  ·  %d℃", cpu_t, cpu_temp_c);
	else if (batt_ok && batt_mw >= 0)
		snprintf(line, sizeof(line), "Kryo  ·  %.0f%%  ·  %.1f W",
			 cpu_t, batt_mw / 1000.0f);
	else
		snprintf(line, sizeof(line), "Kryo  ·  %.0f%%", cpu_t);
	draw_meter_head(y + 148, "CPU", line, px(140, 150, 165));
	col_w = cols > 1 ? (W - 56 - 40 - col_gap) / cols : (W - 56 - 40);
	for (i = 0; i < cores; i++) {
		int col = i % cols;
		int row = i / cols;
		int x0 = 56 + col * (col_w + col_gap);
		int yy = y + 212 + row * rh;
		int bx, bw, pxpct;

		if (cols == 1) {
			bx = bar_x;
			bw = bar_w;
			pxpct = pct_x;
		} else {
			bx = x0 + lab_w;
			bw = col_w - lab_w - 88;
			pxpct = x0 + col_w - 80;
			if (bw < 40)
				bw = 40;
		}
		snprintf(line, sizeof(line), "CPU%d", i);
		draw_text(&font_body, x0, yy + 4, line, px(180, 190, 205));
		draw_bar(bx, yy + 10, bw, 28, cpu_core_s[i] / 100.0f, px(91, 140, 255));
		snprintf(line, sizeof(line), "%.0f%%", cpu_core_t[i]);
		draw_text(&font_body, pxpct, yy + 8, line, px(200, 208, 220));
	}

	{
		int ry = y + 212 + rows * rh + 24;
		used_gb = ram_used_kb / 1024.0f / 1024.0f;
		total_gb = ram_total_kb / 1024.0f / 1024.0f;
		if (ram_total_kb)
			snprintf(line, sizeof(line), "%.2f / %.2f GB", used_gb, total_gb);
		else
			snprintf(line, sizeof(line), "%s", T("占用", "Usage"));
		draw_meter_head(ry, T("内存", "Memory"), line, px(140, 150, 165));
		draw_bar(56, ry + 64, gbar_w, 36, ram_s / 100.0f, px(61, 220, 151));
		snprintf(line, sizeof(line), "%.0f%%", ram_t);
		draw_text(&font_body, pct_x, ry + 62, line, px(200, 208, 220));
	}
}

static void draw_lang_btn(void)
{
	int x = lang_x(), y = lang_y(), w = lang_w(), h = lang_h();
	int hw = w / 2;
	const char *zh = "中", *en = "EN";

	fill_round(x, y, w, h, 22, px(28, 34, 44));
	if (!lang_en)
		fill_round(x + 6, y + 6, hw - 8, h - 12, 16, px(91, 140, 255));
	else
		fill_round(x + hw + 2, y + 6, hw - 8, h - 12, 16, px(91, 140, 255));
	draw_text_centered(&font_title, x + 6, y + 6, hw - 8, h - 12, zh,
			   !lang_en ? px(245, 248, 252) : px(160, 170, 185));
	draw_text_centered(&font_title, x + hw + 2, y + 6, hw - 8, h - 12, en,
			   lang_en ? px(245, 248, 252) : px(160, 170, 185));
}

static void draw_bl_slider(void)
{
	int y = bl_y(), bx = bl_bar_x(), by = bl_bar_y(), bw = bl_bar_w(), bh = bl_bar_h();
	int pct = clampi(bl_pct, 1, 100);
	int fw = (int)((float)bw * pct / 100.0f);
	int kx, ky;
	char line[16];
	const char *lab = T("亮度", "Light");

	fill_round(40, y, W - 80, bl_h(), 28, px(22, 27, 34));
	draw_text_vcenter(&font_body, 56, y, bl_h(), lab, px(180, 190, 205));
	fill_round(bx, by, bw, bh, bh / 2, px(36, 42, 54));
	if (fw >= bh)
		fill_round(bx, by, fw, bh, bh / 2, px(245, 197, 66));
	kx = bx + clampi(fw, bh / 2, bw - bh / 2) - 22;
	ky = by + bh / 2 - 22;
	fill_round(kx, ky, 44, 44, 22, px(255, 255, 255));
	fill_round(kx + 10, ky + 10, 24, 24, 12, px(245, 197, 66));
	snprintf(line, sizeof(line), "%d%%", pct);
	draw_text_vcenter(&font_body, W - 160, y, bl_h(), line, px(200, 208, 220));
}

static void fill_tri_v(int cx, int y, int h, int up, uint32_t c)
{
	int i, maxh;

	if (h < 4)
		return;
	maxh = h - 1;
	for (i = 0; i < h; i++) {
		int row = up ? i : (maxh - i);
		int half = (row * (h / 2)) / maxh;

		if (half < 1)
			half = 1;
		fill_rect(cx - half, y + i, half * 2 + 1, 1, c);
	}
}

static void draw_xfer_badge(int x, int y, int up)
{
	int s = 64;
	int cx = x + s / 2;
	int cy = y + s / 2;
	uint32_t bg = up ? px(52, 34, 28) : px(22, 44, 40);
	uint32_t fg = up ? px(255, 149, 90) : px(61, 220, 151);

	fill_round(x, y, s, s, s / 2, bg);
	if (up) {
		fill_tri_v(cx, cy - 20, 22, 1, fg);
		fill_round(cx - 5, cy - 4, 10, 18, 4, fg);
	} else {
		fill_round(cx - 5, cy - 14, 10, 18, 4, fg);
		fill_tri_v(cx, cy - 2, 22, 0, fg);
	}
}

static void draw_net_cell(int x, int y, int w, int h, int up, unsigned long bps)
{
	char rate[32];
	const char *lab = up ? T("上传", "Upload") : T("下载", "Download");
	uint32_t accent = up ? px(255, 149, 90) : px(61, 220, 151);
	int by = y + (h - 64) / 2;

	fill_round(x, y, w, h, 28, px(22, 27, 34));
	fill_round(x + 8, y + 8, w - 16, h - 16, 22, px(28, 34, 44));
	draw_xfer_badge(x + 24, by, up);
	draw_text(&font_body, x + 104, y + 28, lab, px(140, 150, 165));
	fmt_bps(rate, sizeof(rate), bps);
	draw_text(&font_title, x + 104, y + 72, rate, accent);
}

static void draw_net_speed(void)
{
	int y, h, gap, cw;

	if (!net_speed_shown())
		return;
	y = net_card_y();
	h = net_card_h();
	gap = 16;
	cw = (W - 80 - gap) / 2;
	draw_net_cell(40, y, cw, h, 1, tx_bps);
	draw_net_cell(40 + cw + gap, y, cw, h, 0, rx_bps);
}

static void draw_home(void)
{
	int y = wifi_card_y();
	char line[160], band[24], up[48];
	int joining = wifi_is_joining();
	uint32_t accent = joining ? px(91, 140, 255) :
			  (wifi.connected ? px(61, 220, 151) : px(245, 197, 66));
	float glow = 0.5f + 0.5f * sinf(pulse * 2.2f);
	const char *choose = T("选择网络", "Choose network");

	fill_round(0, 0, W, H, (int)(40.0f * sheet_t + 0.5f), px(11, 14, 20));
	fill_rect(0, 0, W, 8, mix(px(11, 14, 20), accent, (int)(80 + glow * 80)));
	draw_text(&font_title, 48, 36, "Redmi Note 8", px(245, 248, 252));
	draw_lang_btn();
	fmt_uptime(up, sizeof(up), uptime_sec);
	draw_text(&font_body, 48, 96, "Ubuntu 26.04  ·  ginkgo", px(140, 150, 165));
	if (batt_ok && batt_mw >= 0 && batt_pct >= 0)
		snprintf(line, sizeof(line), "%s  %s  ·  %s  %.1f W  ·  %d%%",
			 T("已运行", "up"), up, T("功耗", "PWR"),
			 batt_mw / 1000.0f, batt_pct);
	else if (batt_ok && batt_mw >= 0)
		snprintf(line, sizeof(line), "%s  %s  ·  %s  %.1f W",
			 T("已运行", "up"), up, T("功耗", "PWR"), batt_mw / 1000.0f);
	else
		snprintf(line, sizeof(line), "%s  %s", T("已运行", "up"), up);
	draw_text(&font_body, 48, 140, line, px(140, 150, 165));
	draw_bl_slider();

	fill_round(40, y, W - 80, wifi_card_h(), 32, px(22, 27, 34));
	fill_round(40, y, 10, wifi_card_h(), 4, accent);
	draw_text(&font_title, 80, y + 28, "Wi-Fi", px(245, 248, 252));
	if (joining) {
		const char *ssid = display_ssid();

		draw_text(&font_body, W - 280, y + 36, T("连接中", "Connecting"),
			  px(91, 140, 255));
		draw_text(&font_title, 80, y + 100, T("正在连接", "Connecting"),
			  px(245, 248, 252));
		draw_text_fit(&font_title, 80, y + 168, W - 180, ssid, px(91, 140, 255));
		draw_loader(W / 2, y + 250);
		connecting_label(line, sizeof(line));
		draw_text_fit(&font_body, 80, y + 310, W - 180, line, px(140, 150, 165));
	} else if (wifi.connected) {
		char assoc_t[48];
		unsigned live = t_assoc ? (unsigned)((nsec_now() - t_assoc) / 1000000000ull) : 0;

		fmt_band(band, sizeof(band), wifi.freq);
		fmt_uptime(assoc_t, sizeof(assoc_t), live);
		draw_text(&font_body, W - 260, y + 36, T("已连接", "Connected"), accent);
		draw_text_fit(&font_title, 80, y + 88, W - 180,
			      display_ssid(), px(245, 248, 252));
		draw_signal(80, y + 152, wifi.signal);
		if (wifi.signal_dbm < 0 && band[0])
			snprintf(line, sizeof(line), "%d%%  ·  %d dBm  ·  %s",
				 wifi.signal, wifi.signal_dbm, band);
		else if (wifi.signal_dbm < 0)
			snprintf(line, sizeof(line), "%d%%  ·  %d dBm",
				 wifi.signal, wifi.signal_dbm);
		else if (band[0])
			snprintf(line, sizeof(line), "%d%%  ·  %s", wifi.signal, band);
		else
			snprintf(line, sizeof(line), "%d%%", wifi.signal);
		draw_text(&font_body, 148, y + 158, line, px(180, 190, 205));
		snprintf(line, sizeof(line), "IP    %s", wifi.ip[0] ? wifi.ip : "—");
		draw_text(&font_body, 80, y + 214, line, px(180, 190, 205));
		snprintf(line, sizeof(line), "%s  %s", T("网关", "GW"),
			 wifi.gw[0] ? wifi.gw : "—");
		draw_text(&font_body, 80, y + 262, line, px(180, 190, 205));
		snprintf(line, sizeof(line), "MAC   %s", wifi.hwaddr[0] ? wifi.hwaddr : "—");
		draw_text(&font_body, 80, y + 310, line, px(180, 190, 205));
		if (wifi.iface[0] && live)
			snprintf(line, sizeof(line), "%s  %s  ·  %s %s",
				 T("接口", "IF"), wifi.iface, T("已连", "up"), assoc_t);
		else if (wifi.iface[0])
			snprintf(line, sizeof(line), "%s  %s", T("接口", "IF"), wifi.iface);
		else
			snprintf(line, sizeof(line), "%s  —", T("接口", "IF"));
		draw_text(&font_body, 80, y + 358, line, px(180, 190, 205));
		draw_text(&font_body, 80, y + 406, T("点此选择网络", "Tap to choose a network"),
			  px(100, 110, 125));
	} else {
		draw_text(&font_body, W - 260, y + 36,
			  wifi.have_dev ? T("未连接", "Offline") : T("未就绪", "Not ready"),
			  px(245, 197, 66));
		draw_text(&font_title, 80, y + 96,
			  T("尚未连接无线网络", "Not connected to Wi-Fi"),
			  px(245, 248, 252));
		draw_btn(80, y + 168, W - 200, 68, 20,
			 mix(px(22, 27, 34), accent, 40 + (int)(glow * 40)),
			 &font_title, choose, px(20, 24, 30));
	}

	draw_net_speed();
	draw_meters();
	draw_text(&font_body, 48, H - 52,
		  T("电源键关闭屏幕 · 短按音量 + 进入 Fastboot",
		    "Power sleeps panel · Vol+ Fastboot"),
		  px(110, 120, 135));
}

static void ap_meta(const struct ap *a, char *out, int n)
{
	char band[20] = "";
	int mhz = atoi(a->freq);

	if (mhz >= 4900)
		snprintf(band, sizeof(band), " · 5 GHz");
	else if (mhz >= 2000)
		snprintf(band, sizeof(band), " · 2.4 GHz");
	if (!a->secure || !a->security[0] || !strcmp(a->security, "--"))
		snprintf(out, n, "%s%s%s", T("开放 · 无密码", "Open"), band,
			 a->active ? T(" · 已连接", " · connected") : "");
	else
		snprintf(out, n, "%s · %s%s%s", a->security, T("加密", "secured"), band,
			 a->active ? T(" · 已连接", " · connected") : "");
}

static void draw_list_sheet(int y0)
{
	int i, row_h = list_row_h(), list_top = list_top_from(y0);
	int hover = hover_ap;
	int cx = sheet_card_x(), cw = sheet_card_w(), ch = sheet_card_h();
	int sx, list_bot;

	const char *title = T("可用网络", "Networks");
	const char *back = T("返回", "Back");
	const char *ref = T("刷新", "Refresh");

	if (hover < 0 && nap > 0)
		hover = 0;

	fill_round(cx + 8, y0 + 16, cw, ch, 44, px(8, 10, 14));
	fill_round(cx, y0, cw, ch, 44, px(22, 26, 34));
	fill_round(cx + cw / 2 - 40, y0 + 16, 80, 8, 4, px(90, 98, 112));
	draw_text(&font_title, cx + 36, y0 + 44, title, px(245, 248, 252));
	draw_btn(cx + 24, y0 + 108, 220, 80, 22, px(40, 48, 60),
		 &font_title, back, px(230, 235, 245));
	draw_btn(cx + cw - 244, y0 + 108, 220, 80, 22, px(91, 140, 255),
		 &font_title, ref, px(245, 248, 252));
	if (connecting) {
		char lab[160];
		connecting_label(lab, sizeof(lab));
		draw_text_fit(&font_body, cx + 36, y0 + 208, cw - 200, lab,
			      px(245, 197, 66));
		draw_loader(cx + cw - 90, y0 + 212);
	} else if (status_line[0])
		draw_text_fit(&font_body, cx + 36, y0 + 208, cw - 72, status_line,
			      px(140, 150, 165));
	list_bot = y0 + ch - 40;
	sx = cx + cw - 148;
	for (i = 0; i < nap; i++) {
		int yy = list_top + i * row_h + list_scroll;
		int joining = connecting && connect_ssid[0] &&
			      !strcmp(aps[i].ssid, connect_ssid);
		int on = (i == hover) || joining;
		char sig[16], meta[96];
		uint32_t bg = on ? px(52, 78, 118) : px(30, 36, 46);
		int sig_w;

		if (yy < list_top - row_h || yy > list_bot)
			continue;
		fill_round(cx + 20, yy, cw - 40, row_h - 18, 24, bg);
		if (on)
			fill_round(cx + 20, yy, 10, row_h - 18, 6, px(91, 140, 255));
		draw_text_fit(&font_title, cx + 48, yy + 20, cw - 280, aps[i].ssid,
			      px(245, 248, 252));
		if (joining)
			snprintf(meta, sizeof(meta), "%s",
				 T("正在加入…", "Joining…"));
		else
			ap_meta(&aps[i], meta, sizeof(meta));
		draw_text_fit(&font_body, cx + 48, yy + 88, cw - 280, meta,
			      joining ? px(91, 140, 255) :
			      (aps[i].active ? px(61, 220, 151) : px(150, 162, 178)));
		if (joining)
			draw_loader(sx + 8, yy + 36);
		else {
			draw_signal(sx, yy + 18, aps[i].signal);
			snprintf(sig, sizeof(sig), "%d%%", aps[i].signal);
			sig_w = text_w(&font_body, sig);
			draw_text(&font_body, sx + (52 - sig_w) / 2, yy + 100, sig,
				  px(180, 190, 205));
		}
		draw_text(&font_title, cx + cw - 64, yy + 42, ">", px(120, 130, 148));
	}
	if (nap == 0)
		draw_text(&font_title, cx + 48, list_top + 40,
			  T("正在扫描…", "Scanning…"), px(180, 190, 205));
}

static void draw_kv_row(int x, int y, int w, const char *k, const char *v)
{
	draw_text(&font_body, x, y, k, px(150, 162, 178));
	draw_text_fit(&font_title, x + 220, y - 8, w - 240,
		      (v && v[0]) ? v : "—", px(245, 248, 252));
}

static void draw_detail_sheet(int y0)
{
	char sig[48], band[24];
	const char *ssid = selected_ssid[0] ? selected_ssid : picked_ssid();
	const char *sec = picked_sec();
	int cur = ap_is_current(ssid);
	int cx = sheet_card_x(), cw = sheet_card_w(), ch = sheet_card_h();
	int join_y = y0 + ch - 280;
	int forget_y = y0 + ch - 176;
	int joining = connecting && connect_ssid[0] && ssid[0] &&
		      !strcmp(connect_ssid, ssid);
	int can_join = !cur && !joining && !forgetting;
	int iy;

	sync_selected_ap();
	fmt_band(band, sizeof(band), selected_freq[0] ? selected_freq : wifi.freq);
	if (wifi.signal_dbm < 0 && cur)
		snprintf(sig, sizeof(sig), "%d%%  ·  %d dBm",
			 selected_signal ? selected_signal : wifi.signal, wifi.signal_dbm);
	else if (selected_signal)
		snprintf(sig, sizeof(sig), "%d%%", selected_signal);
	else
		sig[0] = 0;

	fill_round(cx + 8, y0 + 16, cw, ch, 44, px(8, 10, 14));
	fill_round(cx, y0, cw, ch, 44, px(22, 26, 34));
	fill_round(cx + cw / 2 - 40, y0 + 16, 80, 8, 4, px(90, 98, 112));
	draw_text(&font_title, cx + 36, y0 + 44, T("网络详情", "Network"), px(245, 248, 252));
	draw_btn(cx + 24, y0 + 108, 220, 80, 22, px(40, 48, 60),
		 &font_title, T("返回", "Back"), px(230, 235, 245));
	draw_text_fit(&font_title, cx + 36, y0 + 208, cw - 72,
		      ssid[0] ? ssid : "Wi-Fi", px(245, 248, 252));
	if (joining || forgetting) {
		char lab[160];

		if (forgetting)
			snprintf(lab, sizeof(lab), "%s", T("正在忘记…", "Forgetting…"));
		else
			connecting_label(lab, sizeof(lab));
		draw_text_fit(&font_body, cx + 36, y0 + 272, cw - 160, lab, px(245, 197, 66));
		draw_loader(cx + cw - 90, y0 + 276);
	} else if (cur)
		draw_text(&font_body, cx + 36, y0 + 276,
			  T("已连接", "Connected"), px(61, 220, 151));
	else if (connect_fail && status_line[0])
		draw_text_fit(&font_body, cx + 36, y0 + 276, cw - 72, status_line,
			      px(255, 120, 110));
	else
		draw_text(&font_body, cx + 36, y0 + 276,
			  T("未连接", "Not connected"), px(150, 162, 178));

	fill_round(cx + 24, y0 + 340, cw - 48, cur ? 360 : 260, 28, px(30, 36, 46));
	iy = y0 + 364;
	draw_kv_row(cx + 48, iy, cw - 96, T("安全性", "Security"),
		    selected_secure ? (sec[0] ? sec : T("加密", "Secured"))
				    : T("开放 · 无密码", "Open"));
	iy += 64;
	draw_kv_row(cx + 48, iy, cw - 96, T("信号", "Signal"), sig);
	iy += 64;
	draw_kv_row(cx + 48, iy, cw - 96, T("频段", "Band"), band);
	if (cur) {
		iy += 64;
		draw_kv_row(cx + 48, iy, cw - 96, "IP", wifi.ip);
		iy += 64;
		draw_kv_row(cx + 48, iy, cw - 96, T("网关", "Gateway"), wifi.gw);
		iy += 64;
		draw_kv_row(cx + 48, iy, cw - 96, "MAC", wifi.hwaddr);
	}

	draw_btn(cx + 24, join_y, cw - 48, 80, 22,
		 can_join ? px(91, 140, 255) : px(50, 58, 72),
		 &font_title,
		 joining ? T("连接中", "Joining") :
		 (cur ? T("已加入", "Joined") : T("加入此网络", "Join this network")),
		 px(245, 248, 252));
	draw_btn(cx + 24, forget_y, cw - 48, 80, 22,
		 forgetting ? px(70, 40, 42) : px(140, 48, 52),
		 &font_title,
		 forgetting ? T("忘记中", "Wait") : T("忘记此网络", "Forget this network"),
		 px(255, 210, 214));
}

static void draw_pass_sheet(int y0)
{
	char dots[PW_MAX];
	int i;
	int cx = sheet_card_x(), cw = sheet_card_w(), ch = sheet_card_h();

	fill_round(cx + 8, y0 + 16, cw, ch, 44, px(8, 10, 14));
	fill_round(cx, y0, cw, ch, 44, px(22, 26, 34));
	fill_round(cx + cw / 2 - 40, y0 + 16, 80, 8, 4, px(90, 98, 112));
	draw_text(&font_title, 48, y0 + 48, T("输入密码", "Password"), px(245, 248, 252));
	{
		const char *name = selected_ssid[0] ? selected_ssid : connect_ssid;

		if (name[0])
			draw_text_fit(&font_body, 48, y0 + 108, W - 96, name,
				      px(180, 190, 205));
	}
	fill_round(40, y0 + 164, W - 80, 88, 20, px(28, 34, 44));
	for (i = 0; i < pw_len && i < PW_MAX - 1; i++)
		dots[i] = (i == pw_len - 1) ? password[i] : '*';
	dots[pw_len] = 0;
	draw_text_vcenter(&font_title, 64, y0 + 164, 88,
			 pw_len ? dots : T("请输入 Wi-Fi 密码", "Enter Wi-Fi password"),
			 pw_len ? px(245, 248, 252) : px(100, 110, 125));
	if (connect_fail && status_line[0])
		draw_text_fit(&font_title, 48, y0 + 258, W - 96, status_line,
			      px(255, 110, 100));
	else if (connecting) {
		char lab[160];
		connecting_label(lab, sizeof(lab));
		draw_text_fit(&font_body, 48, y0 + 260, W - 360, lab, px(245, 197, 66));
		draw_loader(W - 200, y0 + 256);
	}
	draw_btn(40, y0 + 304, 200, 80, 20, px(40, 48, 60),
		 &font_body, T("取消", "Cancel"), px(230, 235, 245));
	draw_btn(W - 280, y0 + 304, 240, 80, 20,
		 connecting ? px(60, 70, 90) : px(91, 140, 255),
		 &font_title, connecting ? T("连接中", "Wait") : T("连接", "Join"),
		 px(245, 248, 252));
	draw_osk();
}

static void render(void)
{
	int y0;

	if (use_gl) {
		hud_gl_begin();
		if (sheet_t > 0.001f) {
			float s = 1.0f - 0.10f * sheet_t;

			hud_gl_set_view(s, 0.f, -28.f * sheet_t);
		}
	}
	draw_home();
	if (use_gl)
		hud_gl_set_view(1.f, 0.f, 0.f);
	if (sheet_t > 0.002f) {
		y0 = sheet_y0();
		if (use_gl)
			hud_gl_fill_rect_a(0, 0, W, H, px(0, 0, 0),
					   (int)(sheet_t * 130.0f));
		if (sheet_tgt == SHEET_PASS || (sheet == SHEET_PASS && sheet_t > 0.5f))
			draw_pass_sheet(y0);
		else if (sheet_tgt == SHEET_DETAIL ||
			 (sheet == SHEET_DETAIL && sheet_t > 0.5f))
			draw_detail_sheet(y0);
		else
			draw_list_sheet(y0);
	}
	present();
}

static int open_fbdev(void)
{
	struct fb_var_screeninfo v;
	struct fb_fix_screeninfo f;
	int i;

	for (i = 0; i < 150; i++) {
		if (!access("/dev/fb0", F_OK))
			break;
		usleep(200000);
	}
	fb_fd = open("/dev/fb0", O_RDWR);
	if (fb_fd < 0)
		return -1;
	ioctl(fb_fd, FBIOBLANK, FB_BLANK_UNBLANK);
	if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &v) ||
	    ioctl(fb_fd, FBIOGET_FSCREENINFO, &f))
		return -1;
	W = v.xres;
	H = v.yres;
	pitch = f.line_length;
	if (W < 480 || H < 480 || pitch < W * 4)
		return -1;
	back = malloc((size_t)W * (size_t)H * 4);
	if (!back)
		return -1;
	return 0;
}

static int open_fb(void)
{
	tty_fd = open("/dev/tty0", O_RDWR);
	if (tty_fd >= 0)
		ioctl(tty_fd, KDSETMODE, KD_GRAPHICS);

	if (hud_gl_init(&W, &H) == 0) {
		hud_gl_pack_font(&font_body);
		hud_gl_pack_font(&font_title);
		hud_gl_pack_font(&font_num);
		if (hud_gl_finish() == 0) {
			use_gl = 1;
			pitch = W * 4;
			logmsg("hud gles scanout");
			ensure_gpu_open();
			return 0;
		}
		logmsg("hud gles finish failed, fb0");
		hud_gl_shutdown();
	} else {
		logmsg("hud gles init failed, fb0");
		hud_gl_shutdown();
	}

	if (open_fbdev())
		return -1;
	ensure_gpu_open();
	return 0;
}

int main(void)
{
	uint64_t last = nsec_now();

	if (open_fb()) {
		logmsg("open fb0 failed");
		return 1;
	}
	load_hud_conf();
	find_backlight();
	reset_touch();
	scan_input();
	hud_gl_set_idle(drain_inputs);
	sample_cpu();
	sample_ram();
	sample_gpu();
	sample_temp();
	sample_batt();
	rfkill_unblock();
	sysfs_wifi();
	sample_link();
	sample_ip();
	sample_signal();
	sample_net();
	sample_uptime();
	wifi_radio();
	logmsg("hud running");
	for (;;) {
		uint64_t now = nsec_now();
		float dt = (float)(now - last) / 1e9f;
		uint64_t budget, t1, t2, t3;
		int interval_us;
		int nv = 0, nf = 0, wto = 0, did_met = 0, wait_us = 0;

		interval_us = t_loop0 ? (int)((now - t_loop0) / 1000ull) : 0;
		t_loop0 = now;

		/* Hitch recovery: never let one stalled frame jump the bars. */
		if (dt > 0.033f)
			dt = 0.033f;
		last = now;
		pulse += dt;
		drain_inputs();
		if (now - t_scan_input > (npfd ? 8000000000ull : 1000000000ull)) {
			scan_input();
			t_scan_input = now;
		}
		poll_job();
		maybe_confirm_join();
		{
			float k = 1.0f - expf(-dt * 3.0f);
			int i;

			cpu_s += (cpu_t - cpu_s) * k;
			gpu_s += (gpu_t - gpu_s) * k;
			ram_s += (ram_t - ram_s) * k;
			for (i = 0; i < ncpu && i < MAX_CPU; i++)
				cpu_core_s[i] += (cpu_core_t[i] - cpu_core_s[i]) * k;
		}
		{
			float tgt = (sheet_tgt != SHEET_NONE) ? 1.0f : 0.0f;
			float u, dur, e;

			if (fabsf(tgt - sheet_goal) > 0.01f) {
				sheet_from = sheet_t;
				sheet_goal = tgt;
				t_sheet = now;
			}
			dur = (tgt < 0.5f) ? 0.34f : 0.40f;
			if (t_sheet == 0)
				t_sheet = now;
			u = (float)(now - t_sheet) / (dur * 1e9f);
			if (u >= 1.0f) {
				sheet_t = tgt;
				if (tgt > 0.5f)
					sheet = sheet_tgt;
				else if (sheet_tgt == SHEET_NONE)
					sheet = SHEET_NONE;
			} else {
				if (u < 0.0f)
					u = 0.0f;
				e = ease_in_out_cubic(u);
				sheet_t = sheet_from + (tgt - sheet_from) * e;
			}
		}
		{
			float tgt = (osk_held && osk_hi >= 0) ? 1.0f : 0.0f;
			float rate = (tgt > osk_u) ? 28.0f : 18.0f;
			float k = 1.0f - expf(-dt * rate);

			if (osk_held && osk_hi >= 0)
				osk_anim = osk_hi;
			osk_u += (tgt - osk_u) * k;
			if (!osk_held && osk_u < 0.012f) {
				osk_u = 0.0f;
				osk_anim = -1;
			}
		}
		{
			int min_sc = 0, max_sc = 0;
			int vis = (sheet_card_h() - 240) / list_row_h();
			float sk = 1.0f - expf(-dt * 14.0f);

			if (vis < 1)
				vis = 1;
			if (nap > vis)
				max_sc = -(nap - vis) * list_row_h();
			list_scroll_tgt = clampi(list_scroll_tgt, max_sc, min_sc);
			list_scroll += (int)((list_scroll_tgt - list_scroll) * sk);
			if (abs(list_scroll_tgt - list_scroll) <= 1)
				list_scroll = list_scroll_tgt;
		}
		if (screen_off) {
			hud_dirty = 0;
			budget = 1000000000ull;
		} else {
			hud_dirty = 1;
			budget = 0;
		}
		t1 = nsec_now();
		if (hud_dirty && !screen_off) {
			hud_gl_set_poll(pfds, npfd);
			render();
			hud_gl_stats(&nv, &nf, &wait_us, &wto);
		}
		t2 = nsec_now();
		if (now - t_occ > 1000000000ull) {
			uint64_t ts = nsec_now();
			unsigned us;

			sample_cpu();
			sample_ram();
			sample_gpu();
			sample_temp();
			sample_net();
			sample_batt();
			sample_signal();
			sample_uptime();
			sysfs_wifi();
			sample_link();
			sample_ip();
			t_occ = now;
			us = (unsigned)((nsec_now() - ts) / 1000ull);
			if (us >= 2000) {
				char line[96];

				snprintf(line, sizeof(line), "slow occ %u us", us);
				logmsg(line);
				did_met = 1;
			}
		}
		kick_pending();
		if (job_pid < 0 && !connecting) {
			if (sheet_tgt == SHEET_LIST && now - t_scan > 8000000000ull) {
				wifi_scan();
				t_scan = now;
				did_met = 2;
			} else if (now - t_wifi > 1000000000ull) {
				wifi_info();
				t_wifi = now;
				did_met = 3;
			}
		}
		t3 = nsec_now();
		if (!screen_off && interval_us > 0) {
			static int pcpu, pdraw, pwait, pafter, pnv, pnf, pwto, pmet;

			/* interval is time since last loop start = previous frame. */
			perf_add(interval_us, pcpu, pdraw, pwait, pafter, pnv, pnf,
				 pwto, pmet);
			pcpu = (int)((t1 - now) / 1000ull);
			pdraw = (int)((t2 - t1) / 1000ull);
			pwait = wait_us;
			pafter = (int)((t3 - t2) / 1000ull);
			pnv = nv;
			pnf = nf;
			pwto = wto;
			pmet = did_met;
			perf_dump(now);
		}
		wait_deadline(now + budget);
	}
}
