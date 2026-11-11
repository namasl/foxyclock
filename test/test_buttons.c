#include "minitest.h"
#include "xy_buttons.h"
#include "xy_display.h"
#include "xy_settings.h"
#include "xy_ntpord.h"

static void test_debounce_requires_two_samples(void)
{
    xy_btn_t b; xy_btn_init(&b, 0);
    ASSERT_EQ_INT(xy_btn_update(&b, XY_BTN_UP), 0);
    ASSERT_EQ_INT(xy_btn_update(&b, XY_BTN_UP), XY_BTN_UP);
    ASSERT_EQ_INT(xy_btn_update(&b, XY_BTN_UP), 0);
    ASSERT_EQ_INT(xy_btn_update(&b, 0), 0);
    ASSERT_EQ_INT(xy_btn_update(&b, 0), 0);
    ASSERT_EQ_INT(xy_btn_update(&b, XY_BTN_UP), 0);
    ASSERT_EQ_INT(xy_btn_update(&b, XY_BTN_UP), XY_BTN_UP);   /* second press */
}

static void test_debounce_ignores_glitch(void)
{
    xy_btn_t b; xy_btn_init(&b, 0);
    ASSERT_EQ_INT(xy_btn_update(&b, XY_BTN_SET), 0);
    ASSERT_EQ_INT(xy_btn_update(&b, 0), 0);
    ASSERT_EQ_INT(xy_btn_update(&b, XY_BTN_SET), 0);
    ASSERT_EQ_INT(xy_btn_update(&b, 0), 0);
}

static void test_debounce_held_at_init_no_press(void)
{
    xy_btn_t b; xy_btn_init(&b, XY_BTN_DOWN);     /* held through boot */
    ASSERT_EQ_INT(xy_btn_update(&b, XY_BTN_DOWN), 0);
    ASSERT_EQ_INT(xy_btn_update(&b, XY_BTN_DOWN), 0);
}

static void test_debounce_two_buttons(void)
{
    xy_btn_t b; xy_btn_init(&b, 0);
    xy_btn_update(&b, XY_BTN_UP | XY_BTN_DOWN);
    ASSERT_EQ_INT(xy_btn_update(&b, XY_BTN_UP | XY_BTN_DOWN), XY_BTN_UP | XY_BTN_DOWN);
}

static void test_boot_hold_classification(void)
{
    ASSERT_EQ_INT(xy_boot_classify(0, 0), XY_BOOT_NORMAL);
    ASSERT_EQ_INT(xy_boot_classify(XY_BTN_SET, 0), XY_BOOT_AP);
    ASSERT_EQ_INT(xy_boot_classify(XY_BTN_UP, 0), XY_BOOT_AP);
    ASSERT_EQ_INT(xy_boot_classify(XY_BTN_DOWN, 5000), XY_BOOT_AP);
    ASSERT_EQ_INT(xy_boot_classify(XY_BTN_UP | XY_BTN_DOWN, 3000), XY_BOOT_FACTORY);
    ASSERT_EQ_INT(xy_boot_classify(XY_BTN_UP | XY_BTN_DOWN, 1000), XY_BOOT_AP);
    ASSERT_EQ_INT(xy_boot_classify(XY_BTN_UP | XY_BTN_DOWN | XY_BTN_SET, 4000), XY_BOOT_FACTORY);
}

static void test_brightness_clamp(void)
{
    ASSERT_EQ_INT(xy_brightness_step(8, +1), 8);
    ASSERT_EQ_INT(xy_brightness_step(0, -1), 0);
    ASSERT_EQ_INT(xy_brightness_step(4, +1), 5);
    ASSERT_EQ_INT(xy_brightness_step(1, -1), 0);
}

static void test_brightness_schedule(void)
{
    xy_settings_t s; xy_settings_defaults(&s);
    s.sched = true; s.day_level = 7; s.night_level = 2; s.day_start_min = 420; s.night_start_min = 1320;
    ASSERT_EQ_INT(xy_brightness_active(&s, 600), 7);
    ASSERT_EQ_INT(xy_brightness_active(&s, 420), 7);
    ASSERT_EQ_INT(xy_brightness_active(&s, 1320), 2);
    ASSERT_EQ_INT(xy_brightness_active(&s, 1380), 2);
    ASSERT_EQ_INT(xy_brightness_active(&s, 300), 2);
    s.day_start_min = 1320; s.night_start_min = 420;            /* "day" across midnight */
    ASSERT_EQ_INT(xy_brightness_active(&s, 1380), 7);
    ASSERT_EQ_INT(xy_brightness_active(&s, 100), 7);
    ASSERT_EQ_INT(xy_brightness_active(&s, 600), 2);
    ASSERT(xy_brightness_is_day(&s, 100));
}

static void test_brightness_fixed(void)
{
    xy_settings_t s; xy_settings_defaults(&s);
    s.brightness = 3;
    ASSERT_EQ_INT(xy_brightness_active(&s, 0), 3);
    ASSERT_EQ_INT(xy_brightness_active(&s, 1000), 3);
}

static void test_scroll_ip(void)
{
    xy_scroll_t sc; xy_frame_t f;
    xy_scroll_start(&sc, "10.0.0.5", 0);
    ASSERT(xy_scroll_frame(&sc, 0, &f));
    ASSERT_EQ_INT(f.seg[0] | f.seg[1] | f.seg[2] | f.seg[3], 0);   /* starts blank */
    /* "    10.0.0.5    " is 16 chars: windows at pos 0..12, done at step 13. */
    int saw_one = 0;
    int64_t t;
    for (t = 0; t < 100000; t += 50) {
        if (!xy_scroll_frame(&sc, t, &f)) break;
        if (f.seg[3] == XY_FONT['1']) saw_one = 1;
    }
    ASSERT(saw_one);
    ASSERT_EQ_INT(t, 13 * XY_SCROLL_STEP_MS);
    ASSERT(!xy_scroll_frame(&sc, 100000, &f));
}

static void test_scroll_truncates_long_text(void)
{
    xy_scroll_t sc; xy_frame_t f;
    xy_scroll_start(&sc, "0123456789012345678901234567890123456789012345678901234567890", 0);
    ASSERT(xy_scroll_frame(&sc, 0, &f));
}

static void test_ntp_rotate(void)
{
    const char *in[3] = { "a", "b", "c" }, *out[3];
    xy_ntp_rotate(in, 3, 0, out);
    ASSERT_EQ_STR(out[0], "a"); ASSERT_EQ_STR(out[1], "b"); ASSERT_EQ_STR(out[2], "c");
    xy_ntp_rotate(in, 3, 1, out);
    ASSERT_EQ_STR(out[0], "c"); ASSERT_EQ_STR(out[1], "a"); ASSERT_EQ_STR(out[2], "b");
    xy_ntp_rotate(in, 3, 2, out);
    ASSERT_EQ_STR(out[0], "b"); ASSERT_EQ_STR(out[1], "c"); ASSERT_EQ_STR(out[2], "a");
    xy_ntp_rotate(in, 3, 5, out);          /* out-of-range index wraps */
    ASSERT_EQ_STR(out[2], "a");
}

int main(void)
{
    RUN(test_debounce_requires_two_samples);
    RUN(test_debounce_ignores_glitch);
    RUN(test_debounce_held_at_init_no_press);
    RUN(test_debounce_two_buttons);
    RUN(test_boot_hold_classification);
    RUN(test_brightness_clamp);
    RUN(test_brightness_schedule);
    RUN(test_brightness_fixed);
    RUN(test_scroll_ip);
    RUN(test_scroll_truncates_long_text);
    RUN(test_ntp_rotate);
    return TEST_SUMMARY("test_buttons");
}
