#ifndef NET_H
#define NET_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "xy_settings.h"
#include "xy_wifitest.h"

/* ap_mode: run the setup access point instead of joining the router. */
void net_start(bool ap_mode);
bool net_is_ap(void);
bool net_connected(void);
void net_ip_string(char *buf, int n);
void net_mac_string(char *buf, size_t n);
int net_rssi(void);           /* 0 if not connected */

typedef struct { char ssid[33]; int8_t rssi; uint8_t auth; /* wifi_auth_mode_t */ uint8_t cipher; /* pairwise wifi_cipher_type_t */ } net_ap_t;
/* Blocking scan (~2-4 s). Returns networks found (strongest first, unique
 * SSIDs, hidden ones skipped) or -1 on error. */
int net_scan(net_ap_t *out, int max);
/* 802.11 reason code of the last disconnect (0 = none) and its description. */
int net_last_disconnect_reason(void);
const char *reason_name(int r);

/* Connection test: briefly joins the candidate network (SSID, password,
 * security and addressing taken from *candidate), then returns to the
 * previous mode and network whatever the outcome. Nothing is saved.
 * ESP_ERR_INVALID_STATE if a test is already running. */
esp_err_t net_test_start(const xy_settings_t *candidate);
void net_test_get(xy_wt_t *out, char *ssid, size_t n);

#endif
