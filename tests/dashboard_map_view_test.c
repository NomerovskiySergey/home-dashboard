#include <assert.h>
#include <stdio.h>
#include "dashboard_map_view.h"

int main(void)
{
    dashboard_map_view_t v;
    dashboard_map_reset(&v);
    assert(v.zoom == 1 && v.x == 0 && v.y == -48);
    dashboard_map_zoom(&v, 2);
    assert(v.zoom == 2 && v.x == 106 && v.y == 32);
    int x, y;
    dashboard_map_project(&v, 212, 111, &x, &y);
    assert(x == 212 && y == 158);
    dashboard_map_pan(&v, 10000, 10000);
    assert(v.x == 0 && v.y == 0);
    dashboard_map_pan(&v, -10000, -10000);
    assert(v.x == 212 && v.y == 63);
    dashboard_map_zoom(&v, 3);
    assert(v.zoom == 3 && v.x >= 0 && v.x <= 282 && v.y <= 116);
    dashboard_map_project(&v, 0, 0, &x, &y);
    assert(!dashboard_map_visible(x, y));
    assert(dashboard_map_visible(0, 0));
    assert(dashboard_map_visible(423, 317));
    assert(!dashboard_map_visible(424, 318));
    dashboard_map_zoom(&v, 9);
    assert(v.zoom == 3);
    dashboard_map_zoom(&v, 1);
    assert(v.x == 0 && v.y == -48);
    dashboard_map_zoom(&v, 0);
    assert(v.zoom == 1);
    dashboard_map_reset(&v);
    for (int sy = 0; sy < 222; sy += 17) {
        for (int sx = 0; sx < 424; sx += 23) {
            int mx, my;
            dashboard_map_unproject(&v, sx, sy, &mx, &my);
            dashboard_map_project(&v, mx, my, &x, &y);
            assert(x == sx && y == sy);
        }
    }
    for (int zoom = 1; zoom <= 3; ++zoom) {
        dashboard_map_zoom(&v, zoom);
        for (int edge = -10000; edge <= 10000; edge += 10000) {
            dashboard_map_pan(&v, edge, -edge);
            for (int sy = 0; sy < DASH_MAP_VIEW_HEIGHT; ++sy) {
                for (int sx = 0; sx < DASH_MAP_WIDTH; ++sx) {
                    int mx, my;
                    dashboard_map_unproject(&v, sx, sy, &mx, &my);
                    assert(mx >= 0 && mx < DASH_MAP_WIDTH);
                    if (zoom > 1) assert(my >= 0 && my < DASH_MAP_HEIGHT);
                    dashboard_map_project(&v, mx, my, &x, &y);
                    assert(sx - x >= 0 && sx - x < zoom);
                    assert(sy - y >= 0 && sy - y < zoom);
                }
            }
        }
    }
    puts("map viewport tests passed");
    return 0;
}
