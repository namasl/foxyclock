#include "xy_display.h"
#include "xy_settings.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* Font optimised for readability (after Maarten Pennings' nCLC font).
 * Bit order PGFEDCBA. */
const uint8_t XY_FONT[128] = {
    [' '] = 0x00, ['!'] = 0x28, ['"'] = 0x22, ['\''] = 0x02, ['('] = 0x39,
    [')'] = 0x0F, ['*'] = 0x01, ['+'] = 0x42, [','] = 0x0C, ['-'] = 0x40,
    ['.'] = 0x10, ['/'] = 0x52, [':'] = 0x09, ['='] = 0x48, ['?'] = 0x4B,
    ['_'] = 0x08,
    ['0'] = 0x3F, ['1'] = 0x06, ['2'] = 0x5B, ['3'] = 0x4F, ['4'] = 0x66,
    ['5'] = 0x6D, ['6'] = 0x7D, ['7'] = 0x07, ['8'] = 0x7F, ['9'] = 0x6F,
    ['A'] = 0x77, ['B'] = 0x7C, ['C'] = 0x39, ['D'] = 0x5E, ['E'] = 0x79,
    ['F'] = 0x71, ['G'] = 0x3D, ['H'] = 0x76, ['I'] = 0x30, ['J'] = 0x1E,
    ['K'] = 0x75, ['L'] = 0x38, ['M'] = 0x55, ['N'] = 0x37, ['O'] = 0x3F,
    ['P'] = 0x73, ['Q'] = 0x6B, ['R'] = 0x33, ['S'] = 0x6D, ['T'] = 0x78,
    ['U'] = 0x3E, ['V'] = 0x72, ['W'] = 0x6A, ['X'] = 0x36, ['Y'] = 0x6E,
    ['Z'] = 0x5B,
    ['a'] = 0x5F, ['b'] = 0x7C, ['c'] = 0x58, ['d'] = 0x5E, ['e'] = 0x7B,
    ['f'] = 0x71, ['g'] = 0x6F, ['h'] = 0x74, ['i'] = 0x10, ['j'] = 0x0C,
    ['k'] = 0x75, ['l'] = 0x30, ['m'] = 0x55, ['n'] = 0x54, ['o'] = 0x5C,
    ['p'] = 0x73, ['q'] = 0x67, ['r'] = 0x50, ['s'] = 0x6D, ['t'] = 0x78,
    ['u'] = 0x1C, ['v'] = 0x1C, ['w'] = 0x6A, ['x'] = 0x36, ['y'] = 0x6E,
    ['z'] = 0x5B,
};

static uint8_t digit(int d) { return XY_FONT['0' + d]; }

void xy_render_time(xy_frame_t *f, int hour24, int minute, bool h24_mode)
{
    int h = hour24;
    if (!h24_mode) {
        h %= 12;
        if (h == 0) h = 12;
    }
    f->seg[0] = (h24_mode || h >= 10) ? digit(h / 10) : 0x00;
    f->seg[1] = digit(h % 10);
    f->seg[2] = digit(minute / 10);
    f->seg[3] = digit(minute % 10);
}

void xy_render_dashes(xy_frame_t *f)
{
    for (int i = 0; i < 4; i++) f->seg[i] = XY_FONT['-'];
}

void xy_frame_set_colon(xy_frame_t *f, bool upper, bool lower)
{
    f->seg[2] = (uint8_t)((f->seg[2] & 0x7F) | (upper ? XY_SEG_P : 0));
    f->seg[3] = (uint8_t)((f->seg[3] & 0x7F) | (lower ? XY_SEG_P : 0));
}

void xy_render_text(xy_frame_t *f, const char *s)
{
    for (int i = 0; i < 4; i++) {
        unsigned char c = (s && *s) ? (unsigned char)*s++ : ' ';
        f->seg[i] = XY_FONT[c & 0x7F];
    }
}

void xy_colon_dots(uint8_t colon, uint8_t stale, bool is_stale, int64_t now_ms,
                   bool *upper, bool *lower)
{
    bool phase = (now_ms % 1000) < 500;
    bool on = colon == XY_COLON_BLINK ? phase : true;
    if (is_stale) {
        switch (stale) {
        case XY_STALE_BLINK_TO_STEADY: on = true; break;
        case XY_STALE_STEADY_TO_BLINK: on = phase; break;
        case XY_STALE_ALTERNATE: {
            bool even = ((now_ms / 1000) % 2) == 0;
            *upper = even;
            *lower = !even;
            return;
        }
        default: break;
        }
    }
    *upper = on;
    *lower = on;
}

void xy_scroll_start(xy_scroll_t *sc, const char *text, int64_t now_ms)
{
    snprintf(sc->text, sizeof sc->text, "    %.39s    ", text ? text : "");
    sc->len = (int)strlen(sc->text);
    sc->pos = 0;
    sc->next_ms = now_ms + XY_SCROLL_STEP_MS;
}

bool xy_scroll_frame(xy_scroll_t *sc, int64_t now_ms, xy_frame_t *f)
{
    while (now_ms >= sc->next_ms && sc->pos + 4 <= sc->len) {
        sc->pos++;
        sc->next_ms += XY_SCROLL_STEP_MS;
    }
    if (sc->pos + 4 > sc->len) return false;
    xy_render_text(f, sc->text + sc->pos);
    return true;
}
