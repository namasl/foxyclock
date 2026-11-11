/* Ticks and chimes (no alarm logic): decides which sound to start when a
 * new wall-clock second begins. */
#ifndef XY_SOUND_H
#define XY_SOUND_H
#include <stdbool.h>
#include <stdint.h>
#include "xy_settings.h"

typedef enum { XY_SND_NONE, XY_SND_TICK, XY_SND_MINUTE, XY_SND_HALF, XY_SND_HOUR } xy_snd_kind_t;
typedef struct { xy_snd_kind_t kind; int count; /* hour strikes, 1..12 */ } xy_snd_ev_t;
typedef struct { int64_t last_s; } xy_sound_t;

void xy_sound_init(xy_sound_t *s);
/* Call every loop. Returns an event only on the first call within each new
 * second. Chimes need trusted time (a wrong hour is worse than none); ticks
 * do not. Everything is silent while an alarm is sounding, and during quiet
 * hours (which, with untrusted time, are assumed to be in effect). */
xy_snd_ev_t xy_sound_tick(xy_sound_t *s, const xy_settings_t *cfg, int64_t now_s,
                          int local_hour, int local_min, int local_sec,
                          bool trusted, bool alarm_sounding);

/* True if local time m (minutes after midnight) is inside the quiet window. */
bool xy_sound_quiet_now(const xy_settings_t *cfg, int m);

#endif
