#include "xy_settings.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"

static char s_err[96];

static const char *err(const char *fmt, const char *field)
{
    snprintf(s_err, sizeof s_err, fmt, field);
    return s_err;
}

static void copy(char *dst, size_t n, const char *src)
{
    snprintf(dst, n, "%s", src);
}

void xy_settings_defaults(xy_settings_t *s)
{
    memset(s, 0, sizeof *s);
    s->version = XY_SETTINGS_VERSION;
    s->wifi_sec = XY_SEC_WPA2;
    s->dhcp = true;
    copy(s->hostname, sizeof s->hostname, "xyclock");
    copy(s->ntp[0], sizeof s->ntp[0], "pool.ntp.org");
    copy(s->ntp[1], sizeof s->ntp[1], "time.google.com");
    copy(s->ntp[2], sizeof s->ntp[2], "time.cloudflare.com");
    copy(s->ntp[3], sizeof s->ntp[3], "time.nist.gov");
    s->ntp_interval_s = 3600;
    s->brightness = 4;
    s->day_level = 6;
    s->night_level = 1;
    s->day_start_min = 7 * 60;
    s->night_start_min = 22 * 60;
    s->colon = XY_COLON_BLINK;
    s->stale = XY_STALE_BLINK_TO_STEADY;
    for (int i = 0; i < XY_NUM_ALARMS; i++) {
        xy_alarm_cfg_t *a = &s->alarm[i];
        a->hour = 7;
        a->days = 0x7F;
        a->sound = true;
        a->timeout_min = 1;
        a->snooze_min = 20;
        a->g14 = XY_G14_NONE;
    }
    copy(s->ap_pass, sizeof s->ap_pass, "xyclock123");
    s->quiet_start_min = 22 * 60;
    s->quiet_end_min = 7 * 60;
}

bool xy_parse_ipv4(const char *s, uint8_t out[4])
{
    for (int i = 0; i < 4; i++) {
        if (!isdigit((unsigned char)*s)) return false;
        int v = 0, n = 0;
        while (isdigit((unsigned char)*s)) {
            v = v * 10 + (*s++ - '0');
            if (++n > 3 || v > 255) return false;
        }
        out[i] = (uint8_t)v;
        if (i < 3 && *s++ != '.') return false;
    }
    return *s == 0;
}

static bool is_ipv4(const char *s) { uint8_t b[4]; return xy_parse_ipv4(s, b); }

static bool hostname_ok(const char *h)
{
    size_t n = strlen(h);
    if (n < 1 || n > 32 || h[0] == '-' || h[n - 1] == '-') return false;
    for (size_t i = 0; i < n; i++)
        if (!(islower((unsigned char)h[i]) || isdigit((unsigned char)h[i]) || h[i] == '-'))
            return false;
    return true;
}

static bool wifi_pass_ok(const char *p)
{
    size_t n = strlen(p);
    if (n >= 8 && n <= 63) return true;
    if (n != 64) return false;
    for (size_t i = 0; i < n; i++)
        if (!isxdigit((unsigned char)p[i])) return false;
    return true;
}

/* True if the fixed-size field contains a NUL (so C-string calls are safe). */
#define TERMINATED(f) (memchr((f), 0, sizeof(f)) != NULL)

static const char *check_terminated(const xy_settings_t *s)
{
    if (!TERMINATED(s->ssid)) return "ssid";
    if (!TERMINATED(s->wifi_pass)) return "wifi_pass";
    if (!TERMINATED(s->ip)) return "ip";
    if (!TERMINATED(s->netmask)) return "netmask";
    if (!TERMINATED(s->gateway)) return "gateway";
    if (!TERMINATED(s->dns)) return "dns";
    if (!TERMINATED(s->hostname)) return "hostname";
    for (int i = 0; i < XY_NUM_NTP; i++) if (!TERMINATED(s->ntp[i])) return "ntp";
    if (!TERMINATED(s->tz)) return "tz";
    if (!TERMINATED(s->web_pass)) return "web_pass";
    if (!TERMINATED(s->ap_pass)) return "ap_pass";
    return NULL;
}

/* Network fields only (SSID, password, security, addressing, host name). */
static const char *validate_net(const xy_settings_t *s)
{
    if (strlen(s->ssid) > 32) return err("%s: too long", "ssid");
    if (s->wifi_sec > XY_SEC_WPA2_WPA3) return err("%s: invalid", "wifi_sec");
    if (s->ssid[0] && !wifi_pass_ok(s->wifi_pass))
        return err("%s: must be 8-63 characters (or 64 hex digits)", "wifi_pass");
    bool all_ntp_ip = true;
    for (int i = 0; i < XY_NUM_NTP; i++)
        if (s->ntp[i][0] && !is_ipv4(s->ntp[i])) all_ntp_ip = false;
    if (!s->dhcp) {
        if (!is_ipv4(s->ip)) return err("%s: not a valid IPv4 address", "ip");
        if (!is_ipv4(s->netmask)) return err("%s: not a valid IPv4 address", "netmask");
        if (!is_ipv4(s->gateway)) return err("%s: not a valid IPv4 address", "gateway");
        if (s->dns[0] ? !is_ipv4(s->dns) : !all_ntp_ip)
            return err("%s: a valid DNS server is needed for NTP server names", "dns");
    }
    if (!hostname_ok(s->hostname))
        return err("%s: 1-32 characters a-z, 0-9 and '-', not starting or ending with '-'", "hostname");
    return NULL;
}

static const char *alarm_err(int i, const char *what)
{
    snprintf(s_err, sizeof s_err, "alarm %d: %s", i + 1, what);
    return s_err;
}

const char *xy_settings_validate(const xy_settings_t *s)
{
    const char *unterm = check_terminated(s);
    if (unterm) return err("%s: corrupt (not terminated)", unterm);
    const char *ne = validate_net(s);
    if (ne) return ne;

    bool any_ntp = false;
    for (int i = 0; i < XY_NUM_NTP; i++) if (s->ntp[i][0]) any_ntp = true;
    if (!any_ntp) return err("%s: at least one server is required", "ntp");
    if (s->ntp_interval_s < 60 || s->ntp_interval_s > 86400)
        return err("%s: must be 60-86400 seconds", "ntp_interval_s");

    size_t tzn = strlen(s->tz);
    if (tzn == 0) return err("%s: please choose a time zone", "tz");
    if (tzn > 63 || !(isalpha((unsigned char)s->tz[0]) || s->tz[0] == '<'))
        return err("%s: not a valid POSIX time zone string", "tz");

    if (s->brightness > 8) return err("%s: must be 0-8", "brightness");
    if (s->day_level < 1 || s->day_level > 8) return err("%s: must be 1-8", "day_level");
    if (s->night_level < 1 || s->night_level > 8) return err("%s: must be 1-8", "night_level");
    if (s->day_start_min > 1439) return err("%s: must be 0-1439", "day_start_min");
    if (s->night_start_min > 1439) return err("%s: must be 0-1439", "night_start_min");

    if (s->colon > XY_COLON_BLINK) return err("%s: invalid", "colon");
    if (s->stale > XY_STALE_ALTERNATE) return err("%s: invalid", "stale");
    if (s->stale == XY_STALE_BLINK_TO_STEADY && s->colon != XY_COLON_BLINK)
        return err("%s: 'blinking to steady' needs a blinking colon", "stale");
    if (s->stale == XY_STALE_STEADY_TO_BLINK && s->colon != XY_COLON_STEADY)
        return err("%s: 'steady to blinking' needs a steady colon", "stale");

    for (int i = 0; i < XY_NUM_ALARMS; i++) {
        const xy_alarm_cfg_t *a = &s->alarm[i];
        if (a->hour > 23) return alarm_err(i, "hour must be 0-23");
        if (a->minute > 59) return alarm_err(i, "minute must be 0-59");
        if (a->days > 0x7F) return alarm_err(i, "days invalid");
        if (a->enabled && a->days == 0) return alarm_err(i, "choose at least one of the days");
        if (a->timeout_min > 240) return alarm_err(i, "timeout must be 0-240 minutes");
        if (a->snooze_min < 1 || a->snooze_min > 120) return alarm_err(i, "snooze must be 1-120 minutes");
        if (a->g14 > XY_G14_TOGGLE) return alarm_err(i, "GPIO14 action invalid");
        if (!a->sound && a->g14 == XY_G14_NONE)
            return alarm_err(i, "with sound off, choose a GPIO14 action");
        if (!a->sound && a->timeout_min == 0 &&
            (a->g14 == XY_G14_HIGH_WHILE || a->g14 == XY_G14_LOW_WHILE))
            return alarm_err(i, "with sound off, the timeout cannot be indefinite");
    }
    if (strlen(s->ap_pass) < 8 || strlen(s->ap_pass) > 63)
        return err("%s: must be 8-63 characters", "ap_pass");
    if (strlen(s->web_pass) > 32) return err("%s: too long (max 32)", "web_pass");
    if (s->quiet_start_min > 1439) return err("%s: must be 0-1439", "quiet_start_min");
    if (s->quiet_end_min > 1439) return err("%s: must be 0-1439", "quiet_end_min");
    if (s->quiet && s->quiet_start_min == s->quiet_end_min)
        return err("%s: start and end must differ", "quiet");
    return NULL;
}

/* ---- JSON out ---------------------------------------------------------- */

char *xy_settings_to_json(const xy_settings_t *s)
{
    cJSON *o = cJSON_CreateObject();
    if (!o) return NULL;
    cJSON_AddStringToObject(o, "ssid", s->ssid);
    cJSON_AddStringToObject(o, "wifi_pass", "");
    cJSON_AddNumberToObject(o, "wifi_sec", s->wifi_sec);
    cJSON_AddBoolToObject(o, "dhcp", s->dhcp);
    cJSON_AddStringToObject(o, "ip", s->ip);
    cJSON_AddStringToObject(o, "netmask", s->netmask);
    cJSON_AddStringToObject(o, "gateway", s->gateway);
    cJSON_AddStringToObject(o, "dns", s->dns);
    cJSON_AddStringToObject(o, "hostname", s->hostname);
    cJSON *ntp = cJSON_AddArrayToObject(o, "ntp");
    for (int i = 0; ntp && i < XY_NUM_NTP; i++)
        cJSON_AddItemToArray(ntp, cJSON_CreateString(s->ntp[i]));
    cJSON_AddNumberToObject(o, "ntp_interval_s", s->ntp_interval_s);
    cJSON_AddStringToObject(o, "tz", s->tz);
    cJSON_AddBoolToObject(o, "h24", s->h24);
    cJSON_AddNumberToObject(o, "brightness", s->brightness);
    cJSON_AddBoolToObject(o, "sched", s->sched);
    cJSON_AddNumberToObject(o, "day_level", s->day_level);
    cJSON_AddNumberToObject(o, "night_level", s->night_level);
    cJSON_AddNumberToObject(o, "day_start_min", s->day_start_min);
    cJSON_AddNumberToObject(o, "night_start_min", s->night_start_min);
    cJSON_AddNumberToObject(o, "colon", s->colon);
    cJSON_AddNumberToObject(o, "stale", s->stale);
    cJSON *al = cJSON_AddArrayToObject(o, "alarm");
    for (int i = 0; al && i < XY_NUM_ALARMS; i++) {
        const xy_alarm_cfg_t *a = &s->alarm[i];
        cJSON *j = cJSON_CreateObject();
        if (!j) break;
        cJSON_AddBoolToObject(j, "enabled", a->enabled);
        cJSON_AddNumberToObject(j, "hour", a->hour);
        cJSON_AddNumberToObject(j, "minute", a->minute);
        cJSON_AddNumberToObject(j, "days", a->days);
        cJSON_AddBoolToObject(j, "once", a->once);
        cJSON_AddBoolToObject(j, "sound", a->sound);
        cJSON_AddNumberToObject(j, "timeout_min", a->timeout_min);
        cJSON_AddNumberToObject(j, "snooze_min", a->snooze_min);
        cJSON_AddNumberToObject(j, "g14", a->g14);
        cJSON_AddItemToArray(al, j);
    }
    cJSON_AddBoolToObject(o, "g14_state", s->g14_state);
    cJSON_AddStringToObject(o, "web_pass", "");
    cJSON_AddStringToObject(o, "ap_pass", "");
    cJSON_AddBoolToObject(o, "tick", s->tick);
    cJSON_AddBoolToObject(o, "tick_minute", s->tick_minute);
    cJSON_AddBoolToObject(o, "chime", s->chime);
    cJSON_AddBoolToObject(o, "quiet", s->quiet);
    cJSON_AddNumberToObject(o, "quiet_start_min", s->quiet_start_min);
    cJSON_AddNumberToObject(o, "quiet_end_min", s->quiet_end_min);
    char *out = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    return out;
}

/* ---- JSON in ----------------------------------------------------------- */

/* Each getter: absent key -> leave value, return NULL; wrong type or out of
 * representable range -> return error. Semantic ranges are left to validate. */

static const char *get_str(const cJSON *o, const char *key, char *dst, size_t n)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    if (!v) return NULL;
    if (!cJSON_IsString(v) || !v->valuestring) return err("%s: wrong type", key);
    if (strlen(v->valuestring) >= n) return err("%s: too long", key);
    copy(dst, n, v->valuestring);
    return NULL;
}

static const char *get_secret(const cJSON *o, const char *key, char *dst, size_t n)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    if (!v) return NULL;
    if (!cJSON_IsString(v) || !v->valuestring) return err("%s: wrong type", key);
    if (!v->valuestring[0]) return NULL;          /* "" keeps the old secret */
    if (strlen(v->valuestring) >= n) return err("%s: too long", key);
    copy(dst, n, v->valuestring);
    return NULL;
}

static const char *get_num(const cJSON *o, const char *key, double max, double *out, bool *present)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    *present = false;
    if (!v) return NULL;
    if (!cJSON_IsNumber(v)) return err("%s: wrong type", key);
    double d = v->valuedouble;
    if (!(d >= 0 && d <= max) || floor(d) != d) return err("%s: out of range", key);
    *out = d;
    *present = true;
    return NULL;
}

#define GET_U(o, key, field, max) do { double _d; bool _p; \
    const char *_e = get_num(o, key, max, &_d, &_p); if (_e) return _e; \
    if (_p) (field) = (__typeof__(field))_d; } while (0)

static const char *get_bool(const cJSON *o, const char *key, bool *dst)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    if (!v) return NULL;
    if (!cJSON_IsBool(v)) return err("%s: wrong type", key);
    *dst = cJSON_IsTrue(v);
    return NULL;
}

#define TRY(x) do { const char *_e = (x); if (_e) return _e; } while (0)

static const char *apply(const cJSON *o, xy_settings_t *s)
{
    if (!cJSON_IsObject(o)) return "settings must be a JSON object";
    TRY(get_str(o, "ssid", s->ssid, sizeof s->ssid));
    TRY(get_secret(o, "wifi_pass", s->wifi_pass, sizeof s->wifi_pass));
    GET_U(o, "wifi_sec", s->wifi_sec, 255);
    TRY(get_bool(o, "dhcp", &s->dhcp));
    TRY(get_str(o, "ip", s->ip, sizeof s->ip));
    TRY(get_str(o, "netmask", s->netmask, sizeof s->netmask));
    TRY(get_str(o, "gateway", s->gateway, sizeof s->gateway));
    TRY(get_str(o, "dns", s->dns, sizeof s->dns));
    TRY(get_str(o, "hostname", s->hostname, sizeof s->hostname));

    const cJSON *ntp = cJSON_GetObjectItemCaseSensitive(o, "ntp");
    if (ntp) {
        if (!cJSON_IsArray(ntp) || cJSON_GetArraySize(ntp) > XY_NUM_NTP)
            return err("%s: must be a list of up to 4 servers", "ntp");
        for (int i = 0; i < XY_NUM_NTP; i++) {
            const cJSON *v = cJSON_GetArrayItem(ntp, i);
            if (!v) { s->ntp[i][0] = 0; continue; }
            if (!cJSON_IsString(v) || !v->valuestring) return err("%s: wrong type", "ntp");
            if (strlen(v->valuestring) >= sizeof s->ntp[i]) return err("%s: server name too long", "ntp");
            copy(s->ntp[i], sizeof s->ntp[i], v->valuestring);
        }
    }
    GET_U(o, "ntp_interval_s", s->ntp_interval_s, 4294967295.0);
    TRY(get_str(o, "tz", s->tz, sizeof s->tz));
    TRY(get_bool(o, "h24", &s->h24));
    GET_U(o, "brightness", s->brightness, 255);
    TRY(get_bool(o, "sched", &s->sched));
    GET_U(o, "day_level", s->day_level, 255);
    GET_U(o, "night_level", s->night_level, 255);
    GET_U(o, "day_start_min", s->day_start_min, 65535);
    GET_U(o, "night_start_min", s->night_start_min, 65535);
    GET_U(o, "colon", s->colon, 255);
    GET_U(o, "stale", s->stale, 255);

    const cJSON *al = cJSON_GetObjectItemCaseSensitive(o, "alarm");
    if (al) {
        if (!cJSON_IsArray(al) || cJSON_GetArraySize(al) > XY_NUM_ALARMS)
            return err("%s: must be a list of up to 6 alarms", "alarm");
        for (int i = 0; i < cJSON_GetArraySize(al); i++) {
            const cJSON *j = cJSON_GetArrayItem(al, i);
            xy_alarm_cfg_t *a = &s->alarm[i];
            if (!cJSON_IsObject(j)) return err("%s: each alarm must be an object", "alarm");
            TRY(get_bool(j, "enabled", &a->enabled));
            GET_U(j, "hour", a->hour, 255);
            GET_U(j, "minute", a->minute, 255);
            GET_U(j, "days", a->days, 255);
            TRY(get_bool(j, "once", &a->once));
            TRY(get_bool(j, "sound", &a->sound));
            GET_U(j, "timeout_min", a->timeout_min, 65535);
            GET_U(j, "snooze_min", a->snooze_min, 65535);
            GET_U(j, "g14", a->g14, 255);
        }
    }
    TRY(get_bool(o, "g14_state", &s->g14_state));
    TRY(get_secret(o, "web_pass", s->web_pass, sizeof s->web_pass));
    bool clear = false;
    TRY(get_bool(o, "web_pass_clear", &clear));
    if (clear) s->web_pass[0] = 0;
    TRY(get_secret(o, "ap_pass", s->ap_pass, sizeof s->ap_pass));
    TRY(get_bool(o, "tick", &s->tick));
    TRY(get_bool(o, "tick_minute", &s->tick_minute));
    TRY(get_bool(o, "chime", &s->chime));
    TRY(get_bool(o, "quiet", &s->quiet));
    GET_U(o, "quiet_start_min", s->quiet_start_min, 65535);
    GET_U(o, "quiet_end_min", s->quiet_end_min, 65535);
    return NULL;
}

const char *xy_settings_from_json_net(const char *json, const xy_settings_t *cur, xy_settings_t *out)
{
    if (!json) return "empty request";
    cJSON *o = cJSON_Parse(json);
    if (!o) return "malformed JSON";
    *out = *cur;
    const char *e = apply(o, out);
    cJSON_Delete(o);
    if (e) return e;
    e = check_terminated(out);
    if (e) return err("%s: corrupt (not terminated)", e);
    return validate_net(out);
}

const char *xy_settings_from_json(const char *json, const xy_settings_t *cur, xy_settings_t *out)
{
    if (!json) return "empty request";
    cJSON *o = cJSON_Parse(json);
    if (!o) return "malformed JSON";
    *out = *cur;
    out->version = XY_SETTINGS_VERSION;
    const char *e = apply(o, out);
    cJSON_Delete(o);
    if (e) return e;
    return xy_settings_validate(out);
}

bool xy_settings_from_blob(const void *blob, size_t len, xy_settings_t *out)
{
    uint16_t ver;
    if (len < sizeof ver) return false;
    memcpy(&ver, blob, sizeof ver);              /* version is the first field */
    /* Bytes of each version that are real fields (the rest of its stored
     * size is struct padding, which may overlap later versions' fields). */
    size_t want = 0, fields = 0;
    switch (ver) {
    case 1: want = XY_SETTINGS_V1_SIZE; fields = offsetof(xy_settings_t, tick); break;
    case 2: want = XY_SETTINGS_V2_SIZE; fields = offsetof(xy_settings_t, quiet); break;
    case XY_SETTINGS_VERSION: want = fields = sizeof *out; break;
    default: return false;
    }
    if (len != want || want > sizeof *out) return false;
    xy_settings_defaults(out);                   /* fields newer than the blob keep defaults */
    memcpy(out, blob, fields);
    out->version = XY_SETTINGS_VERSION;
    return xy_settings_validate(out) == NULL;
}
