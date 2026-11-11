#ifndef BEEPER_H
#define BEEPER_H
#include <stdint.h>

void beeper_init(void);
/* Short feedback beep for a button press (4 kHz, 30 ms). */
void beeper_click(void);
/* N short beeps right now (used for boot-mode confirmations); blocks. */
void beeper_beeps(int n);
/* Alarm identification pattern: N x (150 ms tone + 150 ms gap), 1 s pause,
 * repeat. 0 stops. Non-blocking: driven by beeper_tick(). */
void beeper_pattern(int alarm_no);
/* Clock sounds (ticks, minute beep, chimes), non-blocking. kind is an
 * xy_snd_kind_t; count = hour strikes. Ignored while an alarm pattern or
 * another clock sound is playing. */
void beeper_clock_sound(int kind, int count);
/* Call every loop iteration. */
void beeper_tick(int64_t now_ms);

#endif
