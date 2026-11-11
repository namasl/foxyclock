/* 7-segment rendering for the XY-Clock's TM1650 display. No hardware access. */
#ifndef XY_DISPLAY_H
#define XY_DISPLAY_H
#include <stdbool.h>
#include <stdint.h>

#define XY_SEG_P 0x80   /* bit 7 of a digit register (see board wiring notes) */

/* Segment patterns, bit order PGFEDCBA, indexed by 7-bit ASCII. */
extern const uint8_t XY_FONT[128];

/* Values for TM1650 digit registers 1..4 (left to right), P bits included.
 * Wiring: seg[1] P = decimal point of digit 2, seg[0] P = decimal point of
 * digit 3, seg[2] P = upper colon dot, seg[3] P = lower colon dot. */
typedef struct { uint8_t seg[4]; } xy_frame_t;

/* hour24 0..23. 12 h mode: hour 0 -> 12, no leading zero. 24 h: leading zero.
 * P bits are cleared. */
void xy_render_time(xy_frame_t *f, int hour24, int minute, bool h24_mode);
void xy_render_dashes(xy_frame_t *f);
void xy_frame_set_colon(xy_frame_t *f, bool upper, bool lower);
/* First 4 characters of s (shorter strings are padded with blanks). */
void xy_render_text(xy_frame_t *f, const char *s);

/* Colon dots for the configured style (xy_colon_t) and stale indicator
 * (xy_stale_t). now_ms is wall-clock milliseconds; blinking is on for the
 * first half of each second. */
void xy_colon_dots(uint8_t colon, uint8_t stale, bool is_stale, int64_t now_ms,
                   bool *upper, bool *lower);

/* Text scrolling right to left, one character every 300 ms. */
#define XY_SCROLL_STEP_MS 300
typedef struct { char text[48]; int len, pos; int64_t next_ms; } xy_scroll_t;
/* Pads 4 blanks at each end; text longer than 39 characters is truncated. */
void xy_scroll_start(xy_scroll_t *sc, const char *text, int64_t now_ms);
/* Fills f with the current window; returns false once the text has passed. */
bool xy_scroll_frame(xy_scroll_t *sc, int64_t now_ms, xy_frame_t *f);

#endif
