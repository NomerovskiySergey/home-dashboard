#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool night_enabled;
    bool eco_enabled;
    bool alert_wake_enabled;
    bool asleep;
    bool suppress_touch;
    bool alert_known;
    bool alert_active;
    int64_t last_activity_ms;
} dashboard_display_mode_t;

bool dashboard_display_is_night(const dashboard_display_mode_t *mode, int local_minute);
int dashboard_display_brightness(const dashboard_display_mode_t *mode, int local_minute);
void dashboard_display_idle(dashboard_display_mode_t *mode, int64_t now_ms);
bool dashboard_display_touch(dashboard_display_mode_t *mode, bool pressed, int64_t now_ms);
bool dashboard_display_alert(dashboard_display_mode_t *mode, bool fresh, char status, int64_t now_ms);
