/* Order of NTP servers handed to lwIP (see design §5.1, plan Task 8). */
#ifndef XY_NTPORD_H
#define XY_NTPORD_H

/* Rotate n server names so in[0] lands at lwIP index cur, in[1] at cur+1 ... */
void xy_ntp_rotate(const char *in[], int n, int cur, const char *out[]);

#endif
