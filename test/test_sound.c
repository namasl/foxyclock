#include <stddef.h>
#include "minitest.h"
#include "xy_sound.h"
#include "xy_settings.h"

static xy_settings_t S;
static xy_sound_t SND;

static void setup(void) { xy_settings_defaults(&S); xy_sound_init(&SND); }

/* epoch chosen so that h:m:s below are consistent within a day */
static xy_snd_ev_t at(int h, int m, int s, bool trusted, bool alarm)
{
    int64_t t = 1700000000LL - (1700000000LL % 86400) + h * 3600 + m * 60 + s;
    return xy_sound_tick(&SND, &S, t, h, m, s, trusted, alarm);
}

static void test_sound_defaults_off(void)
{
    setup();
    ASSERT(!S.tick); ASSERT(!S.tick_minute); ASSERT(!S.chime);
    ASSERT_EQ_INT(at(10, 0, 0, true, false).kind, XY_SND_NONE);
    ASSERT_EQ_INT(at(10, 0, 1, true, false).kind, XY_SND_NONE);
}

static void test_tick_once_per_second(void)
{
    setup(); S.tick = true;
    ASSERT_EQ_INT(at(10, 5, 7, true, false).kind, XY_SND_TICK);
    ASSERT_EQ_INT(at(10, 5, 7, true, false).kind, XY_SND_NONE);   /* same second again */
    ASSERT_EQ_INT(at(10, 5, 8, true, false).kind, XY_SND_TICK);
}

static void test_tick_runs_untrusted(void)
{
    setup(); S.tick = true;
    ASSERT_EQ_INT(at(10, 5, 7, false, false).kind, XY_SND_TICK);
}

static void test_minute_beep_needs_tick(void)
{
    setup(); S.tick_minute = true;
    ASSERT_EQ_INT(at(10, 6, 0, true, false).kind, XY_SND_NONE);
    S.tick = true;
    ASSERT_EQ_INT(at(10, 7, 0, true, false).kind, XY_SND_MINUTE);
    ASSERT_EQ_INT(at(10, 7, 1, true, false).kind, XY_SND_TICK);
}

static void test_tick_without_minute_ticks_at_zero(void)
{
    setup(); S.tick = true;
    ASSERT_EQ_INT(at(10, 7, 0, true, false).kind, XY_SND_TICK);
}

static void test_chime_hour_counts_12h(void)
{
    setup(); S.chime = true;
    xy_snd_ev_t e = at(15, 0, 0, true, false);
    ASSERT_EQ_INT(e.kind, XY_SND_HOUR); ASSERT_EQ_INT(e.count, 3);
    e = at(0, 0, 0, true, false);  ASSERT_EQ_INT(e.count, 12);   /* midnight */
    e = at(12, 0, 0, true, false); ASSERT_EQ_INT(e.count, 12);   /* noon */
    e = at(1, 0, 0, true, false);  ASSERT_EQ_INT(e.count, 1);
    S.h24 = true;
    e = at(23, 0, 0, true, false); ASSERT_EQ_INT(e.count, 11);   /* still 12-hour strikes */
}

static void test_chime_half_hour(void)
{
    setup(); S.chime = true;
    ASSERT_EQ_INT(at(9, 30, 0, true, false).kind, XY_SND_HALF);
    ASSERT_EQ_INT(at(9, 31, 0, true, false).kind, XY_SND_NONE);
    ASSERT_EQ_INT(at(9, 15, 0, true, false).kind, XY_SND_NONE);
}

static void test_chime_needs_trusted_time(void)
{
    setup(); S.chime = true;
    ASSERT_EQ_INT(at(9, 0, 0, false, false).kind, XY_SND_NONE);
}

static void test_chime_takes_precedence(void)
{
    setup(); S.chime = true; S.tick = true; S.tick_minute = true;
    ASSERT_EQ_INT(at(9, 0, 0, true, false).kind, XY_SND_HOUR);
    ASSERT_EQ_INT(at(9, 30, 0, true, false).kind, XY_SND_HALF);
    ASSERT_EQ_INT(at(9, 31, 0, true, false).kind, XY_SND_MINUTE);
    ASSERT_EQ_INT(at(9, 0, 0, false, false).kind, XY_SND_MINUTE);   /* untrusted: no chime, minute beep instead */
}

static void test_silent_while_alarm_sounds(void)
{
    setup(); S.chime = true; S.tick = true; S.tick_minute = true;
    ASSERT_EQ_INT(at(9, 0, 0, true, true).kind, XY_SND_NONE);
    ASSERT_EQ_INT(at(9, 0, 1, true, true).kind, XY_SND_NONE);
    ASSERT_EQ_INT(at(9, 0, 2, true, false).kind, XY_SND_TICK);
}

static void test_settings_json_has_audio(void)
{
    xy_settings_t s; xy_settings_defaults(&s); strcpy(s.tz, "UTC0");
    xy_settings_t out;
    ASSERT(xy_settings_from_json("{\"tick\":true,\"tick_minute\":true,\"chime\":true}", &s, &out) == NULL);
    ASSERT(out.tick); ASSERT(out.tick_minute); ASSERT(out.chime);
    ASSERT_CONTAINS(xy_settings_from_json("{\"chime\":1}", &s, &out), "chime");
}

static void test_blob_v1_migrates(void)
{
    /* A version-1 blob is the current struct truncated before the audio
     * fields (padded to the struct's alignment), with version = 1. */
    xy_settings_t v; xy_settings_defaults(&v);
    strcpy(v.ssid, "home"); strcpy(v.wifi_pass, "password1"); strcpy(v.tz, "UTC0");
    v.brightness = 6; v.alarm[3].enabled = true; strcpy(v.ap_pass, "apsecret9");
    v.version = 1;
    v.tick = true;                       /* garbage in what used to be padding */
    size_t v1 = XY_SETTINGS_V1_SIZE;
    xy_settings_t out;
    ASSERT(xy_settings_from_blob(&v, v1, &out));
    ASSERT_EQ_INT(out.version, XY_SETTINGS_VERSION);
    ASSERT_EQ_STR(out.ssid, "home"); ASSERT_EQ_STR(out.wifi_pass, "password1");
    ASSERT_EQ_STR(out.ap_pass, "apsecret9"); ASSERT_EQ_INT(out.brightness, 6);
    ASSERT(out.alarm[3].enabled);
    ASSERT(!out.tick); ASSERT(!out.tick_minute); ASSERT(!out.chime);
}

static void test_blob_current_and_bad(void)
{
    xy_settings_t v; xy_settings_defaults(&v); strcpy(v.tz, "UTC0"); v.chime = true;
    xy_settings_t out;
    ASSERT(xy_settings_from_blob(&v, sizeof v, &out)); ASSERT(out.chime);
    ASSERT(!xy_settings_from_blob(&v, sizeof v - 4, &out));            /* wrong size for v2 */
    v.version = 1;
    ASSERT(!xy_settings_from_blob(&v, sizeof v, &out));                /* v1 must be v1 size */
    v.version = 99;
    ASSERT(!xy_settings_from_blob(&v, sizeof v, &out));
}

static void test_quiet_hours_silence_ticks_and_chimes(void)
{
    setup(); S.tick = true; S.tick_minute = true; S.chime = true;
    S.quiet = true; S.quiet_start_min = 22 * 60; S.quiet_end_min = 7 * 60;   /* 22:00-07:00 */
    ASSERT_EQ_INT(at(23, 0, 0, true, false).kind, XY_SND_NONE);
    ASSERT_EQ_INT(at(23, 0, 1, true, false).kind, XY_SND_NONE);
    ASSERT_EQ_INT(at(3, 30, 0, true, false).kind, XY_SND_NONE);
    ASSERT_EQ_INT(at(6, 59, 59, true, false).kind, XY_SND_NONE);
    ASSERT_EQ_INT(at(7, 0, 0, true, false).kind, XY_SND_HOUR);              /* end is exclusive */
    ASSERT_EQ_INT(at(21, 59, 59, true, false).kind, XY_SND_TICK);
    ASSERT_EQ_INT(at(22, 0, 0, true, false).kind, XY_SND_NONE);             /* start is inclusive */
}

static void test_quiet_hours_same_day_window(void)
{
    setup(); S.tick = true; S.quiet = true; S.quiet_start_min = 13 * 60; S.quiet_end_min = 15 * 60;
    ASSERT_EQ_INT(at(14, 0, 1, true, false).kind, XY_SND_NONE);
    ASSERT_EQ_INT(at(12, 0, 1, true, false).kind, XY_SND_TICK);
    ASSERT_EQ_INT(at(15, 0, 1, true, false).kind, XY_SND_TICK);
}

static void test_quiet_hours_off_or_untrusted(void)
{
    setup(); S.tick = true; S.quiet_start_min = 0; S.quiet_end_min = 1439;
    ASSERT_EQ_INT(at(3, 0, 1, true, false).kind, XY_SND_TICK);              /* quiet disabled */
    S.quiet = true;
    ASSERT_EQ_INT(at(3, 0, 2, false, false).kind, XY_SND_NONE);             /* untrusted: assume quiet */
}

static void test_quiet_settings_validated(void)
{
    xy_settings_t s; xy_settings_defaults(&s); strcpy(s.tz, "UTC0");
    ASSERT(!s.quiet); ASSERT_EQ_INT(s.quiet_start_min, 22 * 60); ASSERT_EQ_INT(s.quiet_end_min, 7 * 60);
    s.quiet_end_min = 1440; ASSERT_CONTAINS(xy_settings_validate(&s), "quiet_end_min");
    s.quiet_end_min = 420; s.quiet = true; s.quiet_start_min = 420;
    ASSERT_CONTAINS(xy_settings_validate(&s), "quiet");                     /* empty window */
    xy_settings_t out; s.quiet_start_min = 1320;
    ASSERT(xy_settings_from_json("{\"quiet\":true,\"quiet_start_min\":60}", &s, &out) == NULL);
    ASSERT(out.quiet); ASSERT_EQ_INT(out.quiet_start_min, 60);
}

static void test_blob_v2_migrates(void)
{
    xy_settings_t v; xy_settings_defaults(&v);
    strcpy(v.ssid, "home"); strcpy(v.wifi_pass, "password1"); strcpy(v.tz, "UTC0");
    v.chime = true; v.version = 2;
    v.quiet = true; v.quiet_start_min = 9999;          /* garbage beyond the v2 size */
    xy_settings_t out;
    ASSERT(xy_settings_from_blob(&v, XY_SETTINGS_V2_SIZE, &out));
    ASSERT_EQ_INT(out.version, XY_SETTINGS_VERSION);
    ASSERT(out.chime); ASSERT_EQ_STR(out.ssid, "home");
    ASSERT(!out.quiet); ASSERT_EQ_INT(out.quiet_start_min, 22 * 60);
}

int main(void)
{
    RUN(test_sound_defaults_off);
    RUN(test_tick_once_per_second);
    RUN(test_tick_runs_untrusted);
    RUN(test_minute_beep_needs_tick);
    RUN(test_tick_without_minute_ticks_at_zero);
    RUN(test_chime_hour_counts_12h);
    RUN(test_chime_half_hour);
    RUN(test_chime_needs_trusted_time);
    RUN(test_chime_takes_precedence);
    RUN(test_silent_while_alarm_sounds);
    RUN(test_settings_json_has_audio);
    RUN(test_blob_v1_migrates);
    RUN(test_blob_current_and_bad);
    RUN(test_quiet_hours_silence_ticks_and_chimes);
    RUN(test_quiet_hours_same_day_window);
    RUN(test_quiet_hours_off_or_untrusted);
    RUN(test_quiet_settings_validated);
    RUN(test_blob_v2_migrates);
    return TEST_SUMMARY("test_sound");
}
