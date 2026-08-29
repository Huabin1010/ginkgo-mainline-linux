/* SPDX-License-Identifier: GPL-2.0-only */
/* Pure Wi-Fi HUD flow. No I/O — included by ginkgo-status.c and unit tests. */
#ifndef GEMINI_WIFI_FLOW_H
#define GEMINI_WIFI_FLOW_H

#include <string.h>

enum gemini_wifi_sheet {
	GW_SHEET_HOME = 0,
	GW_SHEET_LIST,
	GW_SHEET_DETAIL,
	GW_SHEET_PASS
};

enum gw_connect_act {
	GW_CONN_SUCCESS = 0,
	GW_CONN_OPEN_PASS,
	GW_CONN_WRONG_PASS,
	GW_CONN_RETRY_PSK,
	GW_CONN_WAIT_PENDING,
	GW_CONN_FAIL
};

struct gw_connect_in {
	int rc;
	const char *out;
	int connected;
	const char *live_ssid;
	const char *connect_ssid;
	int left_home;
	int want_connect;
	const char *pend_ssid;
	int pend_has_pass;
	int used_pass;
	int have_session_psk;
};

static inline int gw_ssid_eq(const char *a, const char *b)
{
	if (!a || !b || !a[0] || !b[0])
		return 0;
	return strcmp(a, b) == 0;
}

static inline int gw_already_on(int connected, const char *live, const char *want)
{
	return connected && gw_ssid_eq(live, want);
}

/* Home card SSID: only show the target name while we are still joining.
 * A leftover connect_ssid used to look like we were already on that AP. */
static inline const char *gw_display_ssid(int joining, const char *want,
					  const char *live)
{
	if (joining && want && want[0])
		return want;
	if (live && live[0])
		return live;
	return "";
}

/* The radio fell back to a different saved AP (autoconnect). */
static inline int gw_join_fell_back(int connected, const char *live,
				    const char *want)
{
	return connected && live && live[0] && want && want[0] &&
	       !gw_ssid_eq(live, want);
}

/* Confirm-join: still being on the previous AP is expected, not a failure. */
static inline int gw_join_confirm_ok(int connected, const char *live,
				     const char *want)
{
	return gw_already_on(connected, live, want);
}

static inline int gw_join_confirm_fail(int timed_out, int connected,
				       const char *live, const char *want)
{
	if (!timed_out)
		return 0;
	return !gw_already_on(connected, live, want);
}

/* After forget, leftover NM "connected" / IP / scan IN-USE must not
 * keep showing 已连接 for the forgotten SSID. */
static inline int gw_forget_hold_connected(const char *forgotten, int nm_up,
					   const char *live)
{
	if (!forgotten || !forgotten[0])
		return 0;
	if (!nm_up)
		return 0;
	if (!live || !live[0] || gw_ssid_eq(forgotten, live))
		return 1;
	return 0;
}

static inline int gw_allow_join(int connecting, int forgetting, int already_on)
{
	return !connecting && !forgetting && !already_on;
}

/* Forget must still start after a queued scan: forgetting=1 with no job
 * is a stuck spinner. */
static inline int gw_forget_can_start(int forgetting, int job_running,
				      int job_is_forget)
{
	if (job_running && job_is_forget)
		return 0;
	if (!job_running)
		return 1;
	(void)forgetting;
	return !job_is_forget;
}

/* A finished join for SSID X must not run a queued empty-password probe for X
 * (that probe is what pops the password sheet again). */
static inline int gw_drop_pending(int ok, const char *done_ssid,
				  const char *pend_ssid)
{
	return ok && gw_ssid_eq(done_ssid, pend_ssid);
}

static inline int gw_job_ok(int rc, const char *out)
{
	if (rc == 0)
		return 1;
	return out && strstr(out, "已连接") != NULL;
}

static inline int gw_job_need_pw(int rc, const char *out)
{
	if (gw_job_ok(rc, out))
		return 0;
	if (rc == 3)
		return 0;
	if (rc == 2)
		return 1;
	return out && strstr(out, "NEED_PASSWORD") != NULL;
}

static inline int gw_job_wrong_pw(int rc, const char *out)
{
	if (gw_job_ok(rc, out))
		return 0;
	if (rc == 3)
		return 1;
	if (!out)
		return 0;
	if (strstr(out, "密码不对") || strstr(out, "WRONG_PASSWORD") ||
	    strstr(out, "psk mismatch"))
		return 1;
	return 0;
}

enum gw_fail_kind {
	GW_FAIL_UNKNOWN = 0,
	GW_FAIL_WRONG_PW,
	GW_FAIL_NEED_PW,
	GW_FAIL_NOT_FOUND,
	GW_FAIL_TIMEOUT,
	GW_FAIL_NO_DEVICE,
	GW_FAIL_DHCP
};

static inline enum gw_fail_kind gw_classify_fail(const char *out)
{
	if (!out || !out[0])
		return GW_FAIL_UNKNOWN;
	if (strstr(out, "已连接"))
		return GW_FAIL_UNKNOWN;
	if (strstr(out, "密码不对") || strstr(out, "WRONG_PASSWORD") ||
	    strstr(out, "psk mismatch") || strstr(out, "4-way handshake") ||
	    strstr(out, "handshake timeout") || strstr(out, "bad password"))
		return GW_FAIL_WRONG_PW;
	if (strstr(out, "NEED_PASSWORD"))
		return GW_FAIL_NEED_PW;
	if (strstr(out, "secrets were required") ||
	    strstr(out, "Secrets were required"))
		return GW_FAIL_WRONG_PW;
	if (strstr(out, "could not be found") ||
	    strstr(out, "The Wi-Fi network could not be found") ||
	    strstr(out, "找不到"))
		return GW_FAIL_NOT_FOUND;
	if (strstr(out, "Timeout expired") || strstr(out, "timeout expired") ||
	    strstr(out, "连接超时"))
		return GW_FAIL_TIMEOUT;
	if (strstr(out, "No suitable device") ||
	    strstr(out, "mismatching interface") || strstr(out, "网卡不匹配"))
		return GW_FAIL_NO_DEVICE;
	if (strstr(out, "IP configuration") || strstr(out, "dhcp") ||
	    strstr(out, "没拿到地址"))
		return GW_FAIL_DHCP;
	return GW_FAIL_UNKNOWN;
}

/* Open the password sheet only when we really still need secrets. */
static inline int gw_open_pass(int need_pw, int already_on, int left_home,
			       int pending_has_pass)
{
	if (!need_pw || already_on || left_home)
		return 0;
	if (pending_has_pass)
		return 0;
	return 1;
}

/* psk-flags: 0 saved in file, 1 agent, 2 not saved. Agent/not-saved cannot
 * join without a password because the HUD has no secret agent. */
static inline int gw_saved_profile_usable(int flags, int psk_visible)
{
	if (flags == 1 || flags == 2)
		return 0;
	(void)psk_visible;
	return 1;
}

/* Do not replace a queued PSK with an empty probe for the same SSID. */
static inline int gw_keep_pending_psk(int queuing_empty, int want_connect,
				      const char *pend_ssid,
				      const char *pend_pass, const char *ssid)
{
	return queuing_empty && want_connect && pend_pass && pend_pass[0] &&
	       gw_ssid_eq(pend_ssid, ssid);
}

/* Password sheet belongs to the in-flight SSID, not a stale list selection. */
static inline const char *gw_pass_ssid(const char *connect_ssid,
				       const char *selected_ssid)
{
	if (connect_ssid && connect_ssid[0])
		return connect_ssid;
	if (selected_ssid && selected_ssid[0])
		return selected_ssid;
	return "";
}

/* Join with the in-memory PSK instead of an empty probe when we already
 * collected it this session (avoids a second password sheet). */
static inline int gw_join_use_session_psk(const char *ssid,
					  const char *known_ssid,
					  const char *known_psk)
{
	return gw_ssid_eq(ssid, known_ssid) && known_psk && known_psk[0];
}

static inline enum gw_connect_act gw_on_connect_job(const struct gw_connect_in *in)
{
	int already;
	int need_pw;
	int wrong;
	int pend_pw;

	if (!in)
		return GW_CONN_FAIL;
	already = gw_already_on(in->connected, in->live_ssid, in->connect_ssid);
	need_pw = gw_job_need_pw(in->rc, in->out);
	wrong = gw_job_wrong_pw(in->rc, in->out);
	pend_pw = in->want_connect && in->pend_has_pass &&
		  gw_ssid_eq(in->pend_ssid, in->connect_ssid);

	if (gw_job_ok(in->rc, in->out) || ((need_pw || wrong) && already))
		return GW_CONN_SUCCESS;
	if ((need_pw || wrong) && pend_pw)
		return GW_CONN_WAIT_PENDING;
	if (need_pw && !wrong && !in->used_pass && in->have_session_psk)
		return GW_CONN_RETRY_PSK;
	if (wrong || (in->used_pass && need_pw))
		return GW_CONN_WRONG_PASS;
	if (gw_open_pass(need_pw, already, in->left_home, 0))
		return GW_CONN_OPEN_PASS;
	return GW_CONN_FAIL;
}

#endif
