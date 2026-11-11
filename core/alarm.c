#include "xy_alarm.h"
#include <string.h>

static void out_reset(xy_al_out_t *o)
{
    memset(o, 0, sizeof *o);
    o->g14 = -1;
    o->persist = -1;
}

/* Compose a GPIO14 action onto what this tick has already decided, in order:
 * a set replaces it; a toggle flips a pending set, or cancels a pending
 * toggle. */
static void compose(xy_al_out_t *o, int8_t act)
{
    if (act == 2) o->g14 = o->g14 == 2 ? -1 : o->g14 >= 0 ? (int8_t)!o->g14 : 2;
    else o->g14 = act;
}

/* One-shot GPIO14 effect when an alarm (re)starts ringing. Held levels for
 * "while ringing" actions are computed separately, in held_level(). */
static void apply_one_shot(const xy_alarm_cfg_t *c, xy_al_out_t *o)
{
    switch (c->g14) {
    case XY_G14_SET_HIGH: compose(o, 1); break;
    case XY_G14_SET_LOW:  compose(o, 0); break;
    case XY_G14_TOGGLE:   compose(o, 2); break;
    default: break;
    }
}

static bool is_one_shot_action(uint8_t g14)
{
    return g14 == XY_G14_SET_HIGH || g14 == XY_G14_SET_LOW || g14 == XY_G14_TOGGLE;
}

static void start_ringing(xy_al_state_t *s, const xy_alarm_cfg_t *c, int64_t now, xy_al_out_t *o, int i)
{
    apply_one_shot(c, o);
    if (!c->sound && is_one_shot_action(c->g14)) {
        /* Nothing to hold: act once and finish. */
        s->phase = XY_AL_IDLE;
        if (c->once) o->disable[i] = true;
        return;
    }
    s->phase = XY_AL_RINGING;
    s->phase_until_s = c->timeout_min ? now + (int64_t)c->timeout_min * 60 : INT64_MAX;
}

static void end_alarm(xy_al_state_t *s, const xy_alarm_cfg_t *c, xy_al_out_t *o, int i)
{
    s->phase = XY_AL_IDLE;
    if (c->once) o->disable[i] = true;
}

/* Level wanted by ringing "while ringing" alarms (lowest number wins), or the
 * opposite level if none rings but one stopped since the last call. */
static int8_t held_level(const xy_alarms_t *a, const xy_alarm_cfg_t cfg[XY_NUM_ALARMS])
{
    for (int i = 0; i < XY_NUM_ALARMS; i++) {
        if (a->st[i].phase != XY_AL_RINGING) continue;
        if (cfg[i].g14 == XY_G14_HIGH_WHILE) return 1;
        if (cfg[i].g14 == XY_G14_LOW_WHILE) return 0;
    }
    return -1;
}

/* Held "while ringing" levels, applied after this tick's one-shots:
 * - a hold still active overrides everything (it wins while ringing);
 * - a hold that ended applies its stop level BEFORE this tick's one-shots,
 *   so a one-shot firing in the same tick is not lost. */
static void update_held(xy_alarms_t *a, const xy_alarm_cfg_t cfg[XY_NUM_ALARMS], xy_al_out_t *o,
                        int8_t one_shots)
{
    int8_t h = held_level(a, cfg);
    if (h >= 0) {
        /* Drive the hold when it starts or changes, or when a one-shot this
         * tick tried to move the pin; otherwise leave the pin alone. */
        o->g14 = (h != a->held || one_shots >= 0) ? h : -1;
        if (o->g14 >= 0) o->persist = (int8_t)!h;     /* reboot ends the alarm: save its stop level */
    } else {
        o->g14 = -1;
        if (a->held >= 0) o->g14 = (int8_t)!a->held;   /* the hold just ended */
        if (one_shots == 2) compose(o, 2);
        else if (one_shots >= 0) o->g14 = one_shots;
    }
    a->held = h;
}

void xy_alarms_init(xy_alarms_t *a)
{
    for (int i = 0; i < XY_NUM_ALARMS; i++) {
        a->st[i].phase = XY_AL_IDLE;
        a->st[i].phase_until_s = 0;
        a->st[i].last_fired_day = -1;
        a->st[i].last_fired_min = -1;
    }
    a->prev_day = -1;
    a->prev_min = -1;
    a->held = -1;
}

void xy_alarms_tick(xy_alarms_t *a, const xy_alarm_cfg_t cfg[XY_NUM_ALARMS], int64_t now_s,
                    int local_wday, int local_hour, int local_min, int32_t local_day,
                    bool trusted, xy_key_t key, xy_al_out_t *out)
{
    out_reset(out);

    /* 1. Existing alarms: keys first, then timers. */
    for (int i = 0; i < XY_NUM_ALARMS; i++) {
        xy_al_state_t *s = &a->st[i];
        const xy_alarm_cfg_t *c = &cfg[i];
        if (s->phase == XY_AL_RINGING) {
            if (c->sound && key == XY_KEY_DISMISS) {
                end_alarm(s, c, out, i);
            } else if (c->sound && key == XY_KEY_SNOOZE) {
                s->phase = XY_AL_SNOOZED;
                s->phase_until_s = now_s + (int64_t)c->snooze_min * 60;
            } else if (now_s >= s->phase_until_s) {
                end_alarm(s, c, out, i);
            }
        } else if (s->phase == XY_AL_SNOOZED) {
            if (key == XY_KEY_DISMISS) {
                end_alarm(s, c, out, i);
            } else if (now_s >= s->phase_until_s) {
                start_ringing(s, c, now_s, out, i);
            }
        }
    }

    /* 2. New triggers. Every local minute from just after the previous tick's
     * minute up to now is checked, so a forward clock step of up to
     * XY_AL_CATCHUP_MIN minutes does not skip alarms. */
    if (trusted) {
        int32_t cur = local_day * 1440 + local_hour * 60 + local_min;
        int32_t prev = a->prev_day >= 0 ? a->prev_day * 1440 + a->prev_min : cur - 1;
        int32_t from = cur - prev > 0 && cur - prev <= XY_AL_CATCHUP_MIN ? prev + 1 : cur;
        for (int32_t m = from; m <= cur; m++) {
            int32_t d = m / 1440;
            int mod = m % 1440;
            int wday = ((local_wday - (local_day - d)) % 7 + 7) % 7;
            for (int i = 0; i < XY_NUM_ALARMS; i++) {
                xy_al_state_t *s = &a->st[i];
                const xy_alarm_cfg_t *c = &cfg[i];
                if (!c->enabled || s->phase != XY_AL_IDLE) continue;
                if (!(c->days & (1u << wday))) continue;
                if (c->hour * 60 + c->minute != mod) continue;
                /* Once per day per alarm time: a clock step back over the
                 * same minute cannot re-ring it, but moving the alarm to a
                 * new time the same day makes it a new trigger. */
                if (s->last_fired_day == d && s->last_fired_min == mod) continue;
                s->last_fired_day = d;
                s->last_fired_min = (int16_t)mod;
                start_ringing(s, c, now_s, out, i);
            }
        }
        a->prev_day = local_day;
        a->prev_min = (int16_t)(local_hour * 60 + local_min);
    } else {
        a->prev_day = -1;          /* no catch-up across a period of unknown time */
    }

    update_held(a, cfg, out, out->g14);

    /* 3. Which pattern to play: the lowest-numbered ringing sound alarm. */
    for (int i = 0; i < XY_NUM_ALARMS; i++) {
        if (a->st[i].phase == XY_AL_RINGING && cfg[i].sound) {
            out->sound_alarm = (int8_t)(i + 1);
            break;
        }
    }
}

bool xy_alarms_owns_keys(const xy_alarms_t *a, const xy_alarm_cfg_t cfg[XY_NUM_ALARMS])
{
    for (int i = 0; i < XY_NUM_ALARMS; i++)
        if (cfg[i].sound && a->st[i].phase != XY_AL_IDLE) return true;
    return false;
}

void xy_alarms_cancel(xy_alarms_t *a, const xy_alarm_cfg_t cfg[XY_NUM_ALARMS], int i, xy_al_out_t *out)
{
    out_reset(out);
    if (i < 0 || i >= XY_NUM_ALARMS || a->st[i].phase == XY_AL_IDLE) return;
    end_alarm(&a->st[i], &cfg[i], out, i);
    update_held(a, cfg, out, out->g14);
}
