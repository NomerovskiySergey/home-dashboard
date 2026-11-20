#pragma once

#include <stddef.h>
#include <stdint.h>

enum {
    DASH_THEME_STANDARD = 0,
    DASH_THEME_STALKER = 1,
    DASH_THEME_FALLOUT = 2,
};

typedef enum {
    DASH_COLOR_BACKGROUND,
    DASH_COLOR_PANEL,
    DASH_COLOR_PANEL_BORDER,
    DASH_COLOR_BUTTON,
    DASH_COLOR_SELECTED,
    DASH_COLOR_TEXT,
    DASH_COLOR_MUTED,
    DASH_COLOR_CLOCK,
    DASH_COLOR_TEMPERATURE,
    DASH_COLOR_WARNING,
    DASH_COLOR_ALARM,
    DASH_COLOR_CONNECTED,
    DASH_COLOR_MAP_BORDER,
    DASH_COLOR_MAP_CLEAR,
    DASH_COLOR_MAP_ALARM,
    DASH_COLOR_MAP_WARNING,
    DASH_COLOR_MAP_MARKER,
    DASH_COLOR_TRACK,
    DASH_COLOR_TRACK_UNKNOWN,
    DASH_COLOR_OUTAGE,
    DASH_COLOR_POSSIBLE_OUTAGE,
    DASH_COLOR_COUNT,
} dashboard_color_t;

typedef struct {
    uint32_t colors[DASH_COLOR_COUNT];
} dashboard_palette_t;

typedef enum {
    DASH_HOME_STANDARD,
    DASH_HOME_PDA,
    DASH_HOME_PIPBOY,
} dashboard_home_layout_t;

typedef struct {
    uint8_t id;
    dashboard_home_layout_t home_layout;
    const char *name;
    uint8_t panel_radius;
    uint8_t button_radius;
    uint8_t panel_border_width;
    dashboard_palette_t day;
    dashboard_palette_t night;
} dashboard_theme_t;

const dashboard_theme_t *dashboard_theme_catalog(size_t *count);
const dashboard_theme_t *dashboard_theme_find(uint8_t id);
