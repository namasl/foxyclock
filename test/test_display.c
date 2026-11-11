#include "minitest.h"
#include "xy_display.h"

static void expect(const xy_frame_t *f, uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    ASSERT_EQ_INT(f->seg[0], a); ASSERT_EQ_INT(f->seg[1], b);
    ASSERT_EQ_INT(f->seg[2], c); ASSERT_EQ_INT(f->seg[3], d);
}

static void test_render_12h_no_leading_zero(void)
{ xy_frame_t f; xy_render_time(&f, 9, 5, false); expect(&f, 0x00, 0x6F, 0x3F, 0x6D); }

static void test_render_12h_midnight_is_12(void)
{ xy_frame_t f; xy_render_time(&f, 0, 7, false); expect(&f, 0x06, 0x5B, 0x3F, 0x07); }

static void test_render_12h_pm(void)
{ xy_frame_t f; xy_render_time(&f, 13, 45, false); expect(&f, 0x00, 0x06, 0x66, 0x6D); }

static void test_render_12h_noon(void)
{ xy_frame_t f; xy_render_time(&f, 12, 0, false); expect(&f, 0x06, 0x5B, 0x3F, 0x3F); }

static void test_render_24h_leading_zero(void)
{ xy_frame_t f; xy_render_time(&f, 9, 5, true); expect(&f, 0x3F, 0x6F, 0x3F, 0x6D); }

static void test_render_24h_late(void)
{ xy_frame_t f; xy_render_time(&f, 23, 59, true); expect(&f, 0x5B, 0x4F, 0x6D, 0x6F); }

static void test_colon_bits(void)
{
    xy_frame_t f; xy_render_time(&f, 9, 5, true);
    xy_frame_set_colon(&f, true, false);
    ASSERT(f.seg[2] & XY_SEG_P); ASSERT(!(f.seg[3] & XY_SEG_P));
    xy_frame_set_colon(&f, false, true);
    ASSERT(!(f.seg[2] & XY_SEG_P)); ASSERT(f.seg[3] & XY_SEG_P);
    ASSERT_EQ_INT(f.seg[2] & 0x7F, 0x3F);   /* digit unchanged */
}

static void test_dashes(void)
{ xy_frame_t f; xy_render_dashes(&f); expect(&f, 0x40, 0x40, 0x40, 0x40); }

static void test_render_text(void)
{
    xy_frame_t f; xy_render_text(&f, "AP");
    expect(&f, XY_FONT['A'], XY_FONT['P'], 0, 0);
    xy_render_text(&f, "UPd");
    expect(&f, XY_FONT['U'], XY_FONT['P'], XY_FONT['d'], 0);
}

int main(void)
{
    RUN(test_render_12h_no_leading_zero);
    RUN(test_render_12h_midnight_is_12);
    RUN(test_render_12h_pm);
    RUN(test_render_12h_noon);
    RUN(test_render_24h_leading_zero);
    RUN(test_render_24h_late);
    RUN(test_colon_bits);
    RUN(test_dashes);
    RUN(test_render_text);
    return TEST_SUMMARY("test_display");
}
