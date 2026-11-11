/* Clock settings: defaults, validation and JSON (de)serialisation.
 * JSON keys are identical to the C field names. */
#ifndef XY_SETTINGS_H
#define XY_SETTINGS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define XY_SETTINGS_VERSION 3
/* Stored blob size of version 1 (no audio fields), the same on the ESP8266
 * and the host. Versions only ever append fields, so older blobs are a
 * prefix of the current struct. */
#define XY_SETTINGS_V1_SIZE 708
#define XY_SETTINGS_V2_SIZE 712   /* v2 added tick, tick_minute, chime */
#define XY_NUM_ALARMS 6
#define XY_NUM_NTP 4

typedef enum { XY_SEC_WPA2 = 0, XY_SEC_WPA3 = 1, XY_SEC_WPA2_WPA3 = 2 } xy_sec_t;
typedef enum { XY_COLON_STEADY = 0, XY_COLON_BLINK = 1 } xy_colon_t;
typedef enum { XY_STALE_NONE = 0, XY_STALE_BLINK_TO_STEADY = 1,
               XY_STALE_STEADY_TO_BLINK = 2, XY_STALE_ALTERNATE = 3 } xy_stale_t;
typedef enum { XY_G14_NONE = 0, XY_G14_HIGH_WHILE = 1, XY_G14_LOW_WHILE = 2,
               XY_G14_SET_HIGH = 3, XY_G14_SET_LOW = 4, XY_G14_TOGGLE = 5 } xy_g14_t;

typedef struct {
    bool enabled;
    uint8_t hour, minute;
    uint8_t days;           /* bit0 = Sunday .. bit6 = Saturday */
    bool once;              /* ring once, then disable */
    bool sound;
    uint16_t timeout_min;   /* 0 = indefinite, 1..240 */
    uint16_t snooze_min;    /* 1..120 */
    uint8_t g14;            /* xy_g14_t */
} xy_alarm_cfg_t;

typedef struct {
    uint16_t version;
    char ssid[33], wifi_pass[65];
    uint8_t wifi_sec;                       /* xy_sec_t */
    bool dhcp;
    char ip[16], netmask[16], gateway[16], dns[16];
    char hostname[33];
    char ntp[XY_NUM_NTP][64];
    uint32_t ntp_interval_s;                /* 60..86400 */
    char tz[64];                            /* POSIX TZ string; "" = not chosen yet */
    bool h24;
    uint8_t brightness;                     /* 0 = off, 1..8 */
    bool sched;
    uint8_t day_level, night_level;         /* 1..8 */
    uint16_t day_start_min, night_start_min;/* minutes after local midnight */
    uint8_t colon;                          /* xy_colon_t */
    uint8_t stale;                          /* xy_stale_t */
    xy_alarm_cfg_t alarm[XY_NUM_ALARMS];
    bool g14_state;
    char web_pass[33];                      /* "" = no password */
    char ap_pass[65];
    /* --- version 2 --- */
    bool tick;                              /* tick every second */
    bool tick_minute;                       /* with tick: a beep instead on the minute */
    bool chime;                             /* strike the hours, short beep on the half hour */
    /* --- version 3 --- */
    bool quiet;                             /* quiet hours: no ticks or chimes (alarms unaffected) */
    uint16_t quiet_start_min, quiet_end_min;/* [start, end) in local minutes, may wrap midnight */
} xy_settings_t;

void xy_settings_defaults(xy_settings_t *s);

/* NULL if valid, else a message naming the offending field. The message
 * points to static storage, valid until the next call into this module. */
const char *xy_settings_validate(const xy_settings_t *s);

/* JSON of all settings; wifi_pass, web_pass and ap_pass are emitted as "".
 * Returns a malloc'd string (caller frees) or NULL on allocation failure. */
char *xy_settings_to_json(const xy_settings_t *s);

/* Applies a JSON object onto a copy of *cur and validates the result.
 * Absent keys keep their current value. Secrets (wifi_pass, web_pass,
 * ap_pass) that are absent or "" keep their value; "web_pass_clear":true
 * clears web_pass. Returns NULL with *out filled on success, else an error
 * message (static storage, as above); *out is then unspecified. */
const char *xy_settings_from_json(const char *json, const xy_settings_t *cur, xy_settings_t *out);

/* As xy_settings_from_json, but validates only the network fields (for the
 * connection test, which must work before a time zone has been chosen). */
const char *xy_settings_from_json_net(const char *json, const xy_settings_t *cur, xy_settings_t *out);

bool xy_parse_ipv4(const char *s, uint8_t out[4]);

/* Interprets a stored blob of len bytes, upgrading older versions (new
 * fields take their defaults). Returns false if it is not a recognised
 * version and size, or fails validation. */
bool xy_settings_from_blob(const void *blob, size_t len, xy_settings_t *out);

#endif
