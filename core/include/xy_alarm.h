/* Alarm state machine (design §8). Pure logic: the caller supplies the time
 * and key events, and applies the outputs (beeper pattern, GPIO14, saving
 * "ring once" alarms as disabled). */
#ifndef XY_ALARM_H
#define XY_ALARM_H
#include <stdbool.h>
#include <stdint.h>
#include "xy_settings.h"

typedef enum { XY_AL_IDLE, XY_AL_RINGING, XY_AL_SNOOZED } xy_al_phase_t;

typedef struct {
    xy_al_phase_t phase;
    int64_t phase_until_s;   /* monotonic seconds. RINGING: timeout (INT64_MAX = indefinite); SNOOZED: re-ring time */
    int32_t last_fired_day;  /* local day number of the last trigger, -1 = never */
    int16_t last_fired_min;  /* local minute of day (h*60+m) of that trigger */
} xy_al_state_t;

typedef struct {
    xy_al_state_t st[XY_NUM_ALARMS];
    int32_t prev_day;        /* local day/minute seen on the previous trusted tick, -1 = none */
    int16_t prev_min;
    int8_t held;             /* GPIO14 level held by "while ringing" alarms: -1 none, 0, 1 */
} xy_alarms_t;

/* A forward clock step (NTP correction, DST start) of up to this many minutes
 * still triggers the alarms whose minute was jumped over. */
#define XY_AL_CATCHUP_MIN 120

typedef struct {
    int8_t g14;                     /* -1 no change, 0 low, 1 high, 2 toggle */
    int8_t persist;                 /* level to save for restore after a reboot: -1 = as applied.
                                       While a "while ringing" hold is active this is its stop
                                       level, since the alarm itself does not survive a reboot. */
    bool disable[XY_NUM_ALARMS];    /* ring-once alarms that finished: clear enabled and save */
    int8_t sound_alarm;             /* 1-based alarm whose pattern plays now, 0 = silence */
} xy_al_out_t;

typedef enum { XY_KEY_NONE, XY_KEY_DISMISS, XY_KEY_SNOOZE } xy_key_t;

void xy_alarms_init(xy_alarms_t *a);

/* Call every loop iteration. now_s: MONOTONIC seconds (timeouts and snoozes
 * are elapsed durations, unaffected by clock steps). local_*: local wall time
 * broken down. local_day: local calendar days since 1970-01-01 (consecutive
 * days must differ by exactly 1, for catch-up across midnight). key: button event routed to alarms.
 *
 * GPIO14: "while ringing" actions are combined across all active alarms; the
 * pin stays in the ringing state until the last such alarm stops. Levels
 * requested by alarms that ring at the same time but disagree: the lowest-
 * numbered alarm wins. One-shot actions (set high/low, toggle) are applied
 * when they fire and are then overridden by any held level. */
void xy_alarms_tick(xy_alarms_t *a, const xy_alarm_cfg_t cfg[XY_NUM_ALARMS], int64_t now_s,
                    int local_wday, int local_hour, int local_min, int32_t local_day,
                    bool trusted, xy_key_t key, xy_al_out_t *out);

/* True while any sound alarm is ringing or snoozed: UP/DOWN/SET then belong
 * to the alarm instead of their normal functions. */
bool xy_alarms_owns_keys(const xy_alarms_t *a, const xy_alarm_cfg_t cfg[XY_NUM_ALARMS]);

/* Alarm i was switched off on the web page: end it as if dismissed. */
void xy_alarms_cancel(xy_alarms_t *a, const xy_alarm_cfg_t cfg[XY_NUM_ALARMS], int i, xy_al_out_t *out);

#endif
