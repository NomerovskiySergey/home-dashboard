#include "dashboard_display_mode.h"

enum {
    NIGHT_START_MINUTE = 22 * 60,
    NIGHT_END_MINUTE = 7 * 60,
    NIGHT_BRIGHTNESS = 8,
    ECO_TIMEOUT_MS = 60 * 1000,
};

bool dashboard_display_is_night(const dashboard_display_mode_t *mode, int local_minute)
{
    return mode->night_enabled && local_minute >= 0 &&
           (local_minute >= NIGHT_START_MINUTE || local_minute < NIGHT_END_MINUTE);
}

int dashboard_display_brightness(const dashboard_display_mode_t *mode, int local_minute)
{
    if (mode->asleep) {
        return 0;
    }
    return dashboard_display_is_night(mode, local_minute) ? NIGHT_BRIGHTNESS : 100;
}

void dashboard_display_idle(dashboard_display_mode_t *mode, int64_t now_ms)
{
    mode->asleep = mode->eco_enabled && now_ms - mode->last_activity_ms >= ECO_TIMEOUT_MS;
}

bool dashboard_display_touch(dashboard_display_mode_t *mode, bool pressed, int64_t now_ms)
{
    if (!pressed) {
        const bool suppress = mode->suppress_touch;
        mode->suppress_touch = false;
        return suppress;
    }
    mode->last_activity_ms = now_ms;
    if (mode->asleep) {
        mode->asleep = false;
        mode->suppress_touch = true;
    }
    return mode->suppress_touch;
}

bool dashboard_display_alert(dashboard_display_mode_t *mode, bool fresh, char status, int64_t now_ms)
{
    if (!fresh || (status != 'R' && status != 'P' && status != 'N' && status != 'Y')) {
        return false;
    }
    const bool active = status == 'R' || status == 'P';
    const bool started = mode->alert_known && !mode->alert_active && active;
    mode->alert_known = true;
    mode->alert_active = active;
    if (started && mode->alert_wake_enabled && mode->asleep) {
        mode->asleep = false;
        mode->last_activity_ms = now_ms;
        return true;
    }
    return false;
}
