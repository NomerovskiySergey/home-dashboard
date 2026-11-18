#pragma once

#include "esp_err.h"
#include "lvgl.h"

esp_err_t dashboard_screen_create(lv_indev_t *touch, esp_err_t (*set_backlight)(int percent));
