#ifndef NTP_H
#define NTP_H
#include <stddef.h>
#include <stdint.h>

/* Start SNTP (idempotent); call when the station has an IP address. */
void ntp_start(void);
/* Re-read servers and interval from the settings store. */
void ntp_restart(void);
/* Name of the server that last answered ("" if none) and the epoch of the last sync (0 if none). */
void ntp_status(char *server, size_t n, int64_t *last_sync_epoch);

#endif
