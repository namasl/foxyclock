#include "xy_timetrust.h"

void xy_tt_init(xy_timetrust_t *t, bool rtc_valid, uint32_t interval_s)
{
    t->have_time = rtc_valid;
    t->last_sync_s = -1;
    t->interval_s = interval_s;
}

void xy_tt_set_interval(xy_timetrust_t *t, uint32_t interval_s) { t->interval_s = interval_s; }

void xy_tt_on_sync(xy_timetrust_t *t, int64_t now_mono_s)
{
    t->have_time = true;
    t->last_sync_s = now_mono_s;
}

xy_trust_t xy_tt_state(const xy_timetrust_t *t, int64_t now_mono_s)
{
    if (!t->have_time) return XY_TIME_UNTRUSTED;
    int64_t since = t->last_sync_s < 0 ? now_mono_s : now_mono_s - t->last_sync_s;
    return since >= 2 * (int64_t)t->interval_s ? XY_TIME_STALE : XY_TIME_TRUSTED;
}
