#pragma once
#include <stdbool.h>
#include <stdint.h>

enum {
    DASH_MAP_WIDTH = 424, DASH_MAP_HEIGHT = 222,
    DASH_MAP_LEFT = 52, DASH_MAP_TOP = 2,
    DASH_MAP_CONTENT_WIDTH = 320, DASH_MAP_CONTENT_HEIGHT = 218,
    DASH_MAP_VIEW_WIDTH = 480, DASH_MAP_VIEW_HEIGHT = 480,
    DASH_MAP_FIT_HEIGHT = 327, DASH_MAP_FIT_TOP = 76,
    DASH_MAP_ICON_SIZE = 24,
};
typedef enum {
    DASH_MAP_ICON_ROCKET, DASH_MAP_ICON_UAV, DASH_MAP_ICON_AIRCRAFT,
    DASH_MAP_ICON_BOMB, DASH_MAP_ICON_SHIELD, DASH_MAP_ICON_UNKNOWN,
    DASH_MAP_ICON_COUNT,
} dashboard_map_icon_t;

/* Fixed transform crops empty raster gutters only; complete geography stays visible. */
void dashboard_map_project(int x, int y, int *sx, int *sy);
bool dashboard_map_source(int sx, int sy, int *x, int *y);
unsigned dashboard_map_categories(uint16_t types, dashboard_map_icon_t icons[DASH_MAP_ICON_COUNT]);
void dashboard_map_group_size(unsigned count, int *width, int *height);
