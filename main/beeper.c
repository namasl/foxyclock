/* Passive beeper on GPIO5 driven by the SDK PWM (one channel). */
#include "beeper.h"
#include "board.h"
#include "xy_sound.h"
#include "driver/pwm.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define ALARM_HZ  2700
#define CLICK_HZ  4000
#define TONE_MS   150
#define GAP_MS    150
#define PAUSE_MS  1000

static bool s_on;
static uint32_t s_hz;
static int s_pattern;            /* alarm number, 0 = none */
static int64_t s_pat_start_ms;
static int64_t s_click_until_ms;

/* Clock sounds: a short sequence of tones (hz, ms on, ms off), played from
 * beeper_tick(). */
typedef struct { uint16_t hz, on_ms, off_ms; } tone_t;
static tone_t s_seq[12];
static int s_seq_len, s_seq_pos;
static int64_t s_seq_next_ms;
static bool s_seq_tone_on;

static void tone_on(uint32_t hz)
{
    if (s_on && s_hz == hz) return;
    uint32_t period = 1000000 / hz;
    pwm_set_period(period);
    pwm_set_duty(0, period / 2);
    pwm_start();
    s_on = true;
    s_hz = hz;
}

static void tone_off(void)
{
    if (!s_on) return;
    pwm_stop(0x0);               /* leave the pin low: transistor off */
    s_on = false;
}

void beeper_init(void)
{
    uint32_t period = 1000000 / ALARM_HZ, duty = 0;
    const uint32_t pin = PIN_BUZZER;
    pwm_init(period, &duty, 1, &pin);
    pwm_stop(0x0);
    s_on = false;
}

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

void beeper_click(void)
{
    s_click_until_ms = now_ms() + 30;
    tone_on(CLICK_HZ);
}

void beeper_beeps(int n)
{
    for (int i = 0; i < n; i++) {
        tone_on(ALARM_HZ);
        vTaskDelay(pdMS_TO_TICKS(TONE_MS));
        tone_off();
        vTaskDelay(pdMS_TO_TICKS(GAP_MS));
    }
}

void beeper_pattern(int alarm_no)
{
    if (alarm_no == s_pattern) return;
    s_pattern = alarm_no;
    s_pat_start_ms = now_ms();
}

void beeper_clock_sound(int kind, int count)
{
    if (s_pattern) return;
    /* Never cut off a sound already playing: an hour's strikes last up to
     * 12 s, and the ticks that fall inside them are simply dropped. */
    if (s_seq_pos < s_seq_len) return;
    s_seq_len = 0;
    switch (kind) {
    case XY_SND_TICK:   /* tick: a very short, high click */
        s_seq[s_seq_len++] = (tone_t){ 3000, 4, 0 };
        break;
    case XY_SND_MINUTE: /* minute beep */
        s_seq[s_seq_len++] = (tone_t){ 2000, 120, 0 };
        break;
    case XY_SND_HALF:   /* half hour: one short beep */
        s_seq[s_seq_len++] = (tone_t){ 1500, 150, 0 };
        break;
    case XY_SND_HOUR:   /* hour: N long, low strikes, like a grandfather clock */
        if (count < 1) count = 1;
        if (count > 12) count = 12;
        for (int i = 0; i < count; i++) s_seq[s_seq_len++] = (tone_t){ 1000, 450, 550 };
        break;
    default:
        return;
    }
    s_seq_pos = 0;
    s_seq_tone_on = false;
    s_seq_next_ms = now_ms();
}

/* Returns true while a clock sound owns the beeper. */
static bool seq_tick(int64_t t)
{
    if (s_seq_pos >= s_seq_len) return false;
    if (t < s_seq_next_ms) return true;
    const tone_t *x = &s_seq[s_seq_pos];
    if (!s_seq_tone_on) {
        tone_on(x->hz);
        s_seq_tone_on = true;
        s_seq_next_ms = t + x->on_ms;
    } else {
        tone_off();
        s_seq_tone_on = false;
        s_seq_next_ms = t + x->off_ms;
        s_seq_pos++;
    }
    return true;
}

void beeper_tick(int64_t t)
{
    if (t < s_click_until_ms) return;            /* click in progress */
    if (s_pattern) {
        s_seq_len = 0;                           /* an alarm cancels clock sounds */
    } else {
        if (seq_tick(t)) return;
        tone_off();
        return;
    }
    int64_t cycle = (int64_t)s_pattern * (TONE_MS + GAP_MS) + PAUSE_MS;
    int64_t pos = (t - s_pat_start_ms) % cycle;
    bool on = pos < (int64_t)s_pattern * (TONE_MS + GAP_MS) && (pos % (TONE_MS + GAP_MS)) < TONE_MS;
    if (on) tone_on(ALARM_HZ); else tone_off();
}
