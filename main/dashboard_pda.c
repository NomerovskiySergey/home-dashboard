#include "dashboard_pda.h"

#include "assets/ukraine_map_data.h"

typedef struct {
    int8_t x1;
    int8_t y1;
    int8_t x2;
    int8_t y2;
} icon_line_t;

static const icon_line_t crosshair[] = {
    {3, 3, 11, 3}, {11, 3, 11, 11}, {11, 11, 3, 11}, {3, 11, 3, 3},
    {7, 0, 7, 5}, {7, 9, 7, 14}, {0, 7, 5, 7}, {9, 7, 14, 7},
};

static const icon_line_t sliders[] = {
    {1, 3, 13, 3}, {1, 7, 13, 7}, {1, 11, 13, 11},
    {4, 1, 4, 5}, {9, 5, 9, 9}, {6, 9, 6, 13},
};

static const icon_line_t signal[] = {
    {0, 5, 4, 2}, {4, 2, 10, 2}, {10, 2, 14, 5},
    {3, 8, 6, 6}, {6, 6, 8, 6}, {8, 6, 11, 8},
    {5, 11, 7, 10}, {7, 10, 9, 11}, {7, 13, 8, 13},
};

static const icon_line_t thermometer[] = {
    {5, 1, 9, 1}, {5, 1, 5, 9}, {9, 1, 9, 9},
    {5, 9, 3, 11}, {3, 11, 5, 14}, {5, 14, 9, 14},
    {9, 14, 11, 11}, {11, 11, 9, 9}, {7, 5, 7, 12},
};

static const icon_line_t warning[] = {
    {7, 1, 14, 13}, {14, 13, 0, 13}, {0, 13, 7, 1},
    {7, 5, 7, 8}, {7, 10, 8, 10},
};

static const icon_line_t radar[] = {
    {10, 1, 4, 1}, {4, 1, 1, 4}, {1, 4, 1, 10}, {1, 10, 4, 13},
    {4, 13, 10, 13}, {10, 13, 13, 10}, {13, 10, 13, 5},
    {7, 7, 13, 1}, {7, 7, 8, 7},
};

static const icon_line_t bolt[] = {
    {8, 0, 1, 8}, {1, 8, 6, 8}, {6, 8, 5, 14},
    {5, 14, 13, 6}, {13, 6, 8, 6}, {8, 6, 8, 0},
};

static void line(lv_layer_t *layer, lv_draw_line_dsc_t *style, int x1, int y1, int x2, int y2)
{
    style->p1 = (lv_point_precise_t){x1, y1};
    style->p2 = (lv_point_precise_t){x2, y2};
    lv_draw_line(layer, style);
}

static void icon(lv_layer_t *layer, int x, int y, lv_color_t color,
                 const icon_line_t *lines, size_t count)
{
    lv_draw_line_dsc_t style;
    lv_draw_line_dsc_init(&style);
    style.color = color;
    style.width = 1;
    for (size_t i = 0; i < count; ++i) {
        line(layer, &style, x + lines[i].x1, y + lines[i].y1,
             x + lines[i].x2, y + lines[i].y2);
    }
}

#define DRAW_ICON(layer, x, y, color, shape) \
    icon(layer, x, y, color, shape, sizeof(shape) / sizeof(*shape))

void dashboard_pda_draw(lv_layer_t *layer, const lv_area_t *map,
                        lv_color_t frame, lv_color_t ink, lv_color_t marker,
                        lv_color_t wifi, lv_color_t alert)
{
    lv_draw_rect_dsc_t rim;
    lv_draw_rect_dsc_init(&rim);
    rim.bg_opa = LV_OPA_TRANSP;
    rim.border_color = frame;
    rim.border_width = 1;
    lv_draw_rect(layer, &rim, &(lv_area_t){5, 5, 474, 474});

    lv_draw_line_dsc_t stroke;
    lv_draw_line_dsc_init(&stroke);
    stroke.width = 1;
    stroke.color = frame;
    stroke.opa = LV_OPA_60;
    for (int y = 13; y < 470; y += 9) {
        line(layer, &stroke, 8, y + 3, 12, y);
        line(layer, &stroke, 467, y + 3, 471, y);
    }

    stroke.opa = LV_OPA_COVER;
    stroke.color = marker;
    const int x[] = {map->x1, map->x2};
    const int y[] = {map->y1, map->y2};
    for (int row = 0; row < 2; ++row) {
        const int dy = row == 0 ? 9 : -9;
        for (int column = 0; column < 2; ++column) {
            const int dx = column == 0 ? 9 : -9;
            line(layer, &stroke, x[column], y[row], x[column] + dx, y[row]);
            line(layer, &stroke, x[column], y[row], x[column], y[row] + dy);
        }
    }

    const int dnipro_x = map->x1 + UA_MAP_DNIPRO_X * lv_area_get_width(map) / UA_MAP_WIDTH;
    const int dnipro_y = map->y1 + UA_MAP_DNIPRO_Y * lv_area_get_height(map) / UA_MAP_HEIGHT;
    rim.border_color = marker;
    lv_draw_rect(layer, &rim, &(lv_area_t){dnipro_x - 4, dnipro_y - 4, dnipro_x + 4, dnipro_y + 4});
    line(layer, &stroke, dnipro_x - 1, dnipro_y, dnipro_x + 1, dnipro_y);

    DRAW_ICON(layer, 24, 26, ink, crosshair);
    DRAW_ICON(layer, 246, 26, ink, sliders);
    DRAW_ICON(layer, 338, 26, wifi, signal);
    DRAW_ICON(layer, 27, 86, ink, thermometer);
    DRAW_ICON(layer, 175, 86, alert, warning);
    DRAW_ICON(layer, 28, 165, ink, radar);
    DRAW_ICON(layer, 28, 405, ink, bolt);
    line(layer, &stroke, 442, 408, 446, 412);
    line(layer, &stroke, 446, 412, 442, 416);
}
