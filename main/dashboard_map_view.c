#include "dashboard_map_view.h"

static int clamp(int value, int minimum, int maximum)
{ return value < minimum ? minimum : value > maximum ? maximum : value; }

static void constrain(dashboard_map_view_t *v)
{
    const int width = (DASH_MAP_WIDTH + v->zoom - 1) / v->zoom;
    const int height = (DASH_MAP_VIEW_HEIGHT + v->zoom - 1) / v->zoom;
    v->x = clamp(v->x, 0, DASH_MAP_WIDTH - width);
    v->y = height > DASH_MAP_HEIGHT ? (DASH_MAP_HEIGHT - height) / 2
                                  : clamp(v->y, 0, DASH_MAP_HEIGHT - height);
}

void dashboard_map_reset(dashboard_map_view_t *v)
{
    *v = (dashboard_map_view_t){ .zoom = 1 };
    constrain(v);
}

void dashboard_map_zoom(dashboard_map_view_t *v, int zoom)
{
    zoom = clamp(zoom, 1, 3);
    v->x += DASH_MAP_WIDTH / (2 * v->zoom) - DASH_MAP_WIDTH / (2 * zoom);
    v->y += DASH_MAP_VIEW_HEIGHT / (2 * v->zoom) - DASH_MAP_VIEW_HEIGHT / (2 * zoom);
    v->zoom = zoom;
    constrain(v);
}

void dashboard_map_pan(dashboard_map_view_t *v, int dx, int dy)
{
    v->x -= dx / v->zoom;
    v->y -= dy / v->zoom;
    constrain(v);
}

void dashboard_map_project(const dashboard_map_view_t *v, int x, int y, int *sx, int *sy)
{ *sx = (x - v->x) * v->zoom; *sy = (y - v->y) * v->zoom; }

void dashboard_map_unproject(const dashboard_map_view_t *v, int sx, int sy, int *x, int *y)
{ *x = v->x + sx / v->zoom; *y = v->y + sy / v->zoom; }

bool dashboard_map_visible(int x, int y)
{ return x >= 0 && x < DASH_MAP_WIDTH && y >= 0 && y < DASH_MAP_VIEW_HEIGHT; }
