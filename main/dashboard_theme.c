#include "dashboard_theme.h"

static const dashboard_theme_t themes[] = {
    {
        .id = DASH_THEME_STANDARD,
        .name = "Стандартна",
        .panel_radius = 12,
        .button_radius = 10,
        .day.colors = {
            [DASH_COLOR_BACKGROUND] = 0x101820,
            [DASH_COLOR_PANEL] = 0x1B2632,
            [DASH_COLOR_PANEL_BORDER] = 0x1B2632,
            [DASH_COLOR_BUTTON] = 0x30465B,
            [DASH_COLOR_SELECTED] = 0x42617B,
            [DASH_COLOR_TEXT] = 0xF4F7FA,
            [DASH_COLOR_MUTED] = 0xAFBECC,
            [DASH_COLOR_CLOCK] = 0xF4F7FA,
            [DASH_COLOR_TEMPERATURE] = 0xA8CFEC,
            [DASH_COLOR_WARNING] = 0xF8C576,
            [DASH_COLOR_ALARM] = 0xE87A74,
            [DASH_COLOR_CONNECTED] = 0xACEBC9,
            [DASH_COLOR_MAP_BORDER] = 0x263848,
            [DASH_COLOR_MAP_CLEAR] = 0x436078,
            [DASH_COLOR_MAP_ALARM] = 0xBE504C,
            [DASH_COLOR_MAP_WARNING] = 0xBE944A,
            [DASH_COLOR_MAP_MARKER] = 0xF4D17E,
            [DASH_COLOR_TRACK] = 0x3E556A,
            [DASH_COLOR_TRACK_UNKNOWN] = 0x303843,
            [DASH_COLOR_OUTAGE] = 0xF8C576,
            [DASH_COLOR_POSSIBLE_OUTAGE] = 0xB92F83,
        },
        .night.colors = {
            [DASH_COLOR_BACKGROUND] = 0x070A0F,
            [DASH_COLOR_PANEL] = 0x101820,
            [DASH_COLOR_PANEL_BORDER] = 0x1B2632,
            [DASH_COLOR_BUTTON] = 0x30465B,
            [DASH_COLOR_SELECTED] = 0x42617B,
            [DASH_COLOR_TEXT] = 0xF4F7FA,
            [DASH_COLOR_MUTED] = 0xAFBECC,
            [DASH_COLOR_CLOCK] = 0xF4F7FA,
            [DASH_COLOR_TEMPERATURE] = 0xA8CFEC,
            [DASH_COLOR_WARNING] = 0xF8C576,
            [DASH_COLOR_ALARM] = 0xE87A74,
            [DASH_COLOR_CONNECTED] = 0xACEBC9,
            [DASH_COLOR_MAP_BORDER] = 0x1C2936,
            [DASH_COLOR_MAP_CLEAR] = 0x304759,
            [DASH_COLOR_MAP_ALARM] = 0xBE504C,
            [DASH_COLOR_MAP_WARNING] = 0xBE944A,
            [DASH_COLOR_MAP_MARKER] = 0xF4D17E,
            [DASH_COLOR_TRACK] = 0x3E556A,
            [DASH_COLOR_TRACK_UNKNOWN] = 0x303843,
            [DASH_COLOR_OUTAGE] = 0xF8C576,
            [DASH_COLOR_POSSIBLE_OUTAGE] = 0xB92F83,
        },
    },
    {
        .id = DASH_THEME_STALKER,
        .home_layout = DASH_HOME_PDA,
        .name = "S.T.A.L.K.E.R.",
        .panel_radius = 2,
        .button_radius = 2,
        .panel_border_width = 1,
        .day.colors = {
            [DASH_COLOR_BACKGROUND] = 0x161811,
            [DASH_COLOR_PANEL] = 0x282C20,
            [DASH_COLOR_PANEL_BORDER] = 0x555C39,
            [DASH_COLOR_BUTTON] = 0x3B422A,
            [DASH_COLOR_SELECTED] = 0x58623A,
            [DASH_COLOR_TEXT] = 0xEAE7D3,
            [DASH_COLOR_MUTED] = 0xBFC4A6,
            [DASH_COLOR_CLOCK] = 0xF5CD7B,
            [DASH_COLOR_TEMPERATURE] = 0xE0E4BC,
            [DASH_COLOR_WARNING] = 0xF4CD79,
            [DASH_COLOR_ALARM] = 0xFF8D80,
            [DASH_COLOR_CONNECTED] = 0xD2DC9E,
            [DASH_COLOR_MAP_BORDER] = 0x23271B,
            [DASH_COLOR_MAP_CLEAR] = 0x5A6540,
            [DASH_COLOR_MAP_ALARM] = 0xD0584E,
            [DASH_COLOR_MAP_WARNING] = 0xC29C4C,
            [DASH_COLOR_MAP_MARKER] = 0xF6DA8E,
            [DASH_COLOR_TRACK] = 0x596542,
            [DASH_COLOR_TRACK_UNKNOWN] = 0x3E4334,
            [DASH_COLOR_OUTAGE] = 0xEFC072,
            [DASH_COLOR_POSSIBLE_OUTAGE] = 0xD17CB1,
        },
        .night.colors = {
            [DASH_COLOR_BACKGROUND] = 0x0C0E09,
            [DASH_COLOR_PANEL] = 0x1B2015,
            [DASH_COLOR_PANEL_BORDER] = 0x484F33,
            [DASH_COLOR_BUTTON] = 0x2F3822,
            [DASH_COLOR_SELECTED] = 0x4C5731,
            [DASH_COLOR_TEXT] = 0xEAE7D3,
            [DASH_COLOR_MUTED] = 0xBFC4A6,
            [DASH_COLOR_CLOCK] = 0xF5CD7B,
            [DASH_COLOR_TEMPERATURE] = 0xE0E4BC,
            [DASH_COLOR_WARNING] = 0xF4CD79,
            [DASH_COLOR_ALARM] = 0xFF8D80,
            [DASH_COLOR_CONNECTED] = 0xD2DC9E,
            [DASH_COLOR_MAP_BORDER] = 0x151910,
            [DASH_COLOR_MAP_CLEAR] = 0x495436,
            [DASH_COLOR_MAP_ALARM] = 0xD0584E,
            [DASH_COLOR_MAP_WARNING] = 0xC29C4C,
            [DASH_COLOR_MAP_MARKER] = 0xF6DA8E,
            [DASH_COLOR_TRACK] = 0x465437,
            [DASH_COLOR_TRACK_UNKNOWN] = 0x30382A,
            [DASH_COLOR_OUTAGE] = 0xEFC072,
            [DASH_COLOR_POSSIBLE_OUTAGE] = 0xD17CB1,
        },
    },
};

const dashboard_theme_t *dashboard_theme_catalog(size_t *count)
{
    *count = sizeof(themes) / sizeof(themes[0]);
    return themes;
}

const dashboard_theme_t *dashboard_theme_find(uint8_t id)
{
    size_t count;
    const dashboard_theme_t *catalog = dashboard_theme_catalog(&count);
    for (size_t i = 0; i < count; ++i) {
        if (catalog[i].id == id) {
            return &catalog[i];
        }
    }
    return &catalog[0];
}
