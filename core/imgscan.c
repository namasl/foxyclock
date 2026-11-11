#include "xy_imgscan.h"
#include <stdio.h>
#include <string.h>

void xy_imgscan_init(xy_imgscan_t *s)
{
    memset(s, 0, sizeof *s);
}

int xy_imgscan_feed(xy_imgscan_t *s, const uint8_t *data, size_t len, const char *want)
{
    if (s->result) return s->result;
    if (len > sizeof s->win - s->win_len) len = sizeof s->win - s->win_len;
    memcpy(s->win + s->win_len, data, len);
    s->win_len += len;

    /* esp_app_desc_t: magic(4) secure_version(4) reserv1(8) version[32] project_name[32] ... */
    size_t i = 0;
    for (; i + XY_APP_DESC_SIZE <= s->win_len; i += 4) {
        uint32_t m;
        memcpy(&m, s->win + i, 4);
        if (m != XY_APP_DESC_MAGIC) continue;
        /* The magic word also occurs in code as a literal constant. A real
         * descriptor has its reserv1[2] words zero and NUL-terminated,
         * printable version and project strings. */
        static const uint8_t zero8[8];
        const uint8_t *d = s->win + i;
        if (memcmp(d + 8, zero8, 8) != 0) continue;
        if (!memchr(d + 16, 0, 32) || !memchr(d + 48, 0, 32)) continue;
        int printable = 1;
        for (const uint8_t *p = d + 48; *p; p++) if (*p < 0x20 || *p > 0x7E) printable = 0;
        if (!printable || d[48] == 0) continue;
        snprintf(s->version, sizeof s->version, "%.31s", (const char *)s->win + i + 16);
        snprintf(s->project, sizeof s->project, "%.31s", (const char *)s->win + i + 48);
        s->result = strcmp(s->project, want) == 0 ? 1 : -1;
        return s->result;
    }
    /* Keep the unscanned remainder (from i, which is 4-aligned) for the next call. */
    memmove(s->win, s->win + i, s->win_len - i);
    s->win_off += i;
    s->win_len -= i;
    return 0;
}
