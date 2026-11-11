#include "xy_wifitest.h"
#include <stdio.h>
#include <string.h>

void xy_wt_init(xy_wt_t *w) { memset(w, 0, sizeof *w); }

bool xy_wt_start(xy_wt_t *w, int64_t now_ms)
{
    if (w->state == XY_WT_RUNNING) return false;
    xy_wt_init(w);
    w->state = XY_WT_RUNNING;
    w->start_ms = now_ms;
    return true;
}

static void finish(xy_wt_t *w, bool ok)
{
    w->ok = ok;
    w->restore_pending = true;
}

void xy_wt_on_associated(xy_wt_t *w, int64_t now_ms)
{
    (void)now_ms;
    if (w->state == XY_WT_RUNNING && w->started) w->associated = true;
}

void xy_wt_on_got_ip(xy_wt_t *w, int64_t now_ms, const char *ip)
{
    (void)now_ms;
    if (w->state != XY_WT_RUNNING || !w->started || w->restore_pending) return;
    snprintf(w->ip, sizeof w->ip, "%s", ip ? ip : "");
    finish(w, true);
}

void xy_wt_on_disconnect(xy_wt_t *w, int64_t now_ms, int reason)
{
    (void)now_ms;
    if (w->state != XY_WT_RUNNING || !w->started || w->restore_pending) return;
    w->reason = reason;
    w->failures++;
    if (reason == XY_WT_REASON_NO_AP_FOUND || w->failures >= XY_WT_MAX_FAILURES) finish(w, false);
    else w->retry_pending = true;
}

xy_wt_act_t xy_wt_poll(xy_wt_t *w, int64_t now_ms)
{
    if (w->state != XY_WT_RUNNING) return XY_WT_ACT_NONE;
    if (!w->started) { w->started = true; return XY_WT_ACT_CONNECT_TEST; }
    if (!w->restore_pending && now_ms - w->start_ms >= XY_WT_TIMEOUT_MS) {
        w->timed_out = true;
        finish(w, false);
    }
    if (w->restore_pending) {
        w->restore_pending = false;
        w->state = XY_WT_DONE;
        return XY_WT_ACT_RESTORE;
    }
    if (w->retry_pending) { w->retry_pending = false; return XY_WT_ACT_RETRY; }
    return XY_WT_ACT_NONE;
}
