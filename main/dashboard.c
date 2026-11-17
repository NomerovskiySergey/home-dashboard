#include "dashboard_data.h"
#include "dashboard_screen.h"
#include "dashboard_wifi.h"

#include <stdbool.h>
#include <stdlib.h>
#include <time.h>

#include "esp_board_manager_includes.h"
#include "esp_err.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"

static const char *const tag = "dashboard";

static void start_display(void)
{
    ESP_ERROR_CHECK(esp_board_manager_init_device_by_name("display_lcd"));

    dev_display_lcd_handles_t *lcd = NULL;
    dev_display_lcd_config_t *config = NULL;
    dev_lcd_touch_handles_t *touch = NULL;

    ESP_ERROR_CHECK(esp_board_manager_get_device_handle("display_lcd", (void **)&lcd));
    /* About 60 Hz at this panel's timings leaves bandwidth for Wi-Fi and PSRAM. */
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_set_pclk(lcd->panel_handle, 16000000));
    ESP_ERROR_CHECK(esp_board_manager_get_device_config("display_lcd", (void **)&config));
    ESP_ERROR_CHECK(esp_board_manager_init_device_by_name("lcd_touch"));
    ESP_ERROR_CHECK(esp_board_manager_get_device_handle("lcd_touch", (void **)&touch));

    const lvgl_port_cfg_t port_config = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&port_config));

    const bool avoid_tearing = config->sub_cfg.rgb_3wire_spi.rgb_panel_config.num_fbs > 1;
    const lvgl_port_display_cfg_t display_config = {
        .io_handle = lcd->io_handle,
        .panel_handle = lcd->panel_handle,
        .buffer_size = config->lcd_width * config->lcd_height,
        .hres = config->lcd_width,
        .vres = config->lcd_height,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = {
            .swap_xy = config->swap_xy,
            .mirror_x = config->mirror_x,
            .mirror_y = config->mirror_y,
        },
        .flags = {
            .buff_spiram = true,
            .direct_mode = avoid_tearing,
            .swap_bytes = config->frame_format == DEV_DISPLAY_LCD_FRAME_FORMAT_RGB565_BE,
        },
    };
    const lvgl_port_display_rgb_cfg_t rgb_config = {
        .flags = {
            .bb_mode = config->sub_cfg.rgb_3wire_spi.rgb_panel_config.bounce_buffer_size_px > 0,
            .avoid_tearing = avoid_tearing,
        },
    };
    lv_display_t *display = lvgl_port_add_disp_rgb(&display_config, &rgb_config);
    ESP_ERROR_CHECK(display ? ESP_OK : ESP_FAIL);

    const lvgl_port_touch_cfg_t touch_config = {
        .disp = display,
        .handle = touch->touch_handle,
    };
    ESP_ERROR_CHECK(lvgl_port_add_touch(&touch_config) ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(esp_board_manager_init_device_by_name("lcd_brightness"));
}

void app_main(void)
{
    setenv("TZ", "EET-2EEST,M3.5.0/3,M10.5.0/4", 1);
    tzset();
    start_display();
    ESP_ERROR_CHECK(dashboard_wifi_start());
    ESP_ERROR_CHECK_WITHOUT_ABORT(dashboard_data_start());
    if (!lvgl_port_lock(0)) {
        ESP_ERROR_CHECK(ESP_ERR_TIMEOUT);
    }
    const esp_err_t result = dashboard_screen_create();
    lvgl_port_unlock();
    ESP_ERROR_CHECK(result);
    ESP_LOGI(tag, "Dashboard ready");
}
