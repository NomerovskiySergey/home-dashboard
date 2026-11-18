#include <assert.h>
#include <stdio.h>

#include "dashboard_display_mode.h"

enum { SECOND_MS = 1000 };

int main(void)
{
    dashboard_display_mode_t mode = {0};
    assert(dashboard_display_brightness(&mode, 22 * 60) == 100);
    mode.night_enabled = true;
    assert(dashboard_display_brightness(&mode, -1) == 100);
    assert(dashboard_display_brightness(&mode, 21 * 60 + 59) == 100);
    assert(dashboard_display_brightness(&mode, 22 * 60) == 8);
    assert(dashboard_display_brightness(&mode, 0) == 8);
    assert(dashboard_display_brightness(&mode, 6 * 60 + 59) == 8);
    assert(dashboard_display_brightness(&mode, 7 * 60) == 100);

    dashboard_display_idle(&mode, 60 * SECOND_MS);
    assert(!mode.asleep);
    mode.eco_enabled = true;
    assert(!dashboard_display_touch(&mode, true, 100 * SECOND_MS));
    dashboard_display_idle(&mode, 159 * SECOND_MS);
    assert(!mode.asleep);
    dashboard_display_idle(&mode, 160 * SECOND_MS);
    assert(dashboard_display_brightness(&mode, 0) == 0);
    assert(dashboard_display_touch(&mode, true, 161 * SECOND_MS));
    assert(dashboard_display_brightness(&mode, 0) == 8);
    assert(dashboard_display_touch(&mode, true, 162 * SECOND_MS));
    assert(dashboard_display_touch(&mode, false, 163 * SECOND_MS));
    assert(!dashboard_display_touch(&mode, true, 164 * SECOND_MS));
    dashboard_display_idle(&mode, 224 * SECOND_MS);
    assert(mode.asleep);

    assert(!dashboard_display_alert(&mode, true, 'N', 225 * SECOND_MS));
    assert(!dashboard_display_alert(&mode, true, 'R', 226 * SECOND_MS));
    assert(mode.asleep);
    mode.alert_wake_enabled = true;
    assert(!dashboard_display_alert(&mode, true, 'R', 227 * SECOND_MS));
    assert(!dashboard_display_alert(&mode, true, 'N', 228 * SECOND_MS));
    assert(!dashboard_display_alert(&mode, false, 'R', 229 * SECOND_MS));
    assert(mode.asleep);
    assert(dashboard_display_alert(&mode, true, 'R', 230 * SECOND_MS));
    assert(dashboard_display_brightness(&mode, 0) == 8);
    dashboard_display_idle(&mode, 290 * SECOND_MS);
    assert(!dashboard_display_alert(&mode, false, 'N', 291 * SECOND_MS));
    assert(!dashboard_display_alert(&mode, true, 'R', 292 * SECOND_MS));
    assert(mode.asleep);
    assert(!dashboard_display_alert(&mode, true, 'N', 293 * SECOND_MS));
    assert(dashboard_display_alert(&mode, true, 'P', 294 * SECOND_MS));
    dashboard_display_idle(&mode, 354 * SECOND_MS);
    assert(!dashboard_display_alert(&mode, true, 'R', 355 * SECOND_MS));

    dashboard_display_mode_t first_sample = {.asleep = true, .alert_wake_enabled = true};
    assert(!dashboard_display_alert(&first_sample, true, 'R', 0));
    mode.eco_enabled = false;
    dashboard_display_idle(&mode, 356 * SECOND_MS);
    assert(dashboard_display_brightness(&mode, 8 * 60) == 100);
    puts("Display mode tests passed");
    return 0;
}
