/* Tracks whether the displayed time can be trusted, and NTP staleness. */
#ifndef XY_TIMETRUST_H
#define XY_TIMETRUST_H
#include <stdbool.h>
#include <stdint.h>

typedef enum { XY_TIME_UNTRUSTED, XY_TIME_TRUSTED, XY_TIME_STALE } xy_trust_t;

typedef struct {
    bool have_time;          /* RTC was valid at boot, or NTP has synced */
    int64_t last_sync_s;     /* monotonic seconds of last NTP sync, -1 = never */
    uint32_t interval_s;     /* NTP sync interval */
} xy_timetrust_t;

void xy_tt_init(xy_timetrust_t *t, bool rtc_valid, uint32_t interval_s);
void xy_tt_set_interval(xy_timetrust_t *t, uint32_t interval_s);
void xy_tt_on_sync(xy_timetrust_t *t, int64_t now_mono_s);
/* Stale = trusted, but no sync for two consecutive intervals (counted from
 * boot if there has been no sync yet). */
xy_trust_t xy_tt_state(const xy_timetrust_t *t, int64_t now_mono_s);

#endif
