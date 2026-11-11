#include "minitest.h"
#include "xy_alarm.h"
#include "xy_settings.h"

/* Test clock: day number D, minute-of-day; wday = D % 7 (day 0 = Sunday). */
#define DAY 100   /* 100 % 7 == 2 -> Tuesday */
static int64_t at(int32_t day, int h, int m, int s) { return (int64_t)day * 86400 + h * 3600 + m * 60 + s; }

static xy_settings_t S;
static xy_alarms_t A;
static xy_al_out_t O;

static void setup(void)
{
    xy_settings_defaults(&S);
    xy_alarms_init(&A);
    S.alarm[0].enabled = true;          /* 07:00 every day, sound, snooze 20 */
    for (int i = 0; i < XY_NUM_ALARMS; i++)
        S.alarm[i].timeout_min = 5;     /* tests below assume 5 min unless they set it */
}

static void tick_at(int64_t t, bool trusted, xy_key_t key)
{
    int32_t day = (int32_t)(t / 86400);
    int sod = (int)(t % 86400);
    xy_alarms_tick(&A, S.alarm, t, day % 7, sod / 3600, (sod / 60) % 60, day, trusted, key, &O);
}
static void tick(int64_t t) { tick_at(t, true, XY_KEY_NONE); }

/* Simulated GPIO14 pin: apply each tick's output as the clock task does. */
static int PIN;
static void pin_apply(void) { if (O.g14 == 2) PIN = !PIN; else if (O.g14 >= 0) PIN = O.g14; }
static void ptick(int64_t t) { tick(t); pin_apply(); }
static void ptick_key(int64_t t, xy_key_t k) { tick_at(t, true, k); pin_apply(); }

/* Separate wall-clock (local) and monotonic time, for clock-step tests. */
static void tick_lm(int64_t local, int64_t mono, xy_key_t key)
{
    int32_t day = (int32_t)(local / 86400);
    int sod = (int)(local % 86400);
    xy_alarms_tick(&A, S.alarm, mono, day % 7, sod / 3600, (sod / 60) % 60, day, true, key, &O);
}

static void test_alarm_triggers_on_matching_minute(void)
{
    setup();
    tick(at(DAY, 6, 59, 59)); ASSERT_EQ_INT(A.st[0].phase, XY_AL_IDLE); ASSERT_EQ_INT(O.sound_alarm, 0);
    tick(at(DAY, 7, 0, 0));   ASSERT_EQ_INT(A.st[0].phase, XY_AL_RINGING); ASSERT_EQ_INT(O.sound_alarm, 1);
    ASSERT(xy_alarms_owns_keys(&A, S.alarm));
}

static void test_alarm_triggers_mid_minute(void)
{
    setup();
    tick(at(DAY, 7, 0, 30)); ASSERT_EQ_INT(A.st[0].phase, XY_AL_RINGING);
}

static void test_alarm_wrong_day_no_trigger(void)
{
    setup(); S.alarm[0].days = 1 << 1;   /* Monday only; DAY is Tuesday */
    tick(at(DAY, 7, 0, 0)); ASSERT_EQ_INT(A.st[0].phase, XY_AL_IDLE);
    tick(at(DAY + 6, 7, 0, 0)); ASSERT_EQ_INT(A.st[0].phase, XY_AL_RINGING); /* day 106 = Monday */
}

static void test_alarm_disabled_no_trigger(void)
{
    setup(); S.alarm[0].enabled = false;
    tick(at(DAY, 7, 0, 0)); ASSERT_EQ_INT(A.st[0].phase, XY_AL_IDLE);
}

static void test_alarm_untrusted_no_trigger(void)
{
    setup();
    tick_at(at(DAY, 7, 0, 0), false, XY_KEY_NONE); ASSERT_EQ_INT(A.st[0].phase, XY_AL_IDLE);
    tick(at(DAY, 7, 1, 0)); ASSERT_EQ_INT(A.st[0].phase, XY_AL_IDLE);   /* skipped, not late */
}

static void test_alarm_rings_once_across_time_jump(void)
{
    setup();
    tick(at(DAY, 7, 0, 0)); tick_at(at(DAY, 7, 0, 5), true, XY_KEY_DISMISS);
    ASSERT_EQ_INT(A.st[0].phase, XY_AL_IDLE);
    tick(at(DAY, 6, 59, 30));      /* clock stepped back (NTP / DST) */
    tick(at(DAY, 7, 0, 0));
    ASSERT_EQ_INT(A.st[0].phase, XY_AL_IDLE);
}

static void test_alarm_next_day_fires_again(void)
{
    setup();
    tick(at(DAY, 7, 0, 0)); tick_at(at(DAY, 7, 0, 5), true, XY_KEY_DISMISS);
    tick(at(DAY + 1, 7, 0, 0)); ASSERT_EQ_INT(A.st[0].phase, XY_AL_RINGING);
}

static void test_alarm_timeout_ends(void)
{
    setup();
    int64_t t0 = at(DAY, 7, 0, 0);
    tick(t0);
    tick(t0 + 299); ASSERT_EQ_INT(A.st[0].phase, XY_AL_RINGING);
    tick(t0 + 300); ASSERT_EQ_INT(A.st[0].phase, XY_AL_IDLE); ASSERT_EQ_INT(O.sound_alarm, 0);
    tick(t0 + 300 + 20 * 60); ASSERT_EQ_INT(A.st[0].phase, XY_AL_IDLE);  /* no snooze-like re-ring */
}

static void test_alarm_indefinite(void)
{
    setup(); S.alarm[0].timeout_min = 0;
    int64_t t0 = at(DAY, 7, 0, 0);
    tick(t0); tick(t0 + 100000); ASSERT_EQ_INT(A.st[0].phase, XY_AL_RINGING);
}

static void test_alarm_dismiss(void)
{
    setup();
    tick(at(DAY, 7, 0, 0));
    tick_at(at(DAY, 7, 0, 3), true, XY_KEY_DISMISS);
    ASSERT_EQ_INT(A.st[0].phase, XY_AL_IDLE); ASSERT_EQ_INT(O.sound_alarm, 0);
    ASSERT(!xy_alarms_owns_keys(&A, S.alarm));
    ASSERT(!O.disable[0]);
}

static void test_alarm_snooze_rerings(void)
{
    setup();
    int64_t t0 = at(DAY, 7, 0, 0);
    tick(t0);
    tick_at(t0 + 10, true, XY_KEY_SNOOZE);
    ASSERT_EQ_INT(A.st[0].phase, XY_AL_SNOOZED); ASSERT_EQ_INT(O.sound_alarm, 0);
    ASSERT(xy_alarms_owns_keys(&A, S.alarm));
    tick(t0 + 10 + 1199); ASSERT_EQ_INT(A.st[0].phase, XY_AL_SNOOZED);
    tick(t0 + 10 + 1200); ASSERT_EQ_INT(A.st[0].phase, XY_AL_RINGING); ASSERT_EQ_INT(O.sound_alarm, 1);
    tick(t0 + 10 + 1200 + 299); ASSERT_EQ_INT(A.st[0].phase, XY_AL_RINGING);  /* fresh timeout */
    tick_at(t0 + 10 + 1200 + 299, true, XY_KEY_SNOOZE);
    ASSERT_EQ_INT(A.st[0].phase, XY_AL_SNOOZED);
}

static void test_snooze_key_ignored_while_snoozed(void)
{
    setup();
    int64_t t0 = at(DAY, 7, 0, 0);
    tick(t0); tick_at(t0 + 10, true, XY_KEY_SNOOZE);
    int64_t until = A.st[0].phase_until_s;
    tick_at(t0 + 600, true, XY_KEY_SNOOZE);
    ASSERT_EQ_INT(A.st[0].phase, XY_AL_SNOOZED); ASSERT_EQ_INT(A.st[0].phase_until_s, until);
    tick_at(t0 + 601, true, XY_KEY_DISMISS);       /* dismiss ends a snoozed alarm */
    ASSERT_EQ_INT(A.st[0].phase, XY_AL_IDLE);
}

static void test_ring_once_disabled_on_timeout(void)
{
    setup(); S.alarm[0].once = true;
    int64_t t0 = at(DAY, 7, 0, 0);
    tick(t0); ASSERT(!O.disable[0]);
    tick(t0 + 300); ASSERT(O.disable[0]); ASSERT_EQ_INT(A.st[0].phase, XY_AL_IDLE);
}

static void test_ring_once_snoozed_rerings_then_disables_on_dismiss(void)
{
    setup(); S.alarm[0].once = true;
    int64_t t0 = at(DAY, 7, 0, 0);
    tick(t0); tick_at(t0 + 5, true, XY_KEY_SNOOZE); ASSERT(!O.disable[0]);
    tick(t0 + 5 + 1200); ASSERT_EQ_INT(A.st[0].phase, XY_AL_RINGING); ASSERT(!O.disable[0]);
    tick_at(t0 + 5 + 1201, true, XY_KEY_DISMISS);
    ASSERT(O.disable[0]); ASSERT_EQ_INT(A.st[0].phase, XY_AL_IDLE);
}

static void test_gpio_high_while_ringing(void)
{
    setup(); S.alarm[0].g14 = XY_G14_HIGH_WHILE;
    int64_t t0 = at(DAY, 7, 0, 0);
    tick(t0);                               ASSERT_EQ_INT(O.g14, 1);
    tick(t0 + 1);                           ASSERT_EQ_INT(O.g14, -1);
    tick_at(t0 + 2, true, XY_KEY_SNOOZE);   ASSERT_EQ_INT(O.g14, 0);
    tick(t0 + 2 + 1200);                    ASSERT_EQ_INT(O.g14, 1);
    tick_at(t0 + 2 + 1201, true, XY_KEY_DISMISS); ASSERT_EQ_INT(O.g14, 0);
}

static void test_gpio_low_while_ringing_timeout(void)
{
    setup(); S.alarm[0].g14 = XY_G14_LOW_WHILE;
    int64_t t0 = at(DAY, 7, 0, 0);
    tick(t0); ASSERT_EQ_INT(O.g14, 0);
    tick(t0 + 300); ASSERT_EQ_INT(O.g14, 1);
}

static void test_gpio_toggle_reapplies_on_rering(void)
{
    setup(); S.alarm[0].g14 = XY_G14_TOGGLE;
    int64_t t0 = at(DAY, 7, 0, 0);
    tick(t0);                               ASSERT_EQ_INT(O.g14, 2);
    tick_at(t0 + 2, true, XY_KEY_SNOOZE);   ASSERT_EQ_INT(O.g14, -1);
    tick(t0 + 2 + 1200);                    ASSERT_EQ_INT(O.g14, 2);
    tick_at(t0 + 2 + 1201, true, XY_KEY_DISMISS); ASSERT_EQ_INT(O.g14, -1);
}

static void test_gpio_set_low(void)
{
    setup(); S.alarm[0].g14 = XY_G14_SET_LOW;
    tick(at(DAY, 7, 0, 0)); ASSERT_EQ_INT(O.g14, 0); ASSERT_EQ_INT(A.st[0].phase, XY_AL_RINGING);
}

static void test_sound_off_does_not_own_keys(void)
{
    setup(); S.alarm[0].sound = false; S.alarm[0].g14 = XY_G14_HIGH_WHILE;
    int64_t t0 = at(DAY, 7, 0, 0);
    tick(t0); ASSERT_EQ_INT(A.st[0].phase, XY_AL_RINGING); ASSERT_EQ_INT(O.g14, 1);
    ASSERT_EQ_INT(O.sound_alarm, 0);
    ASSERT(!xy_alarms_owns_keys(&A, S.alarm));
    tick_at(t0 + 5, true, XY_KEY_DISMISS);  ASSERT_EQ_INT(A.st[0].phase, XY_AL_RINGING);
    tick_at(t0 + 6, true, XY_KEY_SNOOZE);   ASSERT_EQ_INT(A.st[0].phase, XY_AL_RINGING);
    tick(t0 + 300); ASSERT_EQ_INT(A.st[0].phase, XY_AL_IDLE); ASSERT_EQ_INT(O.g14, 0);
}

static void test_sound_off_set_high_acts_once(void)
{
    setup(); S.alarm[0].sound = false; S.alarm[0].g14 = XY_G14_SET_HIGH; S.alarm[0].once = true;
    tick(at(DAY, 7, 0, 0));
    ASSERT_EQ_INT(O.g14, 1); ASSERT_EQ_INT(A.st[0].phase, XY_AL_IDLE); ASSERT(O.disable[0]);
    tick(at(DAY, 7, 0, 10)); ASSERT_EQ_INT(O.g14, -1);
}

static void test_two_alarms_same_minute(void)
{
    setup(); S.alarm[0].enabled = false;
    S.alarm[1].enabled = true; S.alarm[2].enabled = true;
    S.alarm[1].g14 = XY_G14_SET_LOW; S.alarm[2].g14 = XY_G14_SET_HIGH;
    tick(at(DAY, 7, 0, 0));
    ASSERT_EQ_INT(O.sound_alarm, 2); ASSERT_EQ_INT(O.g14, 1);   /* last writer (alarm 3) wins */
    tick_at(at(DAY, 7, 0, 5), true, XY_KEY_DISMISS);
    ASSERT_EQ_INT(A.st[1].phase, XY_AL_IDLE); ASSERT_EQ_INT(A.st[2].phase, XY_AL_IDLE);
}

static void test_sound_shows_lowest_ringing(void)
{
    setup(); S.alarm[3].enabled = true; S.alarm[3].minute = 1;
    tick(at(DAY, 7, 0, 0)); tick_at(at(DAY, 7, 0, 30), true, XY_KEY_SNOOZE);
    tick(at(DAY, 7, 1, 0)); ASSERT_EQ_INT(O.sound_alarm, 4);
}

static void test_cancel_while_snoozed(void)
{
    setup(); S.alarm[0].g14 = XY_G14_HIGH_WHILE; S.alarm[0].once = true;
    int64_t t0 = at(DAY, 7, 0, 0);
    PIN = 0;
    ptick(t0); ASSERT_EQ_INT(PIN, 1);
    ptick_key(t0 + 1, XY_KEY_SNOOZE); ASSERT_EQ_INT(PIN, 0);     /* snooze releases */
    xy_alarms_cancel(&A, S.alarm, 0, &O); pin_apply();
    ASSERT_EQ_INT(A.st[0].phase, XY_AL_IDLE); ASSERT_EQ_INT(PIN, 0);
    tick(t0 + 1 + 1200); ASSERT_EQ_INT(A.st[0].phase, XY_AL_IDLE);
}

static void test_cancel_idle_noop(void)
{
    setup(); xy_alarms_cancel(&A, S.alarm, 0, &O);
    ASSERT_EQ_INT(O.g14, -1); ASSERT_EQ_INT(A.st[0].phase, XY_AL_IDLE);
}

static void test_disabled_while_ringing_keeps_running_until_cancel(void)
{
    /* cfg.enabled only gates new triggers; the web layer calls cancel. */
    setup();
    int64_t t0 = at(DAY, 7, 0, 0);
    tick(t0); S.alarm[0].enabled = false; tick(t0 + 1);
    ASSERT_EQ_INT(A.st[0].phase, XY_AL_RINGING);
}

static void test_alarm_rescheduled_same_day_rings(void)
{
    /* Owner's report: alarm rang and was dismissed, then its time was moved a
     * few minutes later the same day, and it never rang again. */
    setup();
    tick(at(DAY, 7, 0, 0)); tick_at(at(DAY, 7, 0, 5), true, XY_KEY_DISMISS);
    S.alarm[0].minute = 5;                       /* edited on the web page */
    tick(at(DAY, 7, 5, 0));
    ASSERT_EQ_INT(A.st[0].phase, XY_AL_RINGING);
}

static void test_alarm_rescheduled_back_to_same_time_no_rering(void)
{
    /* Re-saving the alarm at the time it already rang must not ring it again
     * when the clock is stepped back over that minute. */
    setup();
    tick(at(DAY, 7, 0, 0)); tick_at(at(DAY, 7, 0, 5), true, XY_KEY_DISMISS);
    tick(at(DAY, 6, 59, 30)); tick(at(DAY, 7, 0, 0));
    ASSERT_EQ_INT(A.st[0].phase, XY_AL_IDLE);
}

static void test_alarm_forward_ntp_step_does_not_skip(void)
{
    setup();
    tick(at(DAY, 6, 59, 0));
    tick(at(DAY, 7, 1, 0));          /* NTP correction jumped over 07:00 */
    ASSERT_EQ_INT(A.st[0].phase, XY_AL_RINGING);
}

static void test_alarm_dst_gap_rings_after_gap(void)
{
    setup(); S.alarm[0].hour = 2; S.alarm[0].minute = 30;
    tick(at(DAY, 1, 59, 59));
    tick(at(DAY, 3, 0, 0));          /* spring forward: 02:00-02:59 never happen */
    ASSERT_EQ_INT(A.st[0].phase, XY_AL_RINGING);
}

static void test_alarm_large_forward_jump_not_caught_up(void)
{
    setup();
    tick(at(DAY, 5, 0, 0));
    tick(at(DAY, 9, 0, 0));          /* 4 h jump: beyond the catch-up limit */
    ASSERT_EQ_INT(A.st[0].phase, XY_AL_IDLE);
}

static void test_alarm_catch_up_across_midnight_uses_that_days_weekday(void)
{
    setup(); S.alarm[0].hour = 0; S.alarm[0].minute = 0;
    S.alarm[0].days = 1u << ((DAY + 1) % 7);      /* only the next day */
    tick(at(DAY, 23, 59, 0));
    tick(at(DAY + 1, 0, 1, 0));
    ASSERT_EQ_INT(A.st[0].phase, XY_AL_RINGING);
    setup(); S.alarm[0].hour = 0; S.alarm[0].minute = 0;
    S.alarm[0].days = 1u << (DAY % 7);            /* only the earlier day: not due */
    tick(at(DAY, 23, 59, 0));
    tick(at(DAY + 1, 0, 1, 0));
    ASSERT_EQ_INT(A.st[0].phase, XY_AL_IDLE);
}

static void test_alarm_timeout_uses_monotonic_time(void)
{
    setup();
    tick_lm(at(DAY, 7, 0, 0), 1000, XY_KEY_NONE);
    ASSERT_EQ_INT(A.st[0].phase, XY_AL_RINGING);
    tick_lm(at(DAY, 6, 0, 0), 1100, XY_KEY_NONE);   /* wall clock stepped back 1 h */
    ASSERT_EQ_INT(A.st[0].phase, XY_AL_RINGING);
    tick_lm(at(DAY, 6, 3, 20), 1300, XY_KEY_NONE);  /* 300 s of real time: timeout */
    ASSERT_EQ_INT(A.st[0].phase, XY_AL_IDLE);
    setup();
    tick_lm(at(DAY, 7, 0, 0), 1000, XY_KEY_NONE);
    tick_lm(at(DAY, 9, 0, 0), 1010, XY_KEY_NONE);   /* wall clock stepped forward 2 h */
    ASSERT_EQ_INT(A.st[0].phase, XY_AL_RINGING);
}

static void test_alarm_snooze_uses_monotonic_time(void)
{
    setup();
    tick_lm(at(DAY, 7, 0, 0), 1000, XY_KEY_NONE);
    tick_lm(at(DAY, 7, 0, 5), 1005, XY_KEY_SNOOZE);
    tick_lm(at(DAY, 9, 0, 0), 1010, XY_KEY_NONE);   /* forward step must not end the snooze */
    ASSERT_EQ_INT(A.st[0].phase, XY_AL_SNOOZED);
    tick_lm(at(DAY, 9, 20, 0), 1005 + 1200, XY_KEY_NONE);
    ASSERT_EQ_INT(A.st[0].phase, XY_AL_RINGING);
}

static void test_overlapping_held_gpio_stays_asserted(void)
{
    setup();
    S.alarm[0].g14 = XY_G14_HIGH_WHILE; S.alarm[0].timeout_min = 10;     /* 07:00-07:10 */
    S.alarm[1].enabled = true; S.alarm[1].hour = 7; S.alarm[1].minute = 2;
    S.alarm[1].g14 = XY_G14_HIGH_WHILE; S.alarm[1].timeout_min = 1;      /* 07:02-07:03 */
    PIN = 0;
    ptick(at(DAY, 7, 0, 0));  ASSERT_EQ_INT(PIN, 1);
    ptick(at(DAY, 7, 2, 0));  ASSERT_EQ_INT(PIN, 1);
    ptick(at(DAY, 7, 3, 0));  ASSERT_EQ_INT(PIN, 1);    /* alarm 2 ended, alarm 1 still holds */
    ASSERT_EQ_INT(A.st[0].phase, XY_AL_RINGING); ASSERT_EQ_INT(A.st[1].phase, XY_AL_IDLE);
    ptick(at(DAY, 7, 10, 0)); ASSERT_EQ_INT(PIN, 0);    /* last holder ended */
}

static void test_held_overrides_one_shot_while_ringing(void)
{
    /* A one-shot "set low" fires while a "high while ringing" alarm holds the
     * pin: the hold wins until it ends; then the stop level applies. */
    setup(); PIN = 0;
    S.alarm[0].g14 = XY_G14_HIGH_WHILE; S.alarm[0].timeout_min = 10;
    S.alarm[1].enabled = true; S.alarm[1].minute = 3; S.alarm[1].sound = false;
    S.alarm[1].g14 = XY_G14_SET_LOW;
    ptick(at(DAY, 7, 0, 0)); ASSERT_EQ_INT(PIN, 1);
    ptick(at(DAY, 7, 3, 0)); ASSERT_EQ_INT(PIN, 1);
    ptick(at(DAY, 7, 10, 0)); ASSERT_EQ_INT(PIN, 0);
}

static void test_overlapping_held_snooze_keeps_other_holder(void)
{
    /* Both ring together; snoozing applies to both sound alarms, so both
     * release; when only one re-rings it holds again. */
    setup();
    S.alarm[0].g14 = XY_G14_HIGH_WHILE; S.alarm[0].sound = false; S.alarm[0].timeout_min = 30;
    S.alarm[1].enabled = true; S.alarm[1].hour = 7; S.alarm[1].minute = 1;
    S.alarm[1].g14 = XY_G14_HIGH_WHILE;                                   /* sound on */
    PIN = 0;
    ptick(at(DAY, 7, 0, 0));  ASSERT_EQ_INT(PIN, 1);
    ptick(at(DAY, 7, 1, 0));
    ptick_key(at(DAY, 7, 1, 5), XY_KEY_SNOOZE);       /* snoozes alarm 2 only (sound) */
    ASSERT_EQ_INT(PIN, 1);                             /* alarm 1 still holds high */
    ASSERT_EQ_INT(A.st[0].phase, XY_AL_RINGING); ASSERT_EQ_INT(A.st[1].phase, XY_AL_SNOOZED);
}

static void test_one_shot_survives_hold_ending_same_tick(void)
{
    /* Alarm 1 holds high 06:55-07:00; alarm 2 "set high" fires at 07:00, the
     * same tick the hold ends. The pin must end high. */
    setup(); PIN = 0;
    S.alarm[0].hour = 6; S.alarm[0].minute = 55; S.alarm[0].g14 = XY_G14_HIGH_WHILE; S.alarm[0].timeout_min = 5;
    S.alarm[1].enabled = true; S.alarm[1].sound = false; S.alarm[1].g14 = XY_G14_SET_HIGH;
    ptick(at(DAY, 6, 55, 0)); ASSERT_EQ_INT(PIN, 1);
    ptick(at(DAY, 7, 0, 0));  ASSERT_EQ_INT(PIN, 1);
}

static void test_several_one_shots_in_one_catch_up(void)
{
    setup(); PIN = 0; S.alarm[0].enabled = false;
    S.alarm[1].enabled = true; S.alarm[1].sound = false; S.alarm[1].g14 = XY_G14_SET_HIGH;   /* 07:00 */
    S.alarm[2].enabled = true; S.alarm[2].sound = false; S.alarm[2].g14 = XY_G14_TOGGLE; S.alarm[2].minute = 1;
    ptick(at(DAY, 6, 59, 0));
    ptick(at(DAY, 7, 2, 0));          /* catches up 07:00 (high) then 07:01 (toggle) */
    ASSERT_EQ_INT(PIN, 0);
    /* two toggles cancel out */
    setup(); PIN = 1; S.alarm[0].enabled = false;
    S.alarm[1].enabled = true; S.alarm[1].sound = false; S.alarm[1].g14 = XY_G14_TOGGLE;
    S.alarm[2].enabled = true; S.alarm[2].sound = false; S.alarm[2].g14 = XY_G14_TOGGLE; S.alarm[2].minute = 1;
    ptick(at(DAY, 6, 59, 0)); ptick(at(DAY, 7, 2, 0));
    ASSERT_EQ_INT(PIN, 1);
}

static void test_persisted_level_is_stop_level_while_held(void)
{
    /* While a "high while ringing" alarm holds the pin, the level to restore
     * after a reboot is the stop level (the alarm does not survive reboot). */
    setup(); PIN = 0; S.alarm[0].g14 = XY_G14_HIGH_WHILE;
    ptick(at(DAY, 7, 0, 0));
    ASSERT_EQ_INT(PIN, 1); ASSERT_EQ_INT(O.persist, 0);
    S.alarm[0].g14 = XY_G14_LOW_WHILE; setup(); PIN = 1; S.alarm[0].g14 = XY_G14_LOW_WHILE;
    ptick(at(DAY, 7, 0, 0));
    ASSERT_EQ_INT(PIN, 0); ASSERT_EQ_INT(O.persist, 1);
    setup(); PIN = 0; S.alarm[0].sound = false; S.alarm[0].g14 = XY_G14_SET_HIGH;
    ptick(at(DAY, 7, 0, 0));
    ASSERT_EQ_INT(PIN, 1); ASSERT_EQ_INT(O.persist, -1);      /* one-shot: save what was applied */
}

int main(void)
{
    RUN(test_alarm_triggers_on_matching_minute);
    RUN(test_alarm_triggers_mid_minute);
    RUN(test_alarm_wrong_day_no_trigger);
    RUN(test_alarm_disabled_no_trigger);
    RUN(test_alarm_untrusted_no_trigger);
    RUN(test_alarm_rings_once_across_time_jump);
    RUN(test_alarm_next_day_fires_again);
    RUN(test_alarm_timeout_ends);
    RUN(test_alarm_indefinite);
    RUN(test_alarm_dismiss);
    RUN(test_alarm_snooze_rerings);
    RUN(test_snooze_key_ignored_while_snoozed);
    RUN(test_ring_once_disabled_on_timeout);
    RUN(test_ring_once_snoozed_rerings_then_disables_on_dismiss);
    RUN(test_gpio_high_while_ringing);
    RUN(test_gpio_low_while_ringing_timeout);
    RUN(test_gpio_toggle_reapplies_on_rering);
    RUN(test_gpio_set_low);
    RUN(test_sound_off_does_not_own_keys);
    RUN(test_sound_off_set_high_acts_once);
    RUN(test_two_alarms_same_minute);
    RUN(test_sound_shows_lowest_ringing);
    RUN(test_cancel_while_snoozed);
    RUN(test_cancel_idle_noop);
    RUN(test_disabled_while_ringing_keeps_running_until_cancel);
    RUN(test_alarm_rescheduled_same_day_rings);
    RUN(test_alarm_rescheduled_back_to_same_time_no_rering);
    RUN(test_alarm_forward_ntp_step_does_not_skip);
    RUN(test_alarm_dst_gap_rings_after_gap);
    RUN(test_alarm_large_forward_jump_not_caught_up);
    RUN(test_alarm_catch_up_across_midnight_uses_that_days_weekday);
    RUN(test_alarm_timeout_uses_monotonic_time);
    RUN(test_alarm_snooze_uses_monotonic_time);
    RUN(test_overlapping_held_gpio_stays_asserted);
    RUN(test_held_overrides_one_shot_while_ringing);
    RUN(test_overlapping_held_snooze_keeps_other_holder);
    RUN(test_one_shot_survives_hold_ending_same_tick);
    RUN(test_several_one_shots_in_one_catch_up);
    RUN(test_persisted_level_is_stop_level_while_held);
    return TEST_SUMMARY("test_alarm");
}
