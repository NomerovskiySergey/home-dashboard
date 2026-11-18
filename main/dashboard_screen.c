#include "dashboard_screen.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "assets/ukraine_map_data.h"
#include "dashboard_data.h"
#include "dashboard_display_mode.h"
#include "dashboard_pda.h"
#include "dashboard_theme.h"
#include "dashboard_wifi.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"

enum {
    SCREEN_WIDTH = 480,
    MARGIN = 16,
    CARD_WIDTH = SCREEN_WIDTH - MARGIN * 2,
    MINUTES_PER_DAY = 24 * 60,
    DNIPRO_REGION = 12,
    SETTING_NIGHT = 1 << 0,
    SETTING_ECO = 1 << 1,
    SETTING_ALERT_WAKE = 1 << 2,
};

static const char display_settings_namespace[] = "dash_display";
_Static_assert(sizeof(display_settings_namespace) <= NVS_NS_NAME_MAX_SIZE,
               "Display settings namespace exceeds the NVS limit");

extern const uint8_t ubuntu_medium_start[] asm("_binary_Ubuntu_Medium_ttf_start");
extern const uint8_t ubuntu_medium_end[] asm("_binary_Ubuntu_Medium_ttf_end");
extern const uint8_t ubuntu_mono_start[] asm("_binary_UbuntuMono_Regular_ttf_start");
extern const uint8_t ubuntu_mono_end[] asm("_binary_UbuntuMono_Regular_ttf_end");

static lv_font_t *small_font;
static lv_font_t *body_font;
static lv_font_t *title_font;
static const lv_font_t *pda_label_font;
static const lv_font_t *pda_value_font;
static const lv_font_t *pda_clock_font;

static struct {
    lv_obj_t *header;
    lv_obj_t *city;
    lv_obj_t *settings_key;
    lv_obj_t *wifi_key;
    lv_obj_t *weather_card;
    lv_obj_t *weather_heading;
    lv_obj_t *alert_card;
    lv_obj_t *alert_heading;
    lv_obj_t *map_card;
    lv_obj_t *map_heading;
    lv_obj_t *map_marker;
    lv_obj_t *power_card;
    lv_obj_t *power_heading;
} home;
static lv_obj_t *home_screen;
static lv_obj_t *home_clock;
static lv_obj_t *home_wifi_status;
static lv_obj_t *schedule_screen;
static lv_obj_t *wifi_screen;
static lv_obj_t *display_settings_screen;
static lv_obj_t *display_settings_note;
static lv_obj_t *theme_screen;
static lv_obj_t *theme_name;
static lv_obj_t *theme_note;
static lv_obj_t *selected_theme_row;
static lv_obj_t *wifi_status;
static lv_obj_t *wifi_instructions;
static lv_obj_t *wifi_change_button;
static lv_draw_buf_t map_buffer;
static lv_obj_t *map_canvas;
static lv_obj_t *weather_value;
static lv_obj_t *weather_note;
static lv_obj_t *alert_value;
static lv_obj_t *alert_note;

static lv_obj_t *energy_home_track;
static lv_obj_t *energy_home_note;
static lv_obj_t *energy_detail_track;
static lv_obj_t *energy_detail_date;
static lv_obj_t *energy_detail_note;
static lv_obj_t *energy_detail_age;
static lv_obj_t *energy_intervals;
static bool energy_show_tomorrow;
static lv_style_t color_styles[DASH_COLOR_COUNT];
static const dashboard_theme_t *theme;
static const dashboard_palette_t *palette;
static dashboard_display_mode_t display_mode;
static nvs_handle_t display_settings;
static lv_indev_read_cb_t read_touch;
static esp_err_t (*apply_backlight)(int percent);

static void update_home_wifi(dashboard_wifi_state_t state);
static void update_alert_card(const dashboard_data_snapshot_t *data);

static void apply_theme_styles(const dashboard_palette_t *next)
{
    palette = next;
    for (int color = 0; color < DASH_COLOR_COUNT; ++color) {
        const lv_color_t value = lv_color_hex(palette->colors[color]);
        lv_style_set_bg_color(&color_styles[color], value);
        lv_style_set_text_color(&color_styles[color], value);
    }
    lv_style_t *panel = &color_styles[DASH_COLOR_PANEL];
    lv_style_set_radius(panel, theme->panel_radius);
    lv_style_set_border_width(panel, theme->panel_border_width);
    lv_style_set_border_color(panel, lv_color_hex(palette->colors[DASH_COLOR_PANEL_BORDER]));
    lv_style_set_border_post(panel, true);
    lv_style_set_radius(&color_styles[DASH_COLOR_BUTTON], theme->button_radius);
}

static lv_obj_t *label(lv_obj_t *parent, const char *text, const lv_font_t *font,
                       dashboard_color_t color, int x, int y)
{
    lv_obj_t *object = lv_label_create(parent);
    lv_obj_set_style_text_font(object, font, 0);
    lv_obj_add_style(object, &color_styles[color], 0);
    lv_obj_set_pos(object, x, y);
    lv_label_set_text(object, text);
    return object;
}

static lv_obj_t *surface(lv_obj_t *parent, int x, int y, int width, int height,
                         dashboard_color_t color, int radius)
{
    lv_obj_t *object = lv_obj_create(parent);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_size(object, width, height);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_add_style(object, &color_styles[color], 0);
    if (color != DASH_COLOR_PANEL) {
        lv_obj_set_style_border_width(object, 0, 0);
        lv_obj_set_style_radius(object, radius, 0);
    }
    lv_obj_set_scrollable(object, false);
    lv_obj_set_clickable(object, false);
    return object;
}

static void show_screen(lv_event_t *event)
{
    lv_screen_load(lv_event_get_user_data(event));
}

static lv_obj_t *button(lv_obj_t *parent, const char *text, int x, int y, int width,
                        lv_event_cb_t action, void *data)
{
    lv_obj_t *object = lv_button_create(parent);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_size(object, width, 38);
    lv_obj_add_style(object, &color_styles[DASH_COLOR_BUTTON], 0);
    lv_obj_t *caption = label(object, text, body_font, DASH_COLOR_TEXT, 0, 0);
    lv_obj_center(caption);
    lv_obj_add_event_cb(object, action, LV_EVENT_CLICKED, data);
    return object;
}

static lv_obj_t *create_screen(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_add_style(screen, &color_styles[DASH_COLOR_BACKGROUND], 0);
    lv_obj_set_scrollable(screen, false);
    return screen;
}

static esp_err_t paint_map(const char regions[DASHBOARD_REGION_COUNT])
{
    static char painted_regions[DASHBOARD_REGION_COUNT];
    static const dashboard_palette_t *painted_palette;
    if (regions && painted_palette == palette &&
        memcmp(regions, painted_regions, sizeof(painted_regions)) == 0) {
        return ESP_OK;
    }

    uint16_t region_color[UA_MAP_BORDER + 1] = {0};
    region_color[0] = lv_color_to_u16(lv_color_hex(palette->colors[DASH_COLOR_PANEL]));
    region_color[UA_MAP_BORDER] = lv_color_to_u16(lv_color_hex(palette->colors[DASH_COLOR_MAP_BORDER]));
    for (unsigned index = 1; index < DASHBOARD_REGION_COUNT; ++index) {
        dashboard_color_t color = DASH_COLOR_MAP_CLEAR;
        if (regions) {
            if (regions[index] == 'R') {
                color = DASH_COLOR_MAP_ALARM;
            } else if (regions[index] == 'Y' || regions[index] == 'P') {
                color = DASH_COLOR_MAP_WARNING;
            }
        }
        region_color[index] = lv_color_to_u16(lv_color_hex(palette->colors[color]));
    }

    size_t run = 0;
    for (unsigned y = 0; y < UA_MAP_HEIGHT; ++y) {
        uint16_t *row = lv_draw_buf_goto_xy(&map_buffer, 0, y);
        for (unsigned x = 0; x < UA_MAP_WIDTH;) {
            if (run + 1 >= sizeof(ua_map_runs)) {
                return ESP_ERR_INVALID_SIZE;
            }
            const uint8_t length = ua_map_runs[run++];
            const uint8_t region = ua_map_runs[run++];
            if (!length || x + length > UA_MAP_WIDTH || region > UA_MAP_BORDER) {
                return ESP_ERR_INVALID_SIZE;
            }
            const unsigned end = x + length;
            for (; x < end; ++x) {
                row[x] = region_color[region];
            }
        }
    }
    if (run != sizeof(ua_map_runs)) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (regions) {
        memcpy(painted_regions, regions, sizeof(painted_regions));
    } else {
        memset(painted_regions, 0, sizeof(painted_regions));
    }
    painted_palette = palette;
    if (map_canvas) {
        lv_obj_invalidate(map_canvas);
    }
    return ESP_OK;
}

static esp_err_t add_map(lv_obj_t *card)
{
    const uint32_t stride = lv_draw_buf_width_to_stride(UA_MAP_WIDTH, LV_COLOR_FORMAT_RGB565);
    const uint32_t size = LV_DRAW_BUF_SIZE(UA_MAP_WIDTH, UA_MAP_HEIGHT, LV_COLOR_FORMAT_RGB565);
    void *storage = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!storage) {
        return ESP_ERR_NO_MEM;
    }
    if (lv_draw_buf_init(&map_buffer, UA_MAP_WIDTH, UA_MAP_HEIGHT, LV_COLOR_FORMAT_RGB565,
                         stride, storage, size) != LV_RESULT_OK) {
        heap_caps_free(storage);
        return ESP_FAIL;
    }
    const esp_err_t painted = paint_map(NULL);
    if (painted != ESP_OK) {
        heap_caps_free(storage);
        return painted;
    }

    map_canvas = lv_canvas_create(card);
    lv_canvas_set_draw_buf(map_canvas, &map_buffer);
    lv_obj_set_pos(map_canvas, 12, 30);
    home.map_marker = surface(card, 12 + UA_MAP_DNIPRO_X - 4, 30 + UA_MAP_DNIPRO_Y - 4,
                              9, 9, DASH_COLOR_MAP_MARKER, 5);
    return ESP_OK;
}

static lv_obj_t *make_outage_track(lv_obj_t *parent, int x, int y, int width)
{
    lv_obj_t *track = surface(parent, x, y, width, 12, DASH_COLOR_TRACK_UNKNOWN, 4);
    lv_obj_add_style(track, &color_styles[DASH_COLOR_TRACK], LV_STATE_USER_1);
    static const char *const times[] = {"00", "06", "12", "18", "24"};
    for (int index = 0; index < 5; ++index) {
        lv_obj_t *tick = label(parent, times[index], small_font, DASH_COLOR_MUTED, 0, y + 15);
        lv_obj_set_width(tick, 24);
        lv_obj_set_style_text_align(tick, LV_TEXT_ALIGN_CENTER, 0);
        const int center = x + index * width / 4;
        lv_obj_set_x(tick, index == 0 ? x : index == 4 ? x + width - 24 : center - 12);
    }
    return track;
}

static bool has_plan(const dashboard_energy_day_t *day)
{
    return day && (day->state == DASH_ENERGY_PLANNED || day->state == DASH_ENERGY_NO_SHUTDOWNS);
}

static dashboard_color_t outage_color(uint8_t kind)
{
    return kind == DASH_SLOT_OFF ? DASH_COLOR_OUTAGE : DASH_COLOR_POSSIBLE_OUTAGE;
}

static time_t kyiv_clock(struct tm *local)
{
    const time_t current = time(NULL);
    return current >= 1700000000 && localtime_r(&current, local) ? current : 0;
}

static void refresh_display_mode(void)
{
    struct tm local;
    const int minute = kyiv_clock(&local) ? local.tm_hour * 60 + local.tm_min : -1;
    const bool night = dashboard_display_is_night(&display_mode, minute);
    const dashboard_palette_t *next = night ? &theme->night : &theme->day;
    if (next != palette) {
        apply_theme_styles(next);
        lv_obj_report_style_change(NULL);
        dashboard_data_snapshot_t data;
        dashboard_data_snapshot(&data);
        update_home_wifi(dashboard_wifi_state());
        update_alert_card(&data);
        ESP_ERROR_CHECK_WITHOUT_ABORT(paint_map(data.region_alerts));
    }

    static int applied_percent = -1;
    const int percent = dashboard_display_brightness(&display_mode, minute);
    if (percent != applied_percent) {
        const esp_err_t result = apply_backlight(percent);
        if (result == ESP_OK) {
            applied_percent = percent;
        } else {
            ESP_LOGW("dashboard_screen", "Backlight update failed: %s", esp_err_to_name(result));
        }
    }
}

static void read_display_touch(lv_indev_t *input, lv_indev_data_t *data)
{
    read_touch(input, data);
    const bool was_asleep = display_mode.asleep;
    if (dashboard_display_touch(&display_mode, data->state == LV_INDEV_STATE_PRESSED,
                                esp_timer_get_time() / 1000)) {
        // Hide the entire wake gesture, including its release, from LVGL.
        data->state = LV_INDEV_STATE_RELEASED;
    }
    if (was_asleep && !display_mode.asleep) {
        refresh_display_mode();
    }
}

static uint8_t display_flags(void)
{
    return (display_mode.night_enabled ? SETTING_NIGHT : 0) |
           (display_mode.eco_enabled ? SETTING_ECO : 0) |
           (display_mode.alert_wake_enabled ? SETTING_ALERT_WAKE : 0);
}

static void use_display_flags(uint8_t flags)
{
    display_mode.night_enabled = (flags & SETTING_NIGHT) != 0;
    display_mode.eco_enabled = (flags & SETTING_ECO) != 0;
    display_mode.alert_wake_enabled = (flags & SETTING_ALERT_WAKE) != 0;
}

static void change_display_setting(lv_event_t *event)
{
    lv_obj_t *control = lv_event_get_target_obj(event);
    const uint8_t setting = (uintptr_t)lv_event_get_user_data(event);
    const bool checked = lv_obj_has_state(control, LV_STATE_CHECKED);
    const uint8_t old_flags = display_flags();
    const uint8_t flags = checked ? old_flags | setting : old_flags & ~setting;
    esp_err_t err = nvs_set_u8(display_settings, "flags", flags);
    if (err == ESP_OK) {
        err = nvs_commit(display_settings);
    }
    if (err != ESP_OK) {
        lv_obj_set_state(control, LV_STATE_CHECKED, (old_flags & setting) != 0);
        lv_label_set_text(display_settings_note, "Не вдалося зберегти налаштування");
        ESP_LOGW("dashboard_screen", "Display settings save failed: %s", esp_err_to_name(err));
        return;
    }
    lv_label_set_text(display_settings_note, "");
    use_display_flags(flags);
    display_mode.last_activity_ms = esp_timer_get_time() / 1000;
    dashboard_display_idle(&display_mode, display_mode.last_activity_ms);
    refresh_display_mode();
}

static void add_display_switch(int y, const char *name, const char *description, uint8_t setting)
{
    lv_obj_t *card = surface(display_settings_screen, MARGIN, y, CARD_WIDTH, 80, DASH_COLOR_PANEL, 12);
    label(card, name, body_font, DASH_COLOR_TEXT, 14, 9);
    label(card, description, small_font, DASH_COLOR_MUTED, 14, 44);
    lv_obj_t *control = lv_switch_create(card);
    lv_obj_set_size(control, 52, 28);
    lv_obj_set_pos(control, CARD_WIDTH - 67, 10);
    lv_obj_add_style(control, &color_styles[DASH_COLOR_SELECTED], LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_state(control, LV_STATE_CHECKED, (display_flags() & setting) != 0);
    lv_obj_add_event_cb(control, change_display_setting, LV_EVENT_VALUE_CHANGED, (void *)(uintptr_t)setting);
}

static void apply_home_layout(void)
{
    const bool pda = theme->home_layout == DASH_HOME_PDA;
    lv_obj_set_hidden(home.header, !pda);
    lv_obj_set_hidden(home.map_marker, pda);

    lv_label_set_text(home.city, pda ? "ДНІПРО" : "Дніпро");
    lv_obj_set_style_text_font(home.city, pda ? pda_value_font : title_font, 0);
    lv_obj_set_pos(home.city, pda ? 44 : 16, pda ? 23 : 8);
    lv_obj_set_style_text_font(home_clock, pda ? pda_clock_font : title_font, 0);
    lv_obj_align(home_clock, LV_ALIGN_TOP_MID, -60, pda ? 12 : 10);

    lv_obj_set_y(home.settings_key, pda ? 15 : 10);
    lv_obj_set_y(home.wifi_key, pda ? 15 : 10);
    lv_obj_t *settings_caption = lv_obj_get_child(home.settings_key, 0);
    lv_obj_set_style_text_font(settings_caption, pda ? pda_label_font : body_font, 0);
    lv_obj_align(settings_caption, LV_ALIGN_CENTER, pda ? 7 : 0, 0);
    lv_obj_align(home_wifi_status, LV_ALIGN_CENTER, pda ? 7 : 0, 0);

    lv_obj_set_y(home.weather_card, pda ? 61 : 64);
    lv_obj_set_y(home.alert_card, pda ? 61 : 64);
    lv_obj_set_height(home.weather_card, pda ? 60 : 68);
    lv_obj_set_height(home.alert_card, pda ? 60 : 68);
    lv_obj_set_width(home.weather_card, pda ? 148 : 140);
    lv_obj_set_style_text_font(home.weather_heading, pda ? pda_label_font : small_font, 0);
    lv_obj_set_style_text_font(home.alert_heading, pda ? pda_label_font : small_font, 0);
    lv_obj_set_style_text_font(weather_value, pda ? pda_value_font : title_font, 0);
    lv_obj_set_pos(home.weather_heading, pda ? 31 : 12, 3);
    lv_obj_set_pos(home.alert_heading, pda ? 31 : 12, 4);
    lv_obj_set_pos(weather_value, pda ? 31 : 12, pda ? 20 : 18);
    lv_obj_set_pos(weather_note, pda ? 31 : 12, pda ? 41 : 49);
    lv_obj_set_pos(alert_value, pda ? 31 : 12, pda ? 21 : 23);
    lv_obj_set_pos(alert_note, pda ? 31 : 12, pda ? 41 : 49);

    lv_obj_set_y(home.map_card, pda ? 127 : 140);
    lv_obj_set_height(home.map_card, pda ? 258 : 254);
    lv_obj_set_x(home.map_heading, pda ? 32 : 12);
    lv_obj_set_style_text_font(home.map_heading, pda ? pda_label_font : body_font, 0);

    lv_obj_set_y(home.power_card, pda ? 393 : 400);
    lv_obj_set_height(home.power_card, pda ? 73 : 68);
    lv_obj_set_x(home.power_heading, pda ? 32 : 12);
    lv_obj_set_style_text_font(home.power_heading, pda ? pda_label_font : body_font, 0);
    lv_obj_align(energy_home_note, pda ? LV_ALIGN_TOP_RIGHT : LV_ALIGN_TOP_LEFT,
                 pda ? -31 : 230, 6);
}

static void draw_pda(lv_event_t *event)
{
    if (theme->home_layout != DASH_HOME_PDA) {
        return;
    }

    lv_area_t map;
    lv_obj_get_coords(map_canvas, &map);
    dashboard_pda_draw(lv_event_get_layer(event), &map,
                       lv_color_hex(palette->colors[DASH_COLOR_PANEL_BORDER]),
                       lv_color_hex(palette->colors[DASH_COLOR_MUTED]),
                       lv_color_hex(palette->colors[DASH_COLOR_MAP_MARKER]),
                       lv_obj_get_style_text_color(home_wifi_status, LV_PART_MAIN),
                       lv_obj_get_style_text_color(alert_value, LV_PART_MAIN));
}

static void change_theme(lv_event_t *event)
{
    const dashboard_theme_t *next = lv_event_get_user_data(event);
    if (next == theme) {
        lv_label_set_text(theme_note, "");
        return;
    }

    esp_err_t err = nvs_set_u8(display_settings, "theme", next->id);
    if (err == ESP_OK) {
        err = nvs_commit(display_settings);
    }
    if (err != ESP_OK) {
        lv_label_set_text(theme_note, "Не вдалося зберегти тему");
        ESP_LOGW("dashboard_screen", "Theme save failed: %s", esp_err_to_name(err));
        return;
    }

    lv_obj_remove_state(selected_theme_row, LV_STATE_CHECKED);
    lv_obj_set_hidden(lv_obj_get_child(selected_theme_row, 1), true);
    selected_theme_row = lv_event_get_target_obj(event);
    lv_obj_add_state(selected_theme_row, LV_STATE_CHECKED);
    lv_obj_set_hidden(lv_obj_get_child(selected_theme_row, 1), false);
    theme = next;
    apply_home_layout();
    lv_label_set_text(theme_name, theme->name);
    lv_label_set_text(theme_note, "");
    refresh_display_mode();
}

static void add_theme_screen(void)
{
    label(theme_screen, "Тема", title_font, DASH_COLOR_TEXT, 16, 15);
    button(theme_screen, "Назад", 370, 14, 94, show_screen, display_settings_screen);

    lv_obj_t *list = surface(theme_screen, MARGIN, 68, CARD_WIDTH, 354, DASH_COLOR_BACKGROUND, 0);
    lv_obj_set_scrollable(list, true);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);

    size_t count;
    const dashboard_theme_t *themes = dashboard_theme_catalog(&count);
    for (size_t i = 0; i < count; ++i) {
        lv_obj_t *row = button(list, themes[i].name, 0, (int)i * 56, CARD_WIDTH - 12,
                               change_theme, (void *)&themes[i]);
        lv_obj_set_height(row, 46);
        lv_obj_add_style(row, &color_styles[DASH_COLOR_SELECTED], LV_STATE_CHECKED);
        lv_obj_t *caption = lv_obj_get_child(row, 0);
        lv_obj_set_width(caption, CARD_WIDTH - 145);
        lv_obj_set_height(caption, lv_font_get_line_height(body_font));
        lv_label_set_long_mode(caption, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_align(caption, LV_ALIGN_LEFT_MID, 6, 0);
        lv_obj_t *marker = label(row, "Обрана", small_font, DASH_COLOR_CLOCK, 0, 0);
        lv_obj_align(marker, LV_ALIGN_RIGHT_MID, -6, 0);
        if (&themes[i] == theme) {
            selected_theme_row = row;
            lv_obj_add_state(row, LV_STATE_CHECKED);
        } else {
            lv_obj_set_hidden(marker, true);
        }
    }
    theme_note = label(theme_screen, "", body_font, DASH_COLOR_ALARM, 16, 435);
}

static void add_display_settings_screen(void)
{
    label(display_settings_screen, "Екран", title_font, DASH_COLOR_TEXT, 16, 15);
    button(display_settings_screen, "Назад", 370, 14, 94, show_screen, home_screen);
    add_display_switch(69, "Нічний режим", "22:00–07:00 за Києвом · яскравість 8%", SETTING_NIGHT);
    add_display_switch(159, "Економний режим", "Підсвітка гасне через 60 с без дотиків", SETTING_ECO);
    add_display_switch(249, "Пробудження від тривоги", "Коли в області починається нова тривога", SETTING_ALERT_WAKE);
    label(display_settings_screen, "Тема", body_font, DASH_COLOR_TEXT, 30, 372);
    lv_obj_t *theme_button = button(display_settings_screen, theme->name, 161, 362, 303,
                                    show_screen, theme_screen);
    theme_name = lv_obj_get_child(theme_button, 0);
    display_settings_note = label(display_settings_screen, "", body_font, DASH_COLOR_ALARM, 16, 422);
}

static void update_clock(void)
{
    struct tm local;
    const time_t current = kyiv_clock(&local);
    static time_t displayed_minute = -1;
    if (displayed_minute == current / 60) {
        return;
    }
    displayed_minute = current / 60;
    if (current) {
        lv_label_set_text_fmt(home_clock, "%02d:%02d", local.tm_hour, local.tm_min);
    } else {
        lv_label_set_text(home_clock, "--:--");
    }
}

static const dashboard_energy_day_t *day_for_date(const dashboard_data_snapshot_t *data, const char *date)
{
    if (!date[0] || (data->energy_state != DASH_DATA_FRESH && data->energy_state != DASH_DATA_STALE)) {
        return NULL;
    }
    if (strcmp(data->energy.today.date, date) == 0) {
        return &data->energy.today;
    }
    return strcmp(data->energy.tomorrow.date, date) == 0 ? &data->energy.tomorrow : NULL;
}

static void render_track(lv_obj_t *track, const dashboard_energy_day_t *day, int now_minutes)
{
    const int width = CARD_WIDTH - 24;
    lv_obj_clean(track);
    lv_obj_set_state(track, LV_STATE_USER_1, has_plan(day));
    if (has_plan(day)) {
        for (int start = 0; start < DASH_ENERGY_SLOTS;) {
            const uint8_t kind = day->slots[start];
            int end = start + 1;
            while (end < DASH_ENERGY_SLOTS && day->slots[end] == kind) {
                ++end;
            }
            if (kind != DASH_SLOT_CLEAR) {
                const int x = start * width / DASH_ENERGY_SLOTS;
                surface(track, x, 0, end * width / DASH_ENERGY_SLOTS - x, 12, outage_color(kind), 0);
            }
            start = end;
        }
    }
    if (now_minutes >= 0) {
        surface(track, now_minutes * (width - 2) / MINUTES_PER_DAY, 0, 2, 12, DASH_COLOR_TEXT, 0);
    }
}

static const char *energy_message(const dashboard_data_snapshot_t *data, const dashboard_energy_day_t *day)
{
    switch (data->energy_state) {
    case DASH_DATA_NO_TOKEN: return "Потрібен ключ API";
    case DASH_DATA_AUTH_ERROR: return "Перевір ключ і доступ";
    case DASH_DATA_WAITING:
        return dashboard_wifi_state() == DASH_WIFI_CONNECTED ? "Оновлення…" : "Очікує Wi-Fi";
    case DASH_DATA_UNAVAILABLE: return "Джерело недоступне";
    case DASH_DATA_STALE: return day ? "Застарілі дані" : "Немає свіжого графіка";
    default: break;
    }
    if (!day || day->state == DASH_ENERGY_WAITING) {
        return "Очікуємо графік";
    }
    if (day->state == DASH_ENERGY_EMERGENCY) {
        return "Аварійні відключення";
    }
    if (day->state == DASH_ENERGY_NO_SHUTDOWNS) {
        return "Планових немає";
    }
    return "Плановий графік";
}

static void render_next_outage(const dashboard_data_snapshot_t *data, const dashboard_energy_day_t *day,
                               int now_minutes)
{
    if (data->energy_state == DASH_DATA_FRESH && day && day->state == DASH_ENERGY_PLANNED &&
        now_minutes >= 0) {
        const int current_slot = now_minutes / 30;
        for (int index = current_slot; index < DASH_ENERGY_SLOTS; ++index) {
            if (day->slots[index] == DASH_SLOT_CLEAR) {
                continue;
            }
            const uint8_t kind = day->slots[index];
            int end = index + 1;
            while (end < DASH_ENERGY_SLOTS && day->slots[end] == kind) {
                ++end;
            }
            if (index == current_slot) {
                lv_label_set_text_fmt(energy_home_note, "%s до %02d:%02d",
                                      kind == DASH_SLOT_OFF ? "Зараз" : "Можливо", end / 2, end % 2 * 30);
            } else {
                lv_label_set_text_fmt(energy_home_note, "%s %02d:%02d–%02d:%02d",
                                      kind == DASH_SLOT_OFF ? "Далі" : "Можливо",
                                      index / 2, index % 2 * 30, end / 2, end % 2 * 30);
            }
            return;
        }
        lv_label_set_text(energy_home_note, "Більше немає за планом");
        return;
    }
    lv_label_set_text(energy_home_note, energy_message(data, day));
}

static void render_intervals(const dashboard_energy_day_t *day)
{
    lv_obj_clean(energy_intervals);
    int row = 0;
    if (has_plan(day)) {
        for (int start = 0; start < DASH_ENERGY_SLOTS;) {
            const uint8_t kind = day->slots[start];
            int end = start + 1;
            while (end < DASH_ENERGY_SLOTS && day->slots[end] == kind) {
                ++end;
            }
            if (kind != DASH_SLOT_CLEAR) {
                lv_obj_t *entry = label(energy_intervals, "", body_font, outage_color(kind), 14, 6 + row * 28);
                lv_label_set_text_fmt(entry, "%02d:%02d–%02d:%02d  %s",
                                      start / 2, start % 2 * 30, end / 2, end % 2 * 30,
                                      kind == DASH_SLOT_OFF ? "Заплановано" : "Можливо");
                ++row;
            }
            start = end;
        }
        if (!row) {
            label(energy_intervals, "Планових інтервалів немає", body_font, DASH_COLOR_MUTED, 14, 6);
        }
    }
}

static void change_schedule_day(lv_event_t *event)
{
    energy_show_tomorrow = lv_event_get_user_data(event) != NULL;
}

static void add_schedule_screen(void)
{
    label(schedule_screen, "Відключення ЦЕК 5.2", title_font, DASH_COLOR_TEXT, 16, 15);
    energy_detail_date = label(schedule_screen, "", body_font, DASH_COLOR_MUTED, 16, 53);
    button(schedule_screen, "Назад", 370, 14, 94, show_screen, home_screen);
    button(schedule_screen, "Сьогодні", 16, 81, 105, change_schedule_day, NULL);
    button(schedule_screen, "Завтра", 133, 81, 105, change_schedule_day, schedule_screen);
    energy_detail_note = label(schedule_screen, "Оновлення…", body_font, DASH_COLOR_WARNING, 16, 130);

    lv_obj_t *timeline = surface(schedule_screen, 16, 160, CARD_WIDTH, 92, DASH_COLOR_PANEL, 12);
    surface(timeline, 12, 14, 10, 10, DASH_COLOR_OUTAGE, 3);
    label(timeline, "Заплановано", small_font, DASH_COLOR_TEXT, 29, 10);
    surface(timeline, 157, 14, 10, 10, DASH_COLOR_POSSIBLE_OUTAGE, 3);
    label(timeline, "Можливо", small_font, DASH_COLOR_TEXT, 174, 10);
    energy_detail_track = make_outage_track(timeline, 12, 44, CARD_WIDTH - 24);

    energy_intervals = surface(schedule_screen, 16, 262, CARD_WIDTH, 167, DASH_COLOR_PANEL, 12);
    lv_obj_set_scrollable(energy_intervals, true);
    lv_obj_set_scroll_dir(energy_intervals, LV_DIR_VER);
    energy_detail_age = label(schedule_screen, "Alerts.energy", small_font, DASH_COLOR_MUTED, 16, 446);
}

static void update_energy_cards(const dashboard_data_snapshot_t *data)
{
    struct tm local;
    const time_t current = kyiv_clock(&local);
    const int minutes = current ? local.tm_hour * 60 + local.tm_min : -1;
    static time_t displayed_minute = -1;
    static int64_t displayed_revision = -1;
    static int displayed_state = -1;
    static bool displayed_tomorrow;
    if (displayed_minute == current / 60 && displayed_revision == data->energy_updated_us &&
        displayed_state == (int)data->energy_state && displayed_tomorrow == energy_show_tomorrow) {
        return;
    }
    displayed_minute = current / 60;
    displayed_revision = data->energy_updated_us;
    displayed_state = data->energy_state;
    displayed_tomorrow = energy_show_tomorrow;

    char date[11] = {0};
    if (current) {
        strftime(date, sizeof(date), "%Y-%m-%d", &local);
    }
    const dashboard_energy_day_t *today = day_for_date(data, date);
    render_track(energy_home_track, today, minutes);
    render_next_outage(data, today, minutes);

    if (current && energy_show_tomorrow) {
        ++local.tm_mday;
        local.tm_hour = 12;
        local.tm_isdst = -1;
        if (mktime(&local) != (time_t)-1) {
            strftime(date, sizeof(date), "%Y-%m-%d", &local);
        } else {
            date[0] = '\0';
        }
    }
    const dashboard_energy_day_t *detail = day_for_date(data, date);
    lv_label_set_text_fmt(energy_detail_date, "%s%s%s", energy_show_tomorrow ? "Завтра" : "Сьогодні",
                          date[0] ? " · " : "", date);
    lv_label_set_text(energy_detail_note, energy_message(data, detail));
    render_track(energy_detail_track, detail, energy_show_tomorrow ? -1 : minutes);
    render_intervals(detail);
    if (data->energy_updated_us) {
        const long long age = (esp_timer_get_time() - data->energy_updated_us) / 60000000;
        lv_label_set_text_fmt(energy_detail_age, "Alerts.energy · оновлено %lld хв тому", age);
    }
}

static void request_setup(lv_event_t *event)
{
    if (dashboard_wifi_request_setup() != ESP_OK) {
        lv_label_set_text(wifi_status, "Не вдалося запустити налаштування");
    }
}

static void add_wifi_screen(void)
{
    label(wifi_screen, "Мережа Wi-Fi", title_font, DASH_COLOR_TEXT, 16, 15);
    label(wifi_screen, "Підключення до домашньої мережі", body_font, DASH_COLOR_MUTED, 16, 53);
    button(wifi_screen, "Назад", 370, 14, 94, show_screen, home_screen);

    lv_obj_t *card = surface(wifi_screen, 16, 98, CARD_WIDTH, 226, DASH_COLOR_PANEL, 12);
    wifi_status = label(card, "", body_font, DASH_COLOR_TEXT, 15, 14);
    lv_obj_set_width(wifi_status, CARD_WIDTH - 30);
    wifi_instructions = label(card, "", body_font, DASH_COLOR_MUTED, 15, 59);
    lv_obj_set_width(wifi_instructions, CARD_WIDTH - 30);
    wifi_change_button = button(card, "Змінити мережу", 15, 171, 176, request_setup, NULL);
}

static bool setup_in_progress(int state)
{
    return state == DASH_WIFI_SETUP || state == DASH_WIFI_SETUP_FAILED;
}

static void update_home_wifi(dashboard_wifi_state_t state)
{
    const char *caption = "Підключення…";
    dashboard_color_t color = DASH_COLOR_WARNING;
    switch (state) {
    case DASH_WIFI_CONNECTED:
        caption = "Підключено";
        color = DASH_COLOR_CONNECTED;
        break;
    case DASH_WIFI_SETUP:
        caption = "Налаштування";
        break;
    case DASH_WIFI_SETUP_FAILED:
    case DASH_WIFI_ERROR:
        caption = "Помилка";
        color = DASH_COLOR_ALARM;
        break;
    case DASH_WIFI_OFFLINE:
        caption = "Немає зв’язку";
        color = DASH_COLOR_ALARM;
        break;
    default:
        break;
    }
    lv_label_set_text_fmt(home_wifi_status, "Wi-Fi\n%s", caption);
    lv_obj_set_style_text_color(home_wifi_status, lv_color_hex(palette->colors[color]), 0);
    if (theme->home_layout == DASH_HOME_PDA) {
        lv_obj_invalidate(home.wifi_key);
    }
}

static void update_wifi(lv_timer_t *timer)
{
    static int previous_state = -1;
    const dashboard_wifi_state_t state = dashboard_wifi_state();
    if (previous_state == (int)state) {
        return;
    }

    const bool configuring = setup_in_progress(state);
    const bool was_configuring = setup_in_progress(previous_state);
    previous_state = state;
    update_home_wifi(state);
    if (configuring) {
        lv_label_set_text(wifi_status, state == DASH_WIFI_SETUP_FAILED
                          ? "Не вдалося підключитися. Спробуйте ще раз."
                          : "Налаштування з телефона");
        lv_label_set_text_fmt(wifi_instructions, "%s\nПароль точки: %s\nУ браузері відкрийте http://192.168.4.1",
                              dashboard_wifi_setup_name(), dashboard_wifi_setup_code());
        lv_obj_add_state(wifi_change_button, LV_STATE_DISABLED);
        if (!was_configuring) {
            lv_screen_load(wifi_screen);
        }
        return;
    }

    lv_label_set_text(wifi_instructions, "Домашню мережу можна змінити з телефона.");
    lv_obj_remove_state(wifi_change_button, LV_STATE_DISABLED);
    switch (state) {
    case DASH_WIFI_CONNECTED:
        lv_label_set_text(wifi_status, "Підключено");
        if (was_configuring && lv_screen_active() == wifi_screen) {
            lv_screen_load(home_screen);
        }
        break;
    case DASH_WIFI_OFFLINE:
        lv_label_set_text(wifi_status, "Зв’язок втрачено. Повторне підключення…");
        break;
    case DASH_WIFI_ERROR:
        lv_label_set_text(wifi_status, "Помилка налаштування мережі");
        break;
    default:
        lv_label_set_text(wifi_status, "Підключення…");
        break;
    }
}

static void set_label_if_changed(lv_obj_t *object, const char *text)
{
    const char *current = lv_label_get_text(object);
    if (!current || strcmp(current, text) != 0) {
        lv_label_set_text(object, text);
    }
}

static void update_age(lv_obj_t *object, int64_t updated_us, bool stale)
{
    const long long minutes = (esp_timer_get_time() - updated_us) / 60000000;
    if (minutes == 0) {
        set_label_if_changed(object, stale ? "Щойно · застар." : "Щойно оновлено");
    } else {
        char text[64];
        snprintf(text, sizeof(text), "%lld хв%s", minutes, stale ? " · застар." : " тому");
        set_label_if_changed(object, text);
    }
}

static void update_weather_card(const dashboard_data_snapshot_t *data)
{
    if (data->weather_updated_us) {
        const int value = data->temperature_tenths;
        const int magnitude = value < 0 ? -value : value;
        char text[40];
        snprintf(text, sizeof(text), "%s%d,%d °C", value < 0 ? "−" : "",
                 magnitude / 10, magnitude % 10);
        set_label_if_changed(weather_value, text);
        update_age(weather_note, data->weather_updated_us, data->weather_state == DASH_DATA_STALE);
        return;
    }

    set_label_if_changed(weather_value, "— °C");
    const char *message = "Оновлення…";
    if (dashboard_wifi_state() != DASH_WIFI_CONNECTED) {
        message = "Очікує Wi-Fi";
    } else if (data->weather_state == DASH_DATA_UNAVAILABLE) {
        message = "Немає даних";
    }
    set_label_if_changed(weather_note, message);
}

static void update_alert_card(const dashboard_data_snapshot_t *data)
{
    const char *status = "Статус невідомий";
    const char *note = "Очікування даних";
    dashboard_color_t color = DASH_COLOR_MUTED;
    if (data->alerts_state == DASH_DATA_FRESH) {
        switch (data->region_alerts[DNIPRO_REGION]) {
        case 'R':
            status = "Повітряна тривога";
            color = DASH_COLOR_ALARM;
            break;
        case 'Y':
            status = "Попередня загроза";
            color = DASH_COLOR_WARNING;
            break;
        case 'P':
            status = "Часткова тривога в області";
            color = DASH_COLOR_WARNING;
            break;
        case 'N':
            status = "Тривог не повідомлено";
            break;
        }
    } else if (data->alerts_state == DASH_DATA_STALE) {
        status = "Дані тривог застаріли";
        color = DASH_COLOR_WARNING;
    } else if (data->alerts_state == DASH_DATA_NO_TOKEN) {
        note = "Потрібен токен Alerts.in.ua";
    } else if (data->alerts_state == DASH_DATA_AUTH_ERROR) {
        note = "Токен не прийнято";
    } else if (data->alerts_state == DASH_DATA_UNAVAILABLE) {
        note = "Джерело недоступне";
    } else if (dashboard_wifi_state() != DASH_WIFI_CONNECTED) {
        note = "Очікує Wi-Fi";
    }

    set_label_if_changed(alert_value, status);
    const lv_color_t status_color = lv_color_hex(palette->colors[color]);
    if (!lv_color_eq(lv_obj_get_style_text_color(alert_value, LV_PART_MAIN), status_color)) {
        lv_obj_set_style_text_color(alert_value, status_color, 0);
        if (theme->home_layout == DASH_HOME_PDA) {
            lv_obj_invalidate(home.alert_card);
        }
    }
    if (data->alerts_state == DASH_DATA_FRESH || data->alerts_state == DASH_DATA_STALE) {
        update_age(alert_note, data->alerts_updated_us, data->alerts_state == DASH_DATA_STALE);
    } else {
        set_label_if_changed(alert_note, note);
    }
}

static void update_remote_data(lv_timer_t *timer)
{
    dashboard_data_snapshot_t data;
    dashboard_data_snapshot(&data);
    const int64_t now_ms = esp_timer_get_time() / 1000;
    dashboard_display_idle(&display_mode, now_ms);
    if (dashboard_display_alert(&display_mode, data.alerts_state == DASH_DATA_FRESH,
                                data.region_alerts[DNIPRO_REGION], now_ms) &&
        !setup_in_progress(dashboard_wifi_state())) {
        lv_screen_load(home_screen);
    }
    refresh_display_mode();
    lv_obj_t *active = lv_screen_active();
    if (active != home_screen && active != schedule_screen) {
        return;
    }
    update_energy_cards(&data);
    if (active != home_screen) {
        return;
    }
    update_clock();
    update_weather_card(&data);
    update_alert_card(&data);
    ESP_ERROR_CHECK_WITHOUT_ABORT(paint_map(data.region_alerts));
}

static const lv_font_t *create_pda_font(int size, const lv_font_t *fallback)
{
    const size_t bytes = (size_t)(ubuntu_mono_end - ubuntu_mono_start);
    lv_font_t *font = lv_tiny_ttf_create_data_ex(ubuntu_mono_start, bytes, size, LV_FONT_KERNING_NONE, 32);
    if (!font) {
        ESP_LOGW("dashboard_screen", "Could not initialize PDA font at %d px", size);
        return fallback;
    }
    return font;
}

esp_err_t dashboard_screen_create(lv_indev_t *touch, esp_err_t (*set_backlight)(int percent))
{
    const size_t font_size = (size_t)(ubuntu_medium_end - ubuntu_medium_start);
    small_font = lv_tiny_ttf_create_data_ex(ubuntu_medium_start, font_size, 14, LV_FONT_KERNING_NONE, 48);
    body_font = lv_tiny_ttf_create_data_ex(ubuntu_medium_start, font_size, 17, LV_FONT_KERNING_NONE, 48);
    title_font = lv_tiny_ttf_create_data_ex(ubuntu_medium_start, font_size, 27, LV_FONT_KERNING_NONE, 48);
    if (!small_font || !body_font || !title_font) {
        return ESP_ERR_NO_MEM;
    }

    pda_label_font = create_pda_font(15, small_font);
    pda_value_font = create_pda_font(21, body_font);
    pda_clock_font = create_pda_font(35, title_font);

    uint8_t flags = 0;
    uint8_t theme_id = DASH_THEME_STANDARD;
    const esp_err_t settings_error = nvs_open(display_settings_namespace, NVS_READWRITE, &display_settings);
    if (settings_error == ESP_OK) {
        const esp_err_t flags_error = nvs_get_u8(display_settings, "flags", &flags);
        if (flags_error != ESP_OK && flags_error != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW("dashboard_screen", "Display flags read failed: %s", esp_err_to_name(flags_error));
        }
        const esp_err_t theme_error = nvs_get_u8(display_settings, "theme", &theme_id);
        if (theme_error != ESP_OK && theme_error != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW("dashboard_screen", "Theme read failed: %s", esp_err_to_name(theme_error));
        }
    } else {
        ESP_LOGW("dashboard_screen", "Display settings open failed: %s", esp_err_to_name(settings_error));
    }
    use_display_flags(flags);
    display_mode.last_activity_ms = esp_timer_get_time() / 1000;
    apply_backlight = set_backlight;
    for (int color = 0; color < DASH_COLOR_COUNT; ++color) {
        lv_style_init(&color_styles[color]);
    }
    theme = dashboard_theme_find(theme_id);
    apply_theme_styles(&theme->day);

    home_screen = create_screen();
    schedule_screen = create_screen();
    wifi_screen = create_screen();
    display_settings_screen = create_screen();
    theme_screen = create_screen();

    home.header = surface(home_screen, MARGIN, 10, CARD_WIDTH, 48, DASH_COLOR_PANEL, 12);
    home.city = label(home_screen, "Дніпро", title_font, DASH_COLOR_TEXT, 16, 8);
    home_clock = label(home_screen, "--:--", title_font, DASH_COLOR_CLOCK, 0, 0);

    home.settings_key = button(home_screen, "Екран", 238, 10, 84, show_screen, display_settings_screen);
    home.wifi_key = button(home_screen, "Wi-Fi", 334, 10, 130, show_screen, wifi_screen);
    lv_obj_set_style_pad_all(home.wifi_key, 0, 0);
    home_wifi_status = lv_obj_get_child(home.wifi_key, 0);
    lv_obj_set_style_text_font(home_wifi_status, small_font, 0);
    lv_obj_set_style_text_align(home_wifi_status, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *weather = surface(home_screen, 16, 64, 140, 68, DASH_COLOR_PANEL, 12);
    home.weather_card = weather;
    home.weather_heading = label(weather, "На вулиці", small_font, DASH_COLOR_MUTED, 12, 3);
    weather_value = label(weather, "— °C", title_font, DASH_COLOR_TEMPERATURE, 12, 18);
    weather_note = label(weather, "", small_font, DASH_COLOR_MUTED, 12, 49);

    lv_obj_t *local_alert = surface(home_screen, 164, 64, 300, 68, DASH_COLOR_PANEL, 12);
    home.alert_card = local_alert;
    home.alert_heading = label(local_alert, "Дніпропетровська область", small_font, DASH_COLOR_MUTED, 12, 4);
    alert_value = label(local_alert, "Статус невідомий", body_font, DASH_COLOR_MUTED, 12, 23);
    alert_note = label(local_alert, "", small_font, DASH_COLOR_MUTED, 12, 49);

    lv_obj_t *map = surface(home_screen, 16, 140, CARD_WIDTH, 254, DASH_COLOR_PANEL, 12);
    home.map_card = map;
    home.map_heading = label(map, "Тривоги по областях", body_font, DASH_COLOR_TEXT, 12, 5);
    label(map, "© OSM", small_font, DASH_COLOR_MUTED, 188, 8);
    surface(map, 250, 11, 9, 9, DASH_COLOR_MAP_ALARM, 3);
    label(map, "Тривога", small_font, DASH_COLOR_TEXT, 264, 7);
    surface(map, 329, 11, 9, 9, DASH_COLOR_MAP_WARNING, 3);
    label(map, "Частк. / загроза", small_font, DASH_COLOR_TEXT, 343, 7);
    const esp_err_t result = add_map(map);
    if (result != ESP_OK) {
        ESP_LOGE("dashboard_screen", "Could not initialize the Ukraine map");
        return result;
    }

    lv_obj_t *power = surface(home_screen, 16, 400, CARD_WIDTH, 68, DASH_COLOR_PANEL, 12);
    lv_obj_set_clickable(power, true);
    lv_obj_add_event_cb(power, show_screen, LV_EVENT_CLICKED, schedule_screen);
    home.power_card = power;
    home.power_heading = label(power, "ЦЕК 5.2", body_font, DASH_COLOR_TEXT, 12, 4);
    energy_home_note = label(power, "Оновлення…", small_font, DASH_COLOR_WARNING, 230, 6);
    energy_home_track = make_outage_track(power, 12, 29, CARD_WIDTH - 24);

    add_schedule_screen();
    add_wifi_screen();
    add_display_settings_screen();
    add_theme_screen();
    apply_home_layout();
    lv_obj_add_event_cb(home_screen, draw_pda, LV_EVENT_DRAW_POST, NULL);
    read_touch = lv_indev_get_read_cb(touch);
    lv_indev_set_read_cb(touch, read_display_touch);
    lv_screen_load(home_screen);
    update_wifi(NULL);
    update_remote_data(NULL);
    lv_timer_create(update_wifi, 250, NULL);
    lv_timer_create(update_remote_data, 1000, NULL);
    return ESP_OK;
}
