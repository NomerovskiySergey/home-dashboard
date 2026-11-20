#include "dashboard_pipboy.h"

#include "assets/ukraine_map_data.h"

static void line(lv_layer_t *layer, lv_draw_line_dsc_t *style,
                 int x1, int y1, int x2, int y2)
{
    style->p1 = (lv_point_precise_t){x1, y1};
    style->p2 = (lv_point_precise_t){x2, y2};
    lv_draw_line(layer, style);
}

void dashboard_pipboy_draw(lv_layer_t *layer, const lv_area_t *map,
                           lv_color_t frame, lv_color_t ink, lv_color_t marker,
                           lv_color_t wifi, lv_color_t alert)
{
    lv_draw_rect_dsc_t outline;
    lv_draw_rect_dsc_init(&outline);
    outline.bg_opa = LV_OPA_TRANSP;
    outline.border_color = frame;
    outline.border_width = 1;
    outline.radius = 10;
    lv_draw_rect(layer, &outline, &(lv_area_t){5, 5, 474, 474});

    lv_draw_line_dsc_t stroke;
    lv_draw_line_dsc_init(&stroke);
    stroke.width = 1;
    stroke.color = frame;
    // Instrument graduations stay in the gutter, clear of text and touch targets.
    for (int y = 65; y <= 455; y += 13) {
        const int length = (y - 65) % 52 == 0 ? 6 : 3;
        line(layer, &stroke, 7, y, 7 + length, y);
        line(layer, &stroke, 472 - length, y, 472, y);
    }
    line(layer, &stroke, 18, 56, 462, 56);
    line(layer, &stroke, 18, 469, 462, 469);

    // Location reticle and the three header instrument bars.
    stroke.color = ink;
    line(layer, &stroke, 25, 33, 39, 33);
    line(layer, &stroke, 32, 26, 32, 40);
    outline.radius = 0;
    outline.border_color = ink;
    lv_draw_rect(layer, &outline, &(lv_area_t){28, 29, 36, 37});
    for (int row = 0; row < 3; ++row) {
        line(layer, &stroke, 246, 28 + row * 5, 257, 28 + row * 5);
    }
    stroke.color = wifi;
    for (int bar = 0; bar < 4; ++bar) {
        line(layer, &stroke, 338 + bar * 3, 38, 338 + bar * 3, 35 - bar * 3);
    }

    stroke.color = ink;
    // Thermometer.
    line(layer, &stroke, 32, 84, 32, 96);
    line(layer, &stroke, 36, 84, 36, 96);
    line(layer, &stroke, 32, 84, 36, 84);
    outline.border_color = ink;
    lv_draw_rect(layer, &outline, &(lv_area_t){30, 95, 38, 101});
    // Local danger indicator follows the actual alert status.
    stroke.color = alert;
    line(layer, &stroke, 182, 84, 189, 98);
    line(layer, &stroke, 189, 98, 175, 98);
    line(layer, &stroke, 175, 98, 182, 84);
    line(layer, &stroke, 182, 89, 182, 93);
    line(layer, &stroke, 182, 95, 182, 96);

    // Map instrument symbol and outline.
    stroke.color = ink;
    line(layer, &stroke, 28, 166, 40, 166);
    line(layer, &stroke, 28, 166, 28, 177);
    line(layer, &stroke, 28, 177, 40, 177);
    line(layer, &stroke, 40, 177, 40, 166);
    line(layer, &stroke, 34, 164, 34, 179);
    outline.border_color = frame;
    lv_draw_rect(layer, &outline, map);

    // Dnipro reticle uses the same scaled map coordinates as the renderer.
    const int x = map->x1 + UA_MAP_DNIPRO_X * lv_area_get_width(map) / UA_MAP_WIDTH;
    const int y = map->y1 + UA_MAP_DNIPRO_Y * lv_area_get_height(map) / UA_MAP_HEIGHT;
    outline.border_color = marker;
    lv_draw_rect(layer, &outline, &(lv_area_t){x - 3, y - 3, x + 3, y + 3});
    stroke.color = marker;
    line(layer, &stroke, x - 7, y, x - 4, y);
    line(layer, &stroke, x + 4, y, x + 7, y);
    line(layer, &stroke, x, y - 7, x, y - 4);
    line(layer, &stroke, x, y + 4, x, y + 7);

    // Power gauge icon beside the schedule heading.
    stroke.color = ink;
    outline.border_color = ink;
    lv_draw_rect(layer, &outline, &(lv_area_t){28, 406, 40, 416});
    line(layer, &stroke, 32, 403, 36, 403);
    line(layer, &stroke, 34, 409, 34, 413);
    line(layer, &stroke, 32, 411, 36, 411);
}
