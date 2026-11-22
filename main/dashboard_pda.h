#pragma once

#include "lvgl.h"

void dashboard_pda_draw(lv_layer_t *layer, const lv_area_t *map,
                        lv_color_t frame, lv_color_t ink, lv_color_t marker,
                        lv_color_t alert, int marker_x, int marker_y);
