#include "xy_sound.h"

void xy_sound_init(xy_sound_t *s) { s->last_s = -1; }

xy_snd_ev_t xy_sound_tick(xy_sound_t *s, const xy_settings_t *cfg, int64_t now_s,
                          int h, int m, int sec, bool trusted, bool alarm_sounding)
{
    xy_snd_ev_t ev = { XY_SND_NONE, 0 };
    if (now_s == s->last_s) return ev;
    s->last_s = now_s;
    if (alarm_sounding) return ev;
    if (cfg->quiet && (!trusted || xy_sound_quiet_now(cfg, h * 60 + m))) return ev;
    if (sec == 0 && cfg->chime && trusted) {
        if (m == 0) {
            ev.kind = XY_SND_HOUR;
            ev.count = h % 12 ? h % 12 : 12;     /* grandfather clock: 12-hour strikes */
            return ev;
        }
        if (m == 30) { ev.kind = XY_SND_HALF; return ev; }
    }
    if (cfg->tick) ev.kind = (sec == 0 && cfg->tick_minute) ? XY_SND_MINUTE : XY_SND_TICK;
    return ev;
}

bool xy_sound_quiet_now(const xy_settings_t *cfg, int m)
{
    int a = cfg->quiet_start_min, b = cfg->quiet_end_min;
    if (!cfg->quiet || a == b) return false;
    return a < b ? (m >= a && m < b) : (m >= a || m < b);
}
