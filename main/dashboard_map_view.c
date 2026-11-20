#include "dashboard_map_view.h"

void dashboard_map_project(int x, int y, int *sx, int *sy)
{
    *sx = ((2 * (x - DASH_MAP_LEFT) + 1) * DASH_MAP_VIEW_WIDTH) / (2 * DASH_MAP_CONTENT_WIDTH);
    *sy = DASH_MAP_FIT_TOP + ((2 * (y - DASH_MAP_TOP) + 1) * DASH_MAP_VIEW_WIDTH) / (2 * DASH_MAP_CONTENT_WIDTH);
}

bool dashboard_map_source(int sx, int sy, int *x, int *y)
{
    if (sx < 0 || sx >= DASH_MAP_VIEW_WIDTH || sy < DASH_MAP_FIT_TOP ||
        sy >= DASH_MAP_FIT_TOP + DASH_MAP_FIT_HEIGHT) return false;
    *x = DASH_MAP_LEFT + ((2 * sx + 1) * DASH_MAP_CONTENT_WIDTH) / (2 * DASH_MAP_VIEW_WIDTH);
    *y = DASH_MAP_TOP + ((2 * (sy - DASH_MAP_FIT_TOP) + 1) * DASH_MAP_CONTENT_WIDTH) / (2 * DASH_MAP_VIEW_WIDTH);
    return true;
}

unsigned dashboard_map_categories(uint16_t types, dashboard_map_icon_t icons[DASH_MAP_ICON_COUNT])
{
    unsigned n = 0;
    if (types & ((1u<<4)|(1u<<5)|(1u<<6))) icons[n++] = DASH_MAP_ICON_ROCKET;
    if (types & (1u<<7)) icons[n++] = DASH_MAP_ICON_UAV;
    if (types & ((1u<<1)|(1u<<2)|(1u<<3))) icons[n++] = DASH_MAP_ICON_AIRCRAFT;
    if (types & (1u<<8)) icons[n++] = DASH_MAP_ICON_BOMB;
    if (types & (1u<<9)) icons[n++] = DASH_MAP_ICON_SHIELD;
    if (types & 1u) icons[n++] = DASH_MAP_ICON_UNKNOWN;
    return n;
}

void dashboard_map_group_size(unsigned count, int *width, int *height)
{
    if (count > DASH_MAP_ICON_COUNT) count = DASH_MAP_ICON_COUNT;
    *width = count ? (int)(count < 3 ? count : 3) * DASH_MAP_ICON_SIZE + 6 : 0;
    *height = count ? (int)((count + 2) / 3) * DASH_MAP_ICON_SIZE + 6 : 0;
}
