#include "minitest.h"
#include "xy_timetrust.h"
#include "xy_display.h"
#include "xy_settings.h"

static void test_trust_untrusted_until_sync(void)
{
    xy_timetrust_t t; xy_tt_init(&t, false, 3600);
    ASSERT_EQ_INT(xy_tt_state(&t, 0), XY_TIME_UNTRUSTED);
    ASSERT_EQ_INT(xy_tt_state(&t, 100000), XY_TIME_UNTRUSTED);
    xy_tt_on_sync(&t, 50);
    ASSERT_EQ_INT(xy_tt_state(&t, 60), XY_TIME_TRUSTED);
}

static void test_trust_rtc_valid_trusted(void)
{
    xy_timetrust_t t; xy_tt_init(&t, true, 3600);
    ASSERT_EQ_INT(xy_tt_state(&t, 0), XY_TIME_TRUSTED);
    ASSERT_EQ_INT(xy_tt_state(&t, 7199), XY_TIME_TRUSTED);
    ASSERT_EQ_INT(xy_tt_state(&t, 7200), XY_TIME_STALE);
}

static void test_trust_stale_after_two_intervals(void)
{
    xy_timetrust_t t; xy_tt_init(&t, false, 3600);
    xy_tt_on_sync(&t, 0);
    ASSERT_EQ_INT(xy_tt_state(&t, 7199), XY_TIME_TRUSTED);
    ASSERT_EQ_INT(xy_tt_state(&t, 7200), XY_TIME_STALE);
    xy_tt_on_sync(&t, 7300);
    ASSERT_EQ_INT(xy_tt_state(&t, 7301), XY_TIME_TRUSTED);
}

static void test_trust_interval_change(void)
{
    xy_timetrust_t t; xy_tt_init(&t, false, 3600);
    xy_tt_on_sync(&t, 0);
    xy_tt_set_interval(&t, 60);
    ASSERT_EQ_INT(xy_tt_state(&t, 119), XY_TIME_TRUSTED);
    ASSERT_EQ_INT(xy_tt_state(&t, 120), XY_TIME_STALE);
}

static void dots(uint8_t colon, uint8_t stale, bool is_stale, int64_t ms, int up, int lo)
{
    bool u, l;
    xy_colon_dots(colon, stale, is_stale, ms, &u, &l);
    ASSERT_EQ_INT(u, up); ASSERT_EQ_INT(l, lo);
}

static void test_colon_blink_phase(void)
{
    dots(XY_COLON_BLINK, XY_STALE_BLINK_TO_STEADY, false, 1000, 1, 1);
    dots(XY_COLON_BLINK, XY_STALE_BLINK_TO_STEADY, false, 1499, 1, 1);
    dots(XY_COLON_BLINK, XY_STALE_BLINK_TO_STEADY, false, 1500, 0, 0);
    dots(XY_COLON_BLINK, XY_STALE_BLINK_TO_STEADY, false, 1999, 0, 0);
}

static void test_colon_steady(void)
{ dots(XY_COLON_STEADY, XY_STALE_NONE, false, 1700, 1, 1); }

static void test_colon_stale_blink_to_steady(void)
{ dots(XY_COLON_BLINK, XY_STALE_BLINK_TO_STEADY, true, 1700, 1, 1); }

static void test_colon_stale_steady_to_blink(void)
{
    dots(XY_COLON_STEADY, XY_STALE_STEADY_TO_BLINK, true, 1700, 0, 0);
    dots(XY_COLON_STEADY, XY_STALE_STEADY_TO_BLINK, true, 1200, 1, 1);
}

static void test_colon_stale_alternate(void)
{
    dots(XY_COLON_BLINK, XY_STALE_ALTERNATE, true, 2000, 1, 0);
    dots(XY_COLON_BLINK, XY_STALE_ALTERNATE, true, 3000, 0, 1);
    dots(XY_COLON_STEADY, XY_STALE_ALTERNATE, true, 3700, 0, 1);
}

static void test_colon_stale_none(void)
{
    dots(XY_COLON_BLINK, XY_STALE_NONE, true, 1700, 0, 0);
    dots(XY_COLON_STEADY, XY_STALE_NONE, true, 1700, 1, 1);
}

int main(void)
{
    RUN(test_trust_untrusted_until_sync);
    RUN(test_trust_rtc_valid_trusted);
    RUN(test_trust_stale_after_two_intervals);
    RUN(test_trust_interval_change);
    RUN(test_colon_blink_phase);
    RUN(test_colon_steady);
    RUN(test_colon_stale_blink_to_steady);
    RUN(test_colon_stale_steady_to_blink);
    RUN(test_colon_stale_alternate);
    RUN(test_colon_stale_none);
    return TEST_SUMMARY("test_timetrust");
}
