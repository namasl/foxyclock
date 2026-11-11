/* The 20 ms clock loop: buttons, display, alarms, beeper, LEDs, GPIO14.
 * The hooks below are safe to call from any task. */
#ifndef CLOCK_TASK_H
#define CLOCK_TASK_H
#include <stdbool.h>
#include <stdint.h>
#include "xy_timetrust.h"
#include "xy_alarm.h"

/* rtc_valid: the RTC held a valid time at boot (already set as system time). */
void clock_task_start(bool rtc_valid, bool ap_mode);

void clock_notify_settings_changed(void);
/* After a settings save: cancel the alarms in cancel_mask (bit i = alarm i,
 * switched off or edited) with their old settings, then load the new ones. */
void clock_settings_saved(int cancel_mask);
/* Show text instead of the time for ms milliseconds (0 = until cleared). */
void clock_show_text(const char *s, uint32_t ms);
void clock_clear_text(void);
void clock_set_wifi(bool connected);
/* Called after NTP has set the system time. */
void clock_set_ntp_synced(void);
xy_trust_t clock_trust(void);
/* Web page switched alarm i off. */
void clock_cancel_alarm(int i);
/* GPIO14 set from the web page; persists the state. */
void clock_set_g14(bool high);
xy_al_phase_t clock_alarm_phase(int i);
/* IP string supplier for the SET button (set by the network module). */
void clock_set_ip_provider(void (*fn)(char *buf, int n));

#endif
