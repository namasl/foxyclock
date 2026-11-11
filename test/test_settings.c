#include <stdlib.h>
#include "minitest.h"
#include "xy_settings.h"
#include "xy_tz.h"

static xy_settings_t valid(void)
{
    xy_settings_t s; xy_settings_defaults(&s);
    strcpy(s.tz, "UTC0");
    return s;
}

static void test_defaults(void)
{
    xy_settings_t s; xy_settings_defaults(&s);
    ASSERT_EQ_INT(s.version, XY_SETTINGS_VERSION);
    ASSERT_EQ_INT(s.brightness, 4); ASSERT(!s.h24); ASSERT(!s.sched);
    ASSERT_EQ_INT(s.colon, XY_COLON_BLINK); ASSERT_EQ_INT(s.stale, XY_STALE_BLINK_TO_STEADY);
    ASSERT_EQ_INT(s.ntp_interval_s, 3600);
    ASSERT_EQ_STR(s.ntp[0], "pool.ntp.org"); ASSERT_EQ_STR(s.ntp[1], "time.google.com");
    ASSERT_EQ_STR(s.ntp[2], "time.cloudflare.com"); ASSERT_EQ_STR(s.ntp[3], "time.nist.gov");
    for (int i = 0; i < XY_NUM_ALARMS; i++) {
        ASSERT(!s.alarm[i].enabled); ASSERT_EQ_INT(s.alarm[i].snooze_min, 20);
        ASSERT_EQ_INT(s.alarm[i].timeout_min, 1); ASSERT(s.alarm[i].sound);
        ASSERT_EQ_INT(s.alarm[i].g14, XY_G14_NONE); ASSERT_EQ_INT(s.alarm[i].days, 0x7F);
        ASSERT_EQ_INT(s.alarm[i].hour, 7); ASSERT_EQ_INT(s.alarm[i].minute, 0);
    }
    ASSERT_EQ_STR(s.ap_pass, "xyclock123"); ASSERT_EQ_STR(s.web_pass, "");
    ASSERT_EQ_STR(s.hostname, "xyclock"); ASSERT(s.dhcp); ASSERT_EQ_STR(s.tz, "");
    ASSERT_EQ_STR(s.ssid, ""); ASSERT_EQ_INT(s.wifi_sec, XY_SEC_WPA2);
    ASSERT(!s.g14_state);
}

static void test_validate_requires_tz(void)
{
    xy_settings_t s; xy_settings_defaults(&s);
    ASSERT_CONTAINS(xy_settings_validate(&s), "tz");
    strcpy(s.tz, "UTC0");
    ASSERT(xy_settings_validate(&s) == NULL);
}

static void test_alarm_no_days_rejected(void)
{
    xy_settings_t s = valid();
    s.alarm[2].enabled = true; s.alarm[2].days = 0;
    ASSERT_CONTAINS(xy_settings_validate(&s), "days");
    s.alarm[2].enabled = false;          /* disabled alarms may be left blank */
    ASSERT(xy_settings_validate(&s) == NULL);
}

static void test_sound_off_needs_gpio_action(void)
{
    xy_settings_t s = valid();
    s.alarm[0].sound = false;
    ASSERT_CONTAINS(xy_settings_validate(&s), "alarm 1");
    s.alarm[0].g14 = XY_G14_SET_HIGH;
    ASSERT(xy_settings_validate(&s) == NULL);
}

static void test_sound_off_while_needs_timeout(void)
{
    xy_settings_t s = valid();
    s.alarm[1].sound = false; s.alarm[1].g14 = XY_G14_HIGH_WHILE; s.alarm[1].timeout_min = 0;
    ASSERT_CONTAINS(xy_settings_validate(&s), "alarm 2");
    s.alarm[1].timeout_min = 10;
    ASSERT(xy_settings_validate(&s) == NULL);
    s.alarm[1].sound = true; s.alarm[1].timeout_min = 0;  /* indefinite fine with sound */
    ASSERT(xy_settings_validate(&s) == NULL);
}

static void test_alarm_ranges(void)
{
    xy_settings_t s = valid();
    s.alarm[0].hour = 24; ASSERT(xy_settings_validate(&s) != NULL); s.alarm[0].hour = 23;
    s.alarm[0].minute = 60; ASSERT(xy_settings_validate(&s) != NULL); s.alarm[0].minute = 59;
    s.alarm[0].snooze_min = 0; ASSERT(xy_settings_validate(&s) != NULL); s.alarm[0].snooze_min = 120;
    s.alarm[0].timeout_min = 241; ASSERT(xy_settings_validate(&s) != NULL); s.alarm[0].timeout_min = 240;
    s.alarm[0].g14 = 6; ASSERT(xy_settings_validate(&s) != NULL); s.alarm[0].g14 = XY_G14_TOGGLE;
    ASSERT(xy_settings_validate(&s) == NULL);
}

static void test_stale_style_compat(void)
{
    xy_settings_t s = valid();
    s.colon = XY_COLON_STEADY; s.stale = XY_STALE_BLINK_TO_STEADY;
    ASSERT_CONTAINS(xy_settings_validate(&s), "stale");
    s.stale = XY_STALE_STEADY_TO_BLINK; ASSERT(xy_settings_validate(&s) == NULL);
    s.colon = XY_COLON_BLINK; ASSERT(xy_settings_validate(&s) != NULL);
    s.stale = XY_STALE_ALTERNATE; ASSERT(xy_settings_validate(&s) == NULL);
    s.stale = XY_STALE_NONE; ASSERT(xy_settings_validate(&s) == NULL);
}

static void test_static_ip_requires_valid_addrs(void)
{
    xy_settings_t s = valid();
    s.dhcp = false;
    strcpy(s.ip, "192.168.1.300"); strcpy(s.netmask, "255.255.255.0");
    strcpy(s.gateway, "192.168.1.1"); strcpy(s.dns, "192.168.1.1");
    ASSERT_CONTAINS(xy_settings_validate(&s), "ip");
    strcpy(s.ip, "192.168.1.30"); ASSERT(xy_settings_validate(&s) == NULL);
    strcpy(s.dns, ""); ASSERT_CONTAINS(xy_settings_validate(&s), "dns");
    strcpy(s.ntp[0], "192.168.1.2"); strcpy(s.ntp[1], ""); strcpy(s.ntp[2], ""); strcpy(s.ntp[3], "");
    ASSERT(xy_settings_validate(&s) == NULL);     /* IP-only NTP needs no DNS */
    strcpy(s.netmask, "1.2.3"); ASSERT_CONTAINS(xy_settings_validate(&s), "netmask");
}

static void test_wifi_password_rules(void)
{
    xy_settings_t s = valid();
    strcpy(s.ssid, "home");
    strcpy(s.wifi_pass, "short"); ASSERT_CONTAINS(xy_settings_validate(&s), "wifi_pass");
    strcpy(s.wifi_pass, "longenough"); ASSERT(xy_settings_validate(&s) == NULL);
    s.wifi_sec = 3; ASSERT_CONTAINS(xy_settings_validate(&s), "wifi_sec");
}

static void test_other_ranges(void)
{
    xy_settings_t s = valid();
    s.ntp_interval_s = 59; ASSERT_CONTAINS(xy_settings_validate(&s), "ntp_interval_s");
    s.ntp_interval_s = 86400; ASSERT(xy_settings_validate(&s) == NULL);
    for (int i = 0; i < XY_NUM_NTP; i++) s.ntp[i][0] = 0;
    ASSERT_CONTAINS(xy_settings_validate(&s), "ntp");
    s = valid();
    s.brightness = 9; ASSERT(xy_settings_validate(&s) != NULL); s.brightness = 0;
    ASSERT(xy_settings_validate(&s) == NULL);
    s.day_level = 0; ASSERT(xy_settings_validate(&s) != NULL); s.day_level = 8;
    s.night_start_min = 1440; ASSERT(xy_settings_validate(&s) != NULL); s.night_start_min = 1439;
    ASSERT(xy_settings_validate(&s) == NULL);
    strcpy(s.hostname, "-bad"); ASSERT_CONTAINS(xy_settings_validate(&s), "hostname");
    strcpy(s.hostname, "Bad"); ASSERT(xy_settings_validate(&s) != NULL);
    strcpy(s.hostname, "kitchen-clock2"); ASSERT(xy_settings_validate(&s) == NULL);
    strcpy(s.ap_pass, "1234567"); ASSERT_CONTAINS(xy_settings_validate(&s), "ap_pass");
    strcpy(s.ap_pass, "12345678"); ASSERT(xy_settings_validate(&s) == NULL);
    strcpy(s.tz, "5EST"); ASSERT_CONTAINS(xy_settings_validate(&s), "tz");
    strcpy(s.tz, "<+0530>-5:30"); ASSERT(xy_settings_validate(&s) == NULL);
}

static void test_json_roundtrip(void)
{
    xy_settings_t s = valid(), d, out;
    strcpy(s.ssid, "my net"); strcpy(s.wifi_pass, "secret123"); s.wifi_sec = XY_SEC_WPA3;
    s.h24 = true; s.brightness = 7; s.sched = true; s.night_level = 2; s.day_start_min = 400;
    s.colon = XY_COLON_STEADY; s.stale = XY_STALE_ALTERNATE; strcpy(s.ntp[3], "10.0.0.1");
    s.alarm[4].enabled = true; s.alarm[4].hour = 6; s.alarm[4].minute = 30; s.alarm[4].days = 0x3E;
    s.alarm[4].once = true; s.alarm[4].sound = false; s.alarm[4].g14 = XY_G14_TOGGLE;
    s.alarm[4].timeout_min = 0; s.alarm[4].snooze_min = 9; s.g14_state = true;
    strcpy(s.tz, "EST5EDT,M3.2.0,M11.1.0");
    char *j = xy_settings_to_json(&s);
    ASSERT(j != NULL);
    xy_settings_defaults(&d);
    strcpy(d.wifi_pass, "secret123");          /* secrets come from current settings */
    ASSERT(xy_settings_from_json(j, &d, &out) == NULL);
    ASSERT(memcmp(&s, &out, sizeof s) == 0);
    free(j);
}

static void test_json_secrets_not_emitted(void)
{
    xy_settings_t s = valid();
    strcpy(s.wifi_pass, "hunter22x"); strcpy(s.web_pass, "pw"); strcpy(s.ap_pass, "apsecret1");
    char *j = xy_settings_to_json(&s);
    ASSERT(strstr(j, "hunter22x") == NULL); ASSERT(strstr(j, "apsecret1") == NULL);
    ASSERT_CONTAINS(j, "\"wifi_pass\":\"\"");
    ASSERT(strstr(j, "\"pw\"") == NULL);
    free(j);
}

static void test_json_secret_kept_when_absent(void)
{
    xy_settings_t cur = valid(), out;
    strcpy(cur.wifi_pass, "hunter22x"); strcpy(cur.web_pass, "pw");
    ASSERT(xy_settings_from_json("{\"brightness\":2}", &cur, &out) == NULL);
    ASSERT_EQ_STR(out.wifi_pass, "hunter22x"); ASSERT_EQ_STR(out.web_pass, "pw");
    ASSERT_EQ_INT(out.brightness, 2);
    ASSERT(xy_settings_from_json("{\"wifi_pass\":\"\"}", &cur, &out) == NULL);
    ASSERT_EQ_STR(out.wifi_pass, "hunter22x");
    ASSERT(xy_settings_from_json("{\"web_pass_clear\":true}", &cur, &out) == NULL);
    ASSERT_EQ_STR(out.web_pass, "");
    ASSERT(xy_settings_from_json("{\"web_pass\":\"newpw\"}", &cur, &out) == NULL);
    ASSERT_EQ_STR(out.web_pass, "newpw");
}

static void test_settings_from_json_rejects_overlong_ssid(void)
{
    xy_settings_t cur = valid(), out;
    const char *e = xy_settings_from_json(
        "{\"ssid\":\"0123456789012345678901234567890123456789\"}", &cur, &out);
    ASSERT_CONTAINS(e, "ssid");
}

static void test_settings_from_json_rejects_garbage(void)
{
    xy_settings_t cur = valid(), out;
    ASSERT(xy_settings_from_json("{not json", &cur, &out) != NULL);
    ASSERT(xy_settings_from_json("[]", &cur, &out) != NULL);
    ASSERT(xy_settings_from_json("", &cur, &out) != NULL);
    ASSERT(xy_settings_from_json(NULL, &cur, &out) != NULL);
}

static void test_settings_from_json_rejects_wrong_types(void)
{
    xy_settings_t cur = valid(), out;
    ASSERT_CONTAINS(xy_settings_from_json("{\"brightness\":\"high\"}", &cur, &out), "brightness");
    ASSERT_CONTAINS(xy_settings_from_json("{\"h24\":1}", &cur, &out), "h24");
    ASSERT_CONTAINS(xy_settings_from_json("{\"ntp\":\"pool.ntp.org\"}", &cur, &out), "ntp");
    ASSERT_CONTAINS(xy_settings_from_json("{\"alarm\":[{\"hour\":-1}]}", &cur, &out), "hour");
    ASSERT_CONTAINS(xy_settings_from_json("{\"brightness\":2.5}", &cur, &out), "brightness");
    ASSERT_CONTAINS(xy_settings_from_json("{\"ntp_interval_s\":1e12}", &cur, &out), "ntp_interval_s");
}

static void test_settings_from_json_rejects_invalid_result(void)
{
    xy_settings_t cur = valid(), out;
    ASSERT_CONTAINS(xy_settings_from_json(
        "{\"alarm\":[{\"enabled\":true,\"days\":0}]}", &cur, &out), "days");
}

static void test_json_partial_alarm_array(void)
{
    xy_settings_t cur = valid(), out;
    ASSERT(xy_settings_from_json("{\"alarm\":[{},{\"enabled\":true,\"hour\":5}]}", &cur, &out) == NULL);
    ASSERT(out.alarm[1].enabled); ASSERT_EQ_INT(out.alarm[1].hour, 5);
    ASSERT_EQ_INT(out.alarm[1].minute, 0); ASSERT(!out.alarm[0].enabled);
    ASSERT(xy_settings_from_json(
        "{\"alarm\":[{},{},{},{},{},{},{}]}", &cur, &out) != NULL);   /* 7 alarms */
}

static void test_tz_table_valid(void)
{
    ASSERT(XY_TZ_COUNT >= 20);
    for (size_t i = 0; i < XY_TZ_COUNT; i++) {
        xy_settings_t s = valid();
        strcpy(s.tz, XY_TZ_TABLE[i].posix);
        ASSERT(xy_settings_validate(&s) == NULL);
        for (size_t j = i + 1; j < XY_TZ_COUNT; j++)
            ASSERT(strcmp(XY_TZ_TABLE[i].name, XY_TZ_TABLE[j].name) != 0);
    }
}

static void test_blob_unterminated_strings_rejected(void)
{
    /* A corrupt blob with no NUL inside a string field must be rejected
     * (defaults used), without reading past the field. */
    static xy_settings_t s;
    xy_settings_defaults(&s); strcpy(s.tz, "UTC0");
    memset(s.ssid, 'A', sizeof s.ssid);              /* no terminator */
    xy_settings_t out;
    ASSERT(!xy_settings_from_blob(&s, sizeof s, &out));
    xy_settings_defaults(&s); strcpy(s.tz, "UTC0");
    memset(s.web_pass, 'B', sizeof s.web_pass);      /* last string field before audio */
    ASSERT(!xy_settings_from_blob(&s, sizeof s, &out));
    xy_settings_defaults(&s); strcpy(s.tz, "UTC0");
    memset(s.ntp[3], 'C', sizeof s.ntp[3]);
    ASSERT(!xy_settings_from_blob(&s, sizeof s, &out));
    xy_settings_defaults(&s); strcpy(s.tz, "UTC0");
    ASSERT(xy_settings_from_blob(&s, sizeof s, &out)); /* intact blob still accepted */
}

static void test_validate_unterminated_strings_safe(void)
{
    xy_settings_t s; xy_settings_defaults(&s); strcpy(s.tz, "UTC0");
    memset(s.ap_pass, 'x', sizeof s.ap_pass);
    ASSERT_CONTAINS(xy_settings_validate(&s), "ap_pass");
    xy_settings_defaults(&s);
    memset(s.tz, 'E', sizeof s.tz);
    ASSERT_CONTAINS(xy_settings_validate(&s), "tz");
}

static void test_net_only_validation(void)
{
    /* First setup: no time zone yet, but the network can still be tested. */
    xy_settings_t cur; xy_settings_defaults(&cur);
    xy_settings_t out;
    const char *body = "{\"ssid\":\"home\",\"wifi_pass\":\"password1\",\"tz\":\"\",\"alarm\":[{\"enabled\":true,\"days\":0}]}";
    ASSERT(xy_settings_from_json_net(body, &cur, &out) == NULL);
    ASSERT_EQ_STR(out.ssid, "home");
    ASSERT_CONTAINS(xy_settings_from_json_net("{\"ssid\":\"home\",\"wifi_pass\":\"short\"}", &cur, &out), "wifi_pass");
    ASSERT_CONTAINS(xy_settings_from_json_net("{\"ssid\":\"h\",\"wifi_pass\":\"password1\",\"dhcp\":false,\"ip\":\"1.2.3\"}", &cur, &out), "ip");
    ASSERT(xy_settings_from_json_net("{not json", &cur, &out) != NULL);
}

static void test_wifi_pass_64_must_be_hex(void)
{
    xy_settings_t s; xy_settings_defaults(&s); strcpy(s.tz, "UTC0"); strcpy(s.ssid, "h");
    memset(s.wifi_pass, 'a', 64); s.wifi_pass[64] = 0;
    ASSERT(xy_settings_validate(&s) == NULL);
    s.wifi_pass[10] = 'z';
    ASSERT_CONTAINS(xy_settings_validate(&s), "wifi_pass");
}

int main(void)
{
    RUN(test_defaults);
    RUN(test_validate_requires_tz);
    RUN(test_alarm_no_days_rejected);
    RUN(test_sound_off_needs_gpio_action);
    RUN(test_sound_off_while_needs_timeout);
    RUN(test_alarm_ranges);
    RUN(test_stale_style_compat);
    RUN(test_static_ip_requires_valid_addrs);
    RUN(test_wifi_password_rules);
    RUN(test_other_ranges);
    RUN(test_json_roundtrip);
    RUN(test_json_secrets_not_emitted);
    RUN(test_json_secret_kept_when_absent);
    RUN(test_settings_from_json_rejects_overlong_ssid);
    RUN(test_settings_from_json_rejects_garbage);
    RUN(test_settings_from_json_rejects_wrong_types);
    RUN(test_settings_from_json_rejects_invalid_result);
    RUN(test_json_partial_alarm_array);
    RUN(test_tz_table_valid);
    RUN(test_blob_unterminated_strings_rejected);
    RUN(test_validate_unterminated_strings_safe);
    RUN(test_net_only_validation);
    RUN(test_wifi_pass_64_must_be_hex);
    return TEST_SUMMARY("test_settings");
}
