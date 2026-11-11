/* "Test connection" sequencing (pure logic). The network module feeds WiFi
 * events in and performs the returned actions; the web page polls state. */
#ifndef XY_WIFITEST_H
#define XY_WIFITEST_H
#include <stdbool.h>
#include <stdint.h>

#define XY_WT_TIMEOUT_MS        20000   /* whole test, including DHCP */
#define XY_WT_MAX_FAILURES      3       /* disconnects before giving up */
#define XY_WT_REASON_NO_AP_FOUND 201    /* WIFI_REASON_NO_AP_FOUND */

typedef enum { XY_WT_IDLE, XY_WT_RUNNING, XY_WT_DONE } xy_wt_state_t;
typedef enum {
    XY_WT_ACT_NONE,
    XY_WT_ACT_CONNECT_TEST,   /* switch the station to the network under test */
    XY_WT_ACT_RETRY,          /* try connecting to it again */
    XY_WT_ACT_RESTORE,        /* return to the previous mode and network */
} xy_wt_act_t;

typedef struct {
    xy_wt_state_t state;
    bool started, associated, ok, timed_out, restore_pending, retry_pending;
    int failures, reason;
    int64_t start_ms;
    char ip[16];
} xy_wt_t;

void xy_wt_init(xy_wt_t *w);
/* Returns false if a test is already running. */
bool xy_wt_start(xy_wt_t *w, int64_t now_ms);
void xy_wt_on_associated(xy_wt_t *w, int64_t now_ms);
void xy_wt_on_got_ip(xy_wt_t *w, int64_t now_ms, const char *ip);
void xy_wt_on_disconnect(xy_wt_t *w, int64_t now_ms, int reason);
/* Call regularly; returns at most one action per call. */
xy_wt_act_t xy_wt_poll(xy_wt_t *w, int64_t now_ms);

#endif
