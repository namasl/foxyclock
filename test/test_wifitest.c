#include "minitest.h"
#include "xy_wifitest.h"

static void test_wt_idle_initially(void)
{
    xy_wt_t w; xy_wt_init(&w);
    ASSERT_EQ_INT(w.state, XY_WT_IDLE);
    ASSERT_EQ_INT(xy_wt_poll(&w, 1000), XY_WT_ACT_NONE);
}

static void test_wt_success_restores(void)
{
    xy_wt_t w; xy_wt_init(&w);
    ASSERT(xy_wt_start(&w, 0));
    ASSERT_EQ_INT(w.state, XY_WT_RUNNING);
    ASSERT_EQ_INT(xy_wt_poll(&w, 10), XY_WT_ACT_CONNECT_TEST);   /* first poll: switch to the test network */
    ASSERT_EQ_INT(xy_wt_poll(&w, 20), XY_WT_ACT_NONE);
    xy_wt_on_got_ip(&w, 3000, "192.168.1.77");
    ASSERT_EQ_INT(xy_wt_poll(&w, 3001), XY_WT_ACT_RESTORE);      /* go back right away */
    ASSERT_EQ_INT(w.state, XY_WT_DONE);
    ASSERT(w.ok); ASSERT_EQ_STR(w.ip, "192.168.1.77");
    ASSERT_EQ_INT(xy_wt_poll(&w, 3002), XY_WT_ACT_NONE);
}

static void test_wt_failure_records_reason(void)
{
    xy_wt_t w; xy_wt_init(&w);
    xy_wt_start(&w, 0); xy_wt_poll(&w, 1);
    xy_wt_on_disconnect(&w, 500, 15);      /* first failure: lwIP/driver may retry */
    ASSERT_EQ_INT(xy_wt_poll(&w, 600), XY_WT_ACT_RETRY);
    xy_wt_on_disconnect(&w, 4000, 15);
    ASSERT_EQ_INT(xy_wt_poll(&w, 4001), XY_WT_ACT_RETRY);
    xy_wt_on_disconnect(&w, 8000, 15);     /* third failure: give up */
    ASSERT_EQ_INT(xy_wt_poll(&w, 8001), XY_WT_ACT_RESTORE);
    ASSERT(!w.ok); ASSERT_EQ_INT(w.reason, 15); ASSERT_EQ_INT(w.state, XY_WT_DONE);
}

static void test_wt_not_found_gives_up_at_once(void)
{
    xy_wt_t w; xy_wt_init(&w);
    xy_wt_start(&w, 0); xy_wt_poll(&w, 1);
    xy_wt_on_disconnect(&w, 3000, XY_WT_REASON_NO_AP_FOUND);
    ASSERT_EQ_INT(xy_wt_poll(&w, 3001), XY_WT_ACT_RESTORE);
    ASSERT(!w.ok); ASSERT_EQ_INT(w.reason, XY_WT_REASON_NO_AP_FOUND);
}

static void test_wt_timeout(void)
{
    xy_wt_t w; xy_wt_init(&w);
    xy_wt_start(&w, 0); xy_wt_poll(&w, 1);
    ASSERT_EQ_INT(xy_wt_poll(&w, XY_WT_TIMEOUT_MS - 1), XY_WT_ACT_NONE);
    ASSERT_EQ_INT(xy_wt_poll(&w, XY_WT_TIMEOUT_MS), XY_WT_ACT_RESTORE);
    ASSERT(!w.ok); ASSERT(w.timed_out);
}

static void test_wt_connected_but_no_ip_times_out(void)
{
    xy_wt_t w; xy_wt_init(&w);
    xy_wt_start(&w, 0); xy_wt_poll(&w, 1);
    xy_wt_on_associated(&w, 2000);
    ASSERT_EQ_INT(xy_wt_poll(&w, XY_WT_TIMEOUT_MS), XY_WT_ACT_RESTORE);
    ASSERT(!w.ok); ASSERT(w.associated); ASSERT(w.timed_out);
}

static void test_wt_busy_rejects_second_start(void)
{
    xy_wt_t w; xy_wt_init(&w);
    ASSERT(xy_wt_start(&w, 0));
    ASSERT(!xy_wt_start(&w, 5));
    xy_wt_poll(&w, 1); xy_wt_on_got_ip(&w, 2, "1.2.3.4"); xy_wt_poll(&w, 3);
    ASSERT(xy_wt_start(&w, 10));                       /* done -> may start again */
    ASSERT(!w.ok); ASSERT_EQ_STR(w.ip, "");            /* previous result cleared */
}

static void test_wt_events_ignored_when_not_testing(void)
{
    xy_wt_t w; xy_wt_init(&w);
    xy_wt_on_disconnect(&w, 5, 15);
    xy_wt_on_got_ip(&w, 6, "1.1.1.1");
    ASSERT_EQ_INT(w.state, XY_WT_IDLE);
    ASSERT_EQ_INT(xy_wt_poll(&w, 7), XY_WT_ACT_NONE);
}

int main(void)
{
    RUN(test_wt_idle_initially);
    RUN(test_wt_success_restores);
    RUN(test_wt_failure_records_reason);
    RUN(test_wt_not_found_gives_up_at_once);
    RUN(test_wt_timeout);
    RUN(test_wt_connected_but_no_ip_times_out);
    RUN(test_wt_busy_rejects_second_start);
    RUN(test_wt_events_ignored_when_not_testing);
    return TEST_SUMMARY("test_wifitest");
}
