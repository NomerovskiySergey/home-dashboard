#pragma once

#include <stdbool.h>

enum { DASH_MAP_WIDTH = 424, DASH_MAP_HEIGHT = 222, DASH_MAP_VIEW_HEIGHT = 318 };
typedef struct { int zoom, x, y; } dashboard_map_view_t;

void dashboard_map_reset(dashboard_map_view_t *view);
void dashboard_map_zoom(dashboard_map_view_t *view, int zoom);
void dashboard_map_pan(dashboard_map_view_t *view, int dx, int dy);
void dashboard_map_project(const dashboard_map_view_t *view, int x, int y, int *sx, int *sy);
void dashboard_map_unproject(const dashboard_map_view_t *view, int sx, int sy, int *x, int *y);
bool dashboard_map_visible(int x, int y);
