/* Named time zones offered on the web page. */
#ifndef XY_TZ_H
#define XY_TZ_H
#include <stddef.h>

typedef struct { const char *name; const char *posix; } xy_tz_t;
extern const xy_tz_t XY_TZ_TABLE[];
extern const size_t XY_TZ_COUNT;

#endif
