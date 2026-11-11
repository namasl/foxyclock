#include "xy_ntpord.h"

void xy_ntp_rotate(const char *in[], int n, int cur, const char *out[])
{
    if (n <= 0) return;
    cur %= n;
    if (cur < 0) cur += n;
    for (int i = 0; i < n; i++) out[(cur + i) % n] = in[i];
}
