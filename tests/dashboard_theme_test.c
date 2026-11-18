#include <assert.h>
#include <string.h>

#include "dashboard_theme.h"

int main(void)
{
    const dashboard_theme_t *standard = dashboard_theme_find(DASH_THEME_STANDARD);
    const dashboard_theme_t *stalker = dashboard_theme_find(DASH_THEME_STALKER);

    assert(standard->id == 0);
    assert(stalker->id == 1);
    assert(strcmp(standard->name, "Стандартна") == 0);
    assert(strcmp(stalker->name, "S.T.A.L.K.E.R.") == 0);
    assert(dashboard_theme_find(255) == standard);
    assert(standard->home_layout == DASH_HOME_STANDARD);
    assert(stalker->home_layout == DASH_HOME_PDA);
    assert(dashboard_theme_find(255)->home_layout == DASH_HOME_STANDARD);

    size_t count = 0;
    const dashboard_theme_t *themes = dashboard_theme_catalog(&count);
    assert(count >= 2);
    for (size_t i = 0; i < count; ++i) {
        assert(dashboard_theme_find(themes[i].id) == &themes[i]);
        for (size_t j = 0; j < i; ++j) {
            assert(themes[i].id != themes[j].id);
        }

        const dashboard_palette_t *day = &themes[i].day;
        const dashboard_palette_t *night = &themes[i].night;
        assert(day->colors[DASH_COLOR_BACKGROUND] != night->colors[DASH_COLOR_BACKGROUND]);
        for (size_t mode = 0; mode < 2; ++mode) {
            const dashboard_palette_t *palette = mode == 0 ? day : night;
            assert(palette->colors[DASH_COLOR_MAP_ALARM] != palette->colors[DASH_COLOR_MAP_WARNING]);
            assert(palette->colors[DASH_COLOR_MAP_CLEAR] != palette->colors[DASH_COLOR_MAP_ALARM]);
            assert(palette->colors[DASH_COLOR_MAP_CLEAR] != palette->colors[DASH_COLOR_MAP_WARNING]);
            assert(palette->colors[DASH_COLOR_OUTAGE] != palette->colors[DASH_COLOR_POSSIBLE_OUTAGE]);
            assert(palette->colors[DASH_COLOR_TRACK] != palette->colors[DASH_COLOR_OUTAGE]);
            assert(palette->colors[DASH_COLOR_TRACK] != palette->colors[DASH_COLOR_POSSIBLE_OUTAGE]);
        }
    }
    assert(standard->day.colors[DASH_COLOR_PANEL] != stalker->day.colors[DASH_COLOR_PANEL]);
}
