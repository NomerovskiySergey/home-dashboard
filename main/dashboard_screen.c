#include "dashboard_screen.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "assets/ukraine_map_data.h"
#include "assets/dashboard_threat_icons.h"
#include "dashboard_data.h"
#include "dashboard_map_view.h"
#include "dashboard_display_mode.h"
#include "dashboard_pda.h"
#include "dashboard_pipboy.h"
#include "dashboard_theme.h"
#include "dashboard_wifi.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "nvs.h"

enum {
    SCREEN_WIDTH = 480,
    MARGIN = 16,
    CARD_WIDTH = SCREEN_WIDTH - MARGIN * 2,
    MINUTES_PER_DAY = 24 * 60,
    SETTING_NIGHT = 1 << 0,
    SETTING_ECO = 1 << 1,
    SETTING_ALERT_WAKE = 1 << 2,
};

_Static_assert((int)UA_MAP_WIDTH == (int)DASH_MAP_WIDTH && (int)UA_MAP_HEIGHT == (int)DASH_MAP_HEIGHT,
               "Viewport and generated map dimensions must match");

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
    lv_obj_t *alert_card;
    lv_obj_t *alert_heading;
    lv_obj_t *map_card;
    lv_obj_t *map_marker;
    lv_obj_t *power_card;
    lv_obj_t *power_heading;
} home;
static lv_obj_t *home_screen;
static lv_obj_t *home_clock;
static lv_obj_t *settings_screen;
static lv_obj_t *schedule_screen;
static lv_obj_t *wifi_screen;
static lv_obj_t *display_settings_screen;
static lv_obj_t *display_settings_note;
static lv_obj_t *location_screen, *city_screen, *location_list, *city_list;
static lv_obj_t *location_current, *location_error, *city_heading, *full_map_selected;
static lv_obj_t *location_rows[8], *location_extent;
static bool location_rows_updating;
static const dashboard_city_t *selected_city;
static lv_obj_t *city_rows[8], *city_extent;
static const dashboard_region_t *city_region;
static unsigned city_count;
static bool city_rows_updating;
static lv_obj_t *energy_settings_screen,*energy_settings_list,*energy_settings_heading,*energy_settings_note;
static lv_obj_t *energy_settings_context,*energy_settings_retry,*energy_rows[8];
static bool energy_rows_updating;
static dashboard_energy_catalog_t *energy_ui_catalog;
static dashboard_energy_selection_t energy_draft;
static dashboard_energy_catalog_kind_t energy_stage;
static uint32_t energy_request,energy_loaded;
static uint32_t selected_generation=UINT32_MAX;
static lv_obj_t *theme_screen;
static lv_obj_t *theme_name;
static lv_obj_t *theme_note;
static lv_obj_t *selected_theme_row;
static lv_obj_t *wifi_status;
static lv_obj_t *wifi_instructions;
static lv_obj_t *wifi_change_button;
static lv_obj_t *wifi_connection_fields;
static lv_obj_t *wifi_network_name;
static lv_obj_t *wifi_ip_address;
static lv_obj_t *wifi_signal;
static lv_obj_t *system_screen, *reboot_screen;
static lv_obj_t *system_sources[4], *system_status, *system_restart_button;
static lv_obj_t *reboot_confirm_button, *reboot_status;
static bool reboot_queued;
static lv_draw_buf_t map_buffer;
static lv_obj_t *map_canvas;
static lv_obj_t *weather_value;
static lv_obj_t *weather_note;
static lv_obj_t *alert_value;
static lv_obj_t *alert_note;
static lv_obj_t *alert_threat;
static lv_obj_t *map_state_note;
static bool map_stale;
static dashboard_alert_details_t screen_alert_details;
static lv_obj_t *map_screen, *full_map_viewport, *full_map_canvas, *full_map_note;
static lv_draw_buf_t full_map_buffer;
static bool full_map_dirty = true;
static lv_obj_t *map_cities[sizeof(ua_map_cities) / sizeof(ua_map_cities[0])];
static lv_obj_t *map_city_dots[sizeof(ua_map_cities) / sizeof(ua_map_cities[0])];
static lv_obj_t *map_threats[sizeof(ua_map_centers) / sizeof(ua_map_centers[0])];
static uint16_t visible_threats[DASHBOARD_REGION_COUNT];

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

static void update_alert_card(const dashboard_data_snapshot_t *data);
static void update_wifi_connection(lv_timer_t *timer);
static void update_full_map(void);
static void open_map(lv_event_t *event);
static void update_remote_data(lv_timer_t *timer);
static void update_system(lv_timer_t *timer);
static void prepare_location_list(void);
static void open_energy_settings(lv_event_t *event);
static void update_energy_settings(lv_timer_t *timer);
static void set_label_if_changed(lv_obj_t *object,const char *text);

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
    lv_obj_t *target = lv_event_get_user_data(event);
    if(target==location_screen) prepare_location_list();
    lv_screen_load(target);
    if (target == schedule_screen) update_remote_data(NULL);
    if (target == wifi_screen) update_wifi_connection(NULL);
    if (target == system_screen) update_system(NULL);
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
    static bool painted_stale;
    static const dashboard_palette_t *painted_palette;
    if (regions && painted_palette == palette && painted_stale == map_stale &&
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
        const lv_color_t fill = lv_color_hex(palette->colors[color]);
        region_color[index] = lv_color_to_u16(map_stale
            ? lv_color_mix(fill, lv_color_hex(palette->colors[DASH_COLOR_PANEL]), LV_OPA_30)
            : fill);
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
                row[x] = regions && region < DASHBOARD_REGION_COUNT && regions[region] == 'P' &&
                         !map_stale && (x + y) % 8 < 3
                         ? lv_color_to_u16(lv_color_hex(palette->colors[DASH_COLOR_MAP_CLEAR]))
                         : region_color[region];
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
    painted_stale = map_stale;
    painted_palette = palette;
    full_map_dirty = true;
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
    lv_obj_set_pos(map_canvas, 12, 78);
    lv_obj_set_clickable(map_canvas, true);
    lv_obj_add_event_cb(map_canvas, open_map, LV_EVENT_CLICKED, NULL);
    home.map_marker = surface(card, 12 + selected_city->map_x - 4,
                              78 + selected_city->map_y - 4,
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
        if (!dashboard_data_alert_details(&screen_alert_details, &data)) data.alert_details_state = DASH_DATA_STALE;
        map_stale = data.alerts_state != DASH_DATA_FRESH;
        update_alert_card(&data);
        if (map_canvas) ESP_ERROR_CHECK_WITHOUT_ABORT(paint_map(data.region_alerts));
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
        data->state = LV_INDEV_STATE_RELEASED;
    }
    if (was_asleep && !display_mode.asleep) refresh_display_mode();
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

static void layout_city_title(lv_obj_t *object, const lv_font_t *font, int x, int width)
{
    lv_obj_set_style_text_font(object, font, 0);
    lv_obj_set_size(object, width, lv_font_get_line_height(font));
    lv_obj_set_pos(object, x, 23);
    lv_label_set_long_mode(object, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
}

static void apply_home_layout(void)
{
    const bool compact = theme->home_layout != DASH_HOME_STANDARD;
    lv_obj_set_hidden(home.header, !compact);
    if (home.map_marker) lv_obj_set_hidden(home.map_marker, compact);

    lv_label_set_text(home.city, selected_city->name);
    layout_city_title(home.city, compact ? pda_value_font : body_font,
                      compact ? 44 : 16, compact ? 96 : 120);
    lv_obj_set_style_text_font(home_clock, compact ? pda_clock_font : title_font, 0);
    lv_obj_set_pos(home_clock, 147, compact ? 12 : 10);
    lv_obj_set_style_text_font(weather_value, compact ? pda_value_font : title_font, 0);
    lv_obj_set_pos(weather_value, 278, compact ? 12 : 8);
    lv_obj_set_pos(weather_note, 278, compact ? 36 : 40);

    lv_obj_set_style_text_font(home.alert_heading, compact ? pda_label_font : small_font, 0);
    lv_obj_set_pos(home.alert_heading, compact ? 31 : 12, 4);
    lv_obj_set_pos(alert_value, compact ? 31 : 12, compact ? 21 : 23);
    lv_obj_set_pos(alert_note, compact ? 31 : 12, 48);
    lv_obj_set_pos(alert_threat, 215, 48);
    lv_obj_set_width(alert_value, compact ? 401 : 420);
    lv_obj_set_width(alert_note, 178);
    lv_obj_set_width(alert_threat, 217);

    lv_obj_set_y(home.map_card, 64);
    lv_obj_set_height(home.map_card, 330);

    lv_obj_set_y(home.power_card, 400);
    lv_obj_set_height(home.power_card, 68);
    lv_obj_set_x(home.power_heading, compact ? 32 : 12);
    lv_obj_set_style_text_font(home.power_heading, compact ? pda_label_font : body_font, 0);
    lv_obj_align(energy_home_note, compact ? LV_ALIGN_TOP_RIGHT : LV_ALIGN_TOP_LEFT,
                 compact ? -31 : 230, 6);
}

static void draw_home_instruments(lv_event_t *event)
{
    if (theme->home_layout == DASH_HOME_STANDARD) {
        return;
    }

    if (!map_canvas) return;
    lv_area_t map;
    lv_obj_get_coords(map_canvas, &map);
    void (*draw)(lv_layer_t *, const lv_area_t *, lv_color_t, lv_color_t,
                 lv_color_t, lv_color_t, int, int) =
        theme->home_layout == DASH_HOME_PIPBOY ? dashboard_pipboy_draw : dashboard_pda_draw;
    draw(lv_event_get_layer(event), &map,
         lv_color_hex(palette->colors[DASH_COLOR_PANEL_BORDER]),
         lv_color_hex(palette->colors[DASH_COLOR_MUTED]),
         lv_color_hex(palette->colors[DASH_COLOR_MAP_MARKER]),
         lv_obj_get_style_text_color(alert_value, LV_PART_MAIN), selected_city->map_x, selected_city->map_y);
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
    button(display_settings_screen, "Назад", 370, 14, 94, show_screen, settings_screen);
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
    static struct {
        bool ready, planned;
        uint8_t slots[DASH_ENERGY_SLOTS];
        lv_obj_t *marker;
    } tracks[2];
    const unsigned index=track==energy_home_track ? 0 : 1;
    const bool planned=has_plan(day);
    const int width = CARD_WIDTH - 24;
    if(!tracks[index].ready || tracks[index].planned!=planned ||
       (planned && memcmp(tracks[index].slots,day->slots,sizeof(day->slots)))) {
        lv_obj_clean(track);
        lv_obj_set_state(track, LV_STATE_USER_1, planned);
        if (planned) {
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
        tracks[index].marker=surface(track,0,0,2,12,DASH_COLOR_TEXT,0);
        tracks[index].ready=true;
        tracks[index].planned=planned;
        if(planned) memcpy(tracks[index].slots,day->slots,sizeof(day->slots));
    }
    lv_obj_set_hidden(tracks[index].marker,now_minutes<0);
    if (now_minutes >= 0) {
        lv_obj_set_x(tracks[index].marker,now_minutes * (width - 2) / MINUTES_PER_DAY);
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
    label(schedule_screen, "Відключення", title_font, DASH_COLOR_TEXT, 16, 15);
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
    energy_detail_age = label(schedule_screen, "Alerts.energy", small_font, DASH_COLOR_MUTED, 16, 436);
    lv_obj_set_size(energy_detail_age,448,42);
    lv_label_set_long_mode(energy_detail_age,LV_LABEL_LONG_DOT);
}

static void update_energy_cards(const dashboard_data_snapshot_t *data)
{
    const char *supplier=data->energy_selection.supplier_name;
    const char *short_name=strstr(supplier,"ДТЕК") ? "ДТЕК" : strstr(supplier,"ЦЕК") ? "ЦЕК" : supplier;
    char heading[160];
    snprintf(heading,sizeof(heading),"%s %s",short_name,data->energy_selection.queue);
    set_label_if_changed(home.power_heading,heading);
    struct tm local;
    const time_t current = kyiv_clock(&local);
    const int minutes = current ? local.tm_hour * 60 + local.tm_min : -1;
    static time_t displayed_minute = -1;
    static int64_t displayed_revision = -1;
    static int displayed_state = -1;
    static bool displayed_tomorrow;
    static bool displayed_detail;
    static uint32_t displayed_generation=UINT32_MAX;
    const bool detail_visible=lv_screen_active()==schedule_screen;
    if (displayed_minute == current / 60 && displayed_revision == data->energy_updated_us &&
        displayed_state == (int)data->energy_state && displayed_tomorrow == energy_show_tomorrow &&
        displayed_generation==data->energy_generation && displayed_detail==detail_visible) {
        return;
    }
    displayed_minute = current / 60;
    displayed_revision = data->energy_updated_us;
    displayed_state = data->energy_state;
    displayed_tomorrow = energy_show_tomorrow;
    displayed_generation=data->energy_generation;
    displayed_detail=detail_visible;

    char date[11] = {0};
    if (current) {
        strftime(date, sizeof(date), "%Y-%m-%d", &local);
    }
    const dashboard_energy_day_t *today = day_for_date(data, date);
    render_track(energy_home_track, today, minutes);
    render_next_outage(data, today, minutes);
    if(!detail_visible) return;

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
    /* Interval labels depend on the plan, not the minute or HTTP revision. */
    static bool intervals_ready, intervals_present;
    static dashboard_energy_day_t interval_day;
    if(!intervals_ready || intervals_present!=(detail!=NULL) ||
       (detail && (interval_day.state!=detail->state || strcmp(interval_day.date,detail->date) ||
                   memcmp(interval_day.slots,detail->slots,sizeof(detail->slots))))) {
        render_intervals(detail);
        intervals_ready=true;
        intervals_present=detail!=NULL;
        if(detail) interval_day=*detail;
    }
    if (data->energy_updated_us) {
        const long long age = (esp_timer_get_time() - data->energy_updated_us) / 60000000;
        lv_label_set_text_fmt(energy_detail_age, "%s · %s %s\nAlerts.energy · оновлено %lld хв тому",
            data->energy_selection.area_name,short_name,data->energy_selection.queue,age);
    } else lv_label_set_text_fmt(energy_detail_age,"%s · %s %s\nAlerts.energy · очікуємо оновлення",
        data->energy_selection.area_name,short_name,data->energy_selection.queue);
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
    button(wifi_screen, "Назад", 370, 14, 94, show_screen, settings_screen);

    lv_obj_t *card = surface(wifi_screen, 16, 98, CARD_WIDTH, 320, DASH_COLOR_PANEL, 12);
    wifi_status = label(card, "", body_font, DASH_COLOR_TEXT, 15, 14);
    lv_obj_set_width(wifi_status, CARD_WIDTH - 30);
    lv_obj_set_height(wifi_status, 40);
    lv_label_set_long_mode(wifi_status, LV_LABEL_LONG_DOT);
    wifi_connection_fields = surface(card, 15, 58, CARD_WIDTH - 30, 148, DASH_COLOR_PANEL, 0);
    lv_obj_set_style_border_width(wifi_connection_fields, 0, 0);
    lv_obj_set_style_bg_opa(wifi_connection_fields, LV_OPA_TRANSP, 0);
    label(wifi_connection_fields, "Мережа", small_font, DASH_COLOR_MUTED, 0, 0);
    wifi_network_name = label(wifi_connection_fields, "—", body_font, DASH_COLOR_TEXT, 0, 21);
    lv_obj_set_width(wifi_network_name, CARD_WIDTH - 30);
    lv_obj_set_height(wifi_network_name, 44);
    lv_label_set_long_mode(wifi_network_name, LV_LABEL_LONG_DOT);
    wifi_ip_address = label(wifi_connection_fields, "IP-адреса: —", body_font, DASH_COLOR_TEXT, 0, 78);
    wifi_signal = label(wifi_connection_fields, "Сигнал: —", body_font, DASH_COLOR_TEXT, 0, 112);
    wifi_instructions = label(card, "", body_font, DASH_COLOR_MUTED, 15, 210);
    lv_obj_set_width(wifi_instructions, CARD_WIDTH - 30);
    wifi_change_button = button(card, "Змінити мережу", 15, 260, 176, request_setup, NULL);
    lv_obj_set_height(wifi_change_button, 44);
}

static bool setup_in_progress(int state)
{
    return state == DASH_WIFI_SETUP || state == DASH_WIFI_SETUP_FAILED;
}

static void add_settings_screen(void)
{
    label(settings_screen, "Налаштування", title_font, DASH_COLOR_TEXT, 16, 15);
    button(settings_screen, "Назад", 370, 14, 94, show_screen, home_screen);
    lv_obj_t *display = button(settings_screen, "Екран", 16, 86, CARD_WIDTH,
                                show_screen, display_settings_screen);
    lv_obj_set_height(display, 64);
    lv_obj_t *wifi = button(settings_screen, "Налаштування Wi-Fi", 16, 166, CARD_WIDTH,
                             show_screen, wifi_screen);
    lv_obj_set_height(wifi, 64);
    lv_obj_t *system = button(settings_screen, "Система", 16, 246, CARD_WIDTH,
                              show_screen, system_screen);
    lv_obj_set_height(system, 64);
    lv_obj_t *location = button(settings_screen, "Локація", 16, 326, CARD_WIDTH,
                               show_screen, location_screen);
    lv_obj_set_height(location, 64);
    lv_obj_t *energy=button(settings_screen,"Відключення",16,406,CARD_WIDTH,open_energy_settings,NULL);
    lv_obj_set_height(energy,56);
}

static void select_city(lv_event_t *event)
{
    const dashboard_city_t *city=lv_obj_get_user_data(lv_event_get_target_obj(event));
    if(!city) return;
    const esp_err_t result=dashboard_data_select_location(city);
    if(result!=ESP_OK) {
        lv_label_set_text(location_error,"Не вдалося зберегти. Спробуйте ще раз.");
        return;
    }
    lv_label_set_text(location_error,"");
    lv_screen_load(home_screen);
    update_remote_data(NULL);
}

/* Native strokes avoid requiring a checkmark glyph in the bundled text font. */
static void draw_selected_city(lv_event_t *event)
{
    lv_obj_t *row=lv_event_get_target_obj(event);
    if(lv_obj_get_user_data(row)!=selected_city || !selected_city) return;
    lv_area_t bounds;
    lv_obj_get_coords(row,&bounds);
    const int x=bounds.x2-25, y=(bounds.y1+bounds.y2)/2;
    lv_draw_line_dsc_t stroke;
    lv_draw_line_dsc_init(&stroke);
    stroke.color=lv_obj_get_style_text_color(lv_obj_get_child(row,0),LV_PART_MAIN);
    stroke.width=2;
    stroke.round_start=stroke.round_end=true;
    stroke.p1=(lv_point_precise_t){x,y};
    stroke.p2=(lv_point_precise_t){x+5,y+5};
    lv_draw_line(lv_event_get_layer(event),&stroke);
    stroke.p1=stroke.p2;
    stroke.p2=(lv_point_precise_t){x+15,y-6};
    lv_draw_line(lv_event_get_layer(event),&stroke);
}

/* Keep only the visible window of rows, independent of the territory's city count. */
static void refresh_city_rows(lv_event_t *event)
{
    if(city_rows_updating || !city_region) return;
    city_rows_updating=true;
    int first=lv_obj_get_scroll_y(city_list)/58;
    if(first<0) first=0;
    if(first>=(int)city_count) first=city_count ? (int)city_count-1 : 0;
    if(city_rows[0] && lv_obj_get_user_data(city_rows[0]) && lv_obj_get_style_y(city_rows[0],LV_PART_MAIN)==first*58) {
        city_rows_updating=false;
        return;
    }
    const dashboard_city_t *visible[8]={0};
    unsigned index=0;
    for(size_t i=0;i<dashboard_city_count();++i) {
        const dashboard_city_t *city=dashboard_city_at(i);
        if(city->region!=city_region->code) continue;
        if(index>=(unsigned)first && index<(unsigned)first+8) visible[index-(unsigned)first]=city;
        ++index;
    }
    for(unsigned i=0;i<8;++i) {
        lv_obj_t *row=city_rows[i];
        if(!row) continue;
        const dashboard_city_t *city=visible[i];
        lv_obj_set_user_data(row,(void *)city);
        lv_obj_set_hidden(row,!city);
        if(!city) continue;
        lv_obj_set_y(row,(first+(int)i)*58);
        set_label_if_changed(lv_obj_get_child(row,0),city->name);
        lv_obj_invalidate(row);
    }
    city_rows_updating=false;
}

static void open_city_list(lv_event_t *event)
{
    city_rows_updating=true;
    city_region=lv_obj_get_user_data(lv_event_get_target_obj(event));
    lv_obj_clean(city_list);
    memset(city_rows,0,sizeof(city_rows));
    city_count=0;
    for(size_t i=0;i<dashboard_city_count();++i)
        if(dashboard_city_at(i)->region==city_region->code) ++city_count;
    lv_obj_scroll_to_y(city_list,0,LV_ANIM_OFF);
    lv_label_set_text(city_heading,city_region->name);
    lv_label_set_text(location_error,"");
    city_extent=surface(city_list,0,0,1,(int)city_count*58,DASH_COLOR_BACKGROUND,0);
    const unsigned rows=city_count<8 ? city_count : 8;
    for(unsigned i=0;i<rows;++i) {
        city_rows[i]=button(city_list,"",0,(int)i*58,430,select_city,NULL);
        lv_obj_set_height(city_rows[i],50);
        lv_obj_add_event_cb(city_rows[i],draw_selected_city,LV_EVENT_DRAW_POST,NULL);
        lv_obj_t *name=lv_obj_get_child(city_rows[i],0);
        lv_obj_set_width(name,374);
        lv_obj_align(name,LV_ALIGN_LEFT_MID,0,0);
        lv_obj_set_height(name,lv_font_get_line_height(body_font));
        lv_label_set_long_mode(name,LV_LABEL_LONG_DOT);
    }
    city_rows_updating=false;
    refresh_city_rows(NULL);
    ESP_LOGI("dashboard_screen","City list total=%u rows=%u internal_free=%u largest=%u psram_free=%u",
             city_count,rows,(unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    lv_screen_load(city_screen);
}

static void refresh_location_rows(lv_event_t *event)
{
    if(location_rows_updating || !location_extent) return;
    location_rows_updating=true;
    int first=lv_obj_get_scroll_y(location_list)/58;
    if(first<0) first=0;
    const unsigned count=(unsigned)dashboard_region_count();
    if(first>=(int)count) first=(int)count-1;
    if(location_rows[0] && lv_obj_get_user_data(location_rows[0])==dashboard_region_at((size_t)first)) {
        location_rows_updating=false;
        return;
    }
    for(unsigned i=0;i<8;++i) {
        const dashboard_region_t *region=dashboard_region_at((size_t)first+i);
        lv_obj_t *row=location_rows[i];
        lv_obj_set_user_data(row,(void *)region);
        lv_obj_set_hidden(row,!region);
        if(!region) continue;
        lv_obj_set_y(row,(first+(int)i)*58);
        set_label_if_changed(lv_obj_get_child(row,0),region->name);
    }
    location_rows_updating=false;
}

static void prepare_location_list(void)
{
    if(lv_obj_get_child_count(location_list)) return;
    location_rows_updating=true;
    lv_obj_scroll_to_y(location_list,0,LV_ANIM_OFF);
    location_extent=surface(location_list,0,0,1,(int)dashboard_region_count()*58,DASH_COLOR_BACKGROUND,0);
    for(unsigned i=0;i<8;++i) {
        location_rows[i]=button(location_list,"",0,(int)i*58,430,open_city_list,NULL);
        lv_obj_set_height(location_rows[i],50);
        lv_obj_t *name=lv_obj_get_child(location_rows[i],0);
        lv_obj_set_width(name,398);
        lv_obj_set_height(name,lv_font_get_line_height(body_font));
        lv_label_set_long_mode(name,LV_LABEL_LONG_DOT);
    }
    location_rows_updating=false;
    refresh_location_rows(NULL);
}

static lv_obj_t *location_scroller(lv_obj_t *screen,int y,int height)
{
    lv_obj_t *list=surface(screen,16,y,CARD_WIDTH,height,DASH_COLOR_BACKGROUND,0);
    lv_obj_set_scrollable(list,true);
    lv_obj_set_scroll_dir(list,LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list,LV_SCROLLBAR_MODE_AUTO);
    return list;
}

static void add_location_screens(void)
{
    label(location_screen,"Локація",title_font,DASH_COLOR_TEXT,16,15);
    button(location_screen,"Назад",370,14,94,show_screen,settings_screen);
    location_current=label(location_screen,"",body_font,DASH_COLOR_MUTED,16,60);
    lv_obj_set_width(location_current,440);
    lv_label_set_long_mode(location_current,LV_LABEL_LONG_DOT);
    location_list=location_scroller(location_screen,98,352);
    lv_obj_add_event_cb(location_list,refresh_location_rows,LV_EVENT_SCROLL,NULL);
    label(city_screen,"Місто",title_font,DASH_COLOR_TEXT,16,15);
    button(city_screen,"Назад",370,14,94,show_screen,location_screen);
    city_heading=label(city_screen,"",body_font,DASH_COLOR_MUTED,16,60);
    lv_obj_set_width(city_heading,440);
    lv_label_set_long_mode(city_heading,LV_LABEL_LONG_DOT);
    city_list=location_scroller(city_screen,98,316);
    lv_obj_add_event_cb(city_list,refresh_city_rows,LV_EVENT_SCROLL,NULL);
    location_error=label(city_screen,"",small_font,DASH_COLOR_ALARM,16,426);
    lv_obj_set_width(location_error,440);
    label(location_screen,"Погода й тривоги · відключення окремо",small_font,DASH_COLOR_MUTED,16,456);
}

/* Energy selection wizard: intermediate choices never publish the active schedule. */
static void select_energy_row(lv_event_t *event);

static void refresh_energy_rows(lv_event_t *event)
{
    if(energy_rows_updating || !energy_ui_catalog || !energy_loaded) return;
    energy_rows_updating=true;
    int first=lv_obj_get_scroll_y(energy_settings_list)/58;
    if(first<0) first=0;
    if(first>=(int)energy_ui_catalog->count) first=energy_ui_catalog->count ? (int)energy_ui_catalog->count-1 : 0;
    if(energy_rows[0] && lv_obj_get_user_data(energy_rows[0]) && lv_obj_get_style_y(energy_rows[0],LV_PART_MAIN)==first*58) {
        energy_rows_updating=false;
        return;
    }
    for(unsigned i=0;i<8;++i) {
        lv_obj_t *row=energy_rows[i];
        if(!row) continue;
        const unsigned index=(unsigned)first+i;
        dashboard_energy_entry_t *entry=index<energy_ui_catalog->count ? &energy_ui_catalog->entries[index] : NULL;
        lv_obj_set_user_data(row,entry);
        lv_obj_set_hidden(row,!entry);
        if(!entry) continue;
        lv_obj_set_y(row,(first+(int)i)*58);
        set_label_if_changed(lv_obj_get_child(row,0),entry->name);
    }
    energy_rows_updating=false;
}

static void request_energy_stage(void)
{
    energy_loaded=0;
    /* No deletion inside a row's touch callback; the UI timer replaces old rows. */
    for(unsigned i=0;i<8;++i) if(energy_rows[i]) {
        lv_obj_set_user_data(energy_rows[i],NULL);
        lv_obj_set_hidden(energy_rows[i],true);
    }
    const char *title=energy_stage==DASH_ENERGY_AREAS ? "Область" : energy_stage==DASH_ENERGY_SUPPLIERS ? "Оператор" : "Черга";
    lv_label_set_text(energy_settings_heading,title);
    char context[288];
    if(energy_stage==DASH_ENERGY_AREAS) snprintf(context,sizeof(context),"Зараз: %s · %s · %s",
        energy_draft.area_name,energy_draft.supplier_name,energy_draft.queue);
    else if(energy_stage==DASH_ENERGY_SUPPLIERS) snprintf(context,sizeof(context),"%s",energy_draft.area_name);
    else snprintf(context,sizeof(context),"%s · %s",energy_draft.area_name,energy_draft.supplier_name);
    lv_label_set_text(energy_settings_context,context);
    if(!energy_ui_catalog) energy_ui_catalog=calloc(1,sizeof(*energy_ui_catalog));
    if(!energy_ui_catalog) {
        energy_request=0;
        lv_label_set_text(energy_settings_note,"Недостатньо пам’яті. Спробуйте ще раз.");
        lv_obj_set_hidden(energy_settings_retry,false);
        return;
    }
    energy_request=dashboard_data_energy_catalog_request(energy_stage,energy_draft.area,energy_draft.initiator);
    lv_label_set_text(energy_settings_note,energy_request ? "Завантаження…" : "Недостатньо пам’яті. Спробуйте ще раз.");
    lv_obj_set_hidden(energy_settings_retry,true);
}

static void energy_back(lv_event_t *event)
{
    if(energy_stage==DASH_ENERGY_AREAS) lv_screen_load(settings_screen);
    else { --energy_stage; request_energy_stage(); }
}

static void retry_energy_catalog(lv_event_t *event) { request_energy_stage(); }

static void select_energy_row(lv_event_t *event)
{
    const dashboard_energy_entry_t *entry=lv_obj_get_user_data(lv_event_get_target_obj(event));
    if(!entry || !energy_loaded) return;
    if(energy_stage==DASH_ENERGY_AREAS) {
        snprintf(energy_draft.area,sizeof(energy_draft.area),"%s",entry->id);
        snprintf(energy_draft.area_name,sizeof(energy_draft.area_name),"%s",entry->name);
        energy_stage=DASH_ENERGY_SUPPLIERS;
        request_energy_stage();
    } else if(energy_stage==DASH_ENERGY_SUPPLIERS) {
        snprintf(energy_draft.initiator,sizeof(energy_draft.initiator),"%s",entry->id);
        snprintf(energy_draft.supplier_name,sizeof(energy_draft.supplier_name),"%s",entry->name);
        energy_stage=DASH_ENERGY_QUEUES;
        request_energy_stage();
    } else {
        const size_t queue_length=strlen(entry->id);
        if(queue_length>=sizeof(energy_draft.queue)) return;
        memcpy(energy_draft.queue,entry->id,queue_length+1);
        if(dashboard_data_select_energy(&energy_draft)!=ESP_OK) {
            lv_label_set_text(energy_settings_note,"Не вдалося зберегти. Спробуйте ще раз.");
            return;
        }
        lv_screen_load(home_screen);
        update_remote_data(NULL);
    }
}

static void update_energy_settings(lv_timer_t *timer)
{
    if(lv_screen_active()!=energy_settings_screen) {
        if(energy_ui_catalog) {
            energy_rows_updating=true;
            lv_obj_clean(energy_settings_list);
            memset(energy_rows,0,sizeof(energy_rows));
            free(energy_ui_catalog); energy_ui_catalog=NULL;
            energy_loaded=energy_request=0;
            dashboard_data_energy_catalog_cancel();
            energy_rows_updating=false;
        }
        return;
    }
    dashboard_data_state_t state=DASH_DATA_UNAVAILABLE;
    if(!energy_request || !dashboard_data_energy_catalog_copy(energy_request,NULL,&state)) {
        lv_obj_set_hidden(energy_settings_retry,false);
        return;
    }
    if(state!=DASH_DATA_FRESH) {
        lv_label_set_text(energy_settings_note,state==DASH_DATA_WAITING ? "Завантаження · очікуємо зв’язку або повтору" :
            state==DASH_DATA_NO_TOKEN ? "Потрібен ключ Alerts.energy" :
            state==DASH_DATA_AUTH_ERROR ? "Перевірте ключ і доступ API за підпискою" : "Не вдалося отримати список");
        lv_obj_set_hidden(energy_settings_retry,state==DASH_DATA_WAITING);
        return;
    }
    if(energy_loaded==energy_request) return;
    if(!dashboard_data_energy_catalog_copy(energy_request,energy_ui_catalog,&state) || state!=DASH_DATA_FRESH) return;
    energy_rows_updating=true;
    lv_obj_clean(energy_settings_list);
    memset(energy_rows,0,sizeof(energy_rows));
    const unsigned count=energy_ui_catalog->count;
    surface(energy_settings_list,0,0,1,(int)count*58,DASH_COLOR_BACKGROUND,0);
    for(unsigned i=0;i<8 && i<count;++i) {
        energy_rows[i]=button(energy_settings_list,"",0,(int)i*58,430,select_energy_row,NULL);
        lv_obj_set_height(energy_rows[i],50);
        lv_obj_t *name=lv_obj_get_child(energy_rows[i],0);
        lv_obj_set_width(name,398);
        lv_obj_set_height(name,lv_font_get_line_height(body_font));
        lv_label_set_long_mode(name,LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    }
    int first=0;
    if(energy_stage==DASH_ENERGY_AREAS) {
        dashboard_data_snapshot_t data;
        dashboard_data_snapshot(&data);
        if(!data.energy_configured && selected_city) {
            const int hint=dashboard_energy_area_hint(energy_ui_catalog,selected_city->id);
            if(hint>=0) first=hint;
        } else for(unsigned i=0;i<count;++i)
            if(!strcmp(energy_ui_catalog->entries[i].id,data.energy_selection.area)) first=(int)i;
    }
    lv_obj_scroll_to_y(energy_settings_list,first*58,LV_ANIM_OFF);
    energy_loaded=energy_request;
    energy_rows_updating=false;
    refresh_energy_rows(NULL);
    lv_label_set_text(energy_settings_note,count ? "Оберіть зі списку" : "Для цього вибору список поки порожній");
    lv_obj_set_hidden(energy_settings_retry,false);
}

static void open_energy_settings(lv_event_t *event)
{
    dashboard_data_snapshot_t data;
    dashboard_data_snapshot(&data);
    energy_draft=data.energy_selection;
    energy_stage=DASH_ENERGY_AREAS;
    request_energy_stage();
    lv_screen_load(energy_settings_screen);
}

static void add_energy_settings_screen(void)
{
    energy_settings_heading=label(energy_settings_screen,"Область",title_font,DASH_COLOR_TEXT,16,15);
    button(energy_settings_screen,"Назад",370,14,94,energy_back,NULL);
    energy_settings_context=label(energy_settings_screen,"",small_font,DASH_COLOR_MUTED,16,56);
    lv_obj_set_size(energy_settings_context,448,44);
    lv_label_set_long_mode(energy_settings_context,LV_LABEL_LONG_DOT);
    energy_settings_list=location_scroller(energy_settings_screen,110,292);
    lv_obj_add_event_cb(energy_settings_list,refresh_energy_rows,LV_EVENT_SCROLL,NULL);
    energy_settings_note=label(energy_settings_screen,"",small_font,DASH_COLOR_MUTED,16,416);
    lv_obj_set_size(energy_settings_note,320,52);
    energy_settings_retry=button(energy_settings_screen,"Оновити",350,420,114,retry_energy_catalog,NULL);
}

static const char *source_state(dashboard_data_state_t state)
{
    switch (state) {
        case DASH_DATA_FRESH: return "Актуально";
        case DASH_DATA_STALE: return "Застаріло";
        case DASH_DATA_NO_TOKEN: return "Потрібен ключ API";
        case DASH_DATA_AUTH_ERROR: return "Помилка ключа API";
        case DASH_DATA_UNAVAILABLE: return "Помилка оновлення";
        default: return "Очікування оновлення";
    }
}

static void update_system(lv_timer_t *timer)
{
    if (lv_screen_active() != system_screen) return;
    dashboard_data_snapshot_t data;
    dashboard_data_snapshot(&data);
    dashboard_recovery_t recovery;
    dashboard_data_recovery(&recovery);
    const int64_t now = esp_timer_get_time();
    const int64_t updated[] = { data.weather_updated_us, data.alerts_updated_us,
                              data.alert_details_updated_us, data.energy_updated_us };
    const dashboard_data_state_t states[] = { data.weather_state, data.alerts_state,
                                             data.alert_details_state, data.energy_state };
    char energy_name[160];
    snprintf(energy_name,sizeof(energy_name),"%s %s",data.energy_selection.supplier_name,data.energy_selection.queue);
    const char *const names[] = { "Погода", "Карта тривог", "Типи загроз", energy_name };
    for (int i = 0; i < 4; ++i) {
        char age[64];
        const long long seconds = updated[i] && now >= updated[i] ? (now - updated[i]) / 1000000 : 0;
        if (!updated[i]) snprintf(age, sizeof(age), "ще не оновлено");
        else if (seconds < 60) snprintf(age, sizeof(age), "%lld с тому", seconds);
        else if (seconds < 3600) snprintf(age, sizeof(age), "%lld хв тому", seconds / 60);
        else snprintf(age, sizeof(age), "%lld год тому", seconds / 3600);
        lv_label_set_text_fmt(system_sources[i], "%s · %s\n%s", names[i], age, source_state(states[i]));
    }
    const char *status = recovery.state == DASH_RECOVERY_FAILED
        ? "Оновлення не відповідає. Перезавантаж пристрій."
        : recovery.pending ? "Перезапускаємо оновлення…"
        : dashboard_wifi_state() != DASH_WIFI_CONNECTED ? "Немає Wi-Fi. Очікуємо підключення."
        : time(NULL) < 1700000000 ? "Очікуємо синхронізації часу…"
        : recovery.restarts ? "Оновлення перезапущено. Результати — вище."
        : "Оновлення працює автоматично.";
    lv_label_set_text(system_status, status);
    const bool cooldown = recovery.restarts && now - recovery.restarted_us < DASH_RECOVERY_COOLDOWN_US;
    lv_obj_set_state(system_restart_button, LV_STATE_DISABLED,
                     recovery.pending || cooldown || recovery.state == DASH_RECOVERY_FAILED);
}

static void restart_updates(lv_event_t *event)
{
    if (!dashboard_data_restart()) {
        lv_label_set_text(system_status, "Перезапуск зараз недоступний. Спробуй пізніше.");
        return;
    }
    update_system(NULL);
}

static void reboot_device(lv_timer_t *timer)
{
    esp_restart();
}

static void confirm_reboot(lv_event_t *event)
{
    if (reboot_queued) return;
    lv_timer_t *timer = lv_timer_create(reboot_device, 300, NULL);
    if (!timer) {
        lv_label_set_text(reboot_status, "Не вдалося почати перезавантаження.");
        return;
    }
    lv_timer_set_repeat_count(timer, 1);
    reboot_queued = true;
    lv_obj_add_state(reboot_confirm_button, LV_STATE_DISABLED);
    lv_label_set_text(reboot_status, "Перезавантажуємо…");
}

static void add_system_screen(void)
{
    label(system_screen, "Система", title_font, DASH_COLOR_TEXT, 16, 15);
    button(system_screen, "Назад", 370, 14, 94, show_screen, settings_screen);
    label(system_screen, "Останні успішні оновлення", small_font, DASH_COLOR_MUTED, 16, 61);
    for (int i = 0; i < 4; ++i) {
        system_sources[i] = label(system_screen, "", small_font, DASH_COLOR_TEXT, 16, 89 + i * 41);
        lv_obj_set_size(system_sources[i], CARD_WIDTH, 38);
    }
    system_status = label(system_screen, "", small_font, DASH_COLOR_WARNING, 16, 260);
    lv_obj_set_size(system_status, CARD_WIDTH, 43);
    system_restart_button = button(system_screen, "Перезапустити оновлення", 16, 314, CARD_WIDTH,
                                    restart_updates, NULL);
    lv_obj_set_height(system_restart_button, 52);
    lv_obj_t *reboot = button(system_screen, "Перезавантажити пристрій", 16, 380, CARD_WIDTH,
                              show_screen, reboot_screen);
    lv_obj_set_height(reboot, 52);
    label(system_screen, "Wi-Fi, ключі й тема зберігаються", small_font, DASH_COLOR_MUTED, 16, 448);

    label(reboot_screen, "Перезавантаження", title_font, DASH_COLOR_TEXT, 16, 15);
    reboot_status = label(reboot_screen,
        "Перезавантажити пристрій?\n\nWi-Fi, ключі API та налаштування\nзалишаться збереженими.",
        body_font, DASH_COLOR_TEXT, 16, 102);
    lv_obj_set_width(reboot_status, CARD_WIDTH);
    reboot_confirm_button = button(reboot_screen, "Перезавантажити", 16, 270, CARD_WIDTH,
                                    confirm_reboot, NULL);
    lv_obj_set_height(reboot_confirm_button, 56);
    lv_obj_t *cancel = button(reboot_screen, "Скасувати", 16, 344, CARD_WIDTH, show_screen, system_screen);
    lv_obj_set_height(cancel, 56);
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
    lv_obj_set_hidden(wifi_connection_fields, configuring);
    lv_obj_set_y(wifi_instructions, configuring ? 58 : 210);
    update_wifi_connection(NULL);
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

static void update_wifi_connection(lv_timer_t *timer)
{
    if (lv_screen_active() != wifi_screen) return;
    dashboard_wifi_connection_t connection;
    dashboard_wifi_connection(&connection);
    set_label_if_changed(wifi_network_name, connection.ssid[0] ? connection.ssid : "—");
    char text[64];
    snprintf(text, sizeof(text), "IP-адреса: %s", connection.ipv4[0] ? connection.ipv4 : "—");
    set_label_if_changed(wifi_ip_address, text);
    if (connection.associated) {
        snprintf(text, sizeof(text), "Сигнал: %d dBm", connection.rssi_dbm);
    } else {
        snprintf(text, sizeof(text), "Сигнал: —");
    }
    set_label_if_changed(wifi_signal, text);
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
        switch (data->region_alerts[data->location->region]) {
        case 'R':
            status = "Повітряна тривога";
            color = DASH_COLOR_ALARM;
            break;
        case 'Y':
            status = "Попередня загроза";
            color = DASH_COLOR_WARNING;
            break;
        case 'P':
            status = data->location->region==30 || data->location->region==40
                     ? "Часткова тривога на території" : "Часткова тривога в області";
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
        if (theme->home_layout != DASH_HOME_STANDARD) {
            lv_obj_invalidate(home.alert_card);
        }
    }
    /* Choose final strings before touching LVGL; intermediate defaults cause allocation churn. */
    const char *detail_note=NULL,*threat_note="Торкніться карти ›";
    char note_text[96],threat_text[96];
    if(data->alerts_state==DASH_DATA_FRESH && data->region_alerts[data->location->region]!='N') {
        if(data->alert_details_state!=DASH_DATA_FRESH) threat_note="Типи загроз недоступні";
        else {
            const dashboard_alert_record_t *primary=dashboard_alert_primary(&screen_alert_details,
                data->region_alerts[data->location->region]);
            if(primary) {
                char clock[6];
                const int64_t minutes=dashboard_alert_elapsed(primary->started_at,(int64_t)time(NULL));
                if(minutes>=0 && dashboard_alert_kyiv_clock(primary->started_at,clock)) {
                    snprintf(note_text,sizeof(note_text),"Від %s · %lld хв",clock,(long long)minutes);
                    detail_note=note_text;
                } else detail_note="Час початку невідомий";
                if(primary->threat_count) {
                    snprintf(threat_text,sizeof(threat_text),"%s%s",dashboard_alert_threat_name(primary->threats[0]),
                        primary->threat_count>1 || primary->threats_omitted ? " та інші" : "");
                    threat_note=threat_text;
                } else threat_note="Тип загрози не вказано";
            } else if(screen_alert_details.count)
                detail_note=screen_alert_details.count>1 ? "Кілька локальних тривог" : "Окрема локальна тривога";
        }
    }
    if(detail_note) set_label_if_changed(alert_note,detail_note);
    else if(data->alerts_state==DASH_DATA_FRESH || data->alerts_state==DASH_DATA_STALE)
        update_age(alert_note,data->alerts_updated_us,data->alerts_state==DASH_DATA_STALE);
    else set_label_if_changed(alert_note,note);
    set_label_if_changed(alert_threat,threat_note);
}

/* Badges represent territorial threats, not individual target positions. */
static bool place_map_label(lv_obj_t *object, int x, int y, lv_area_t *used, unsigned *count)
{
    lv_obj_update_layout(object);
    const int width = lv_obj_get_width(object), height = lv_obj_get_height(object);
    const lv_area_t box = {x, y, x + width + 3, y + height + 2};
    bool show = x >= 0 && y >= 0 && box.x2 < DASH_MAP_VIEW_WIDTH && box.y2 < DASH_MAP_VIEW_HEIGHT;
    for (unsigned i = 0; show && i < *count; ++i)
        if (box.x1 <= used[i].x2 && box.x2 >= used[i].x1 &&
            box.y1 <= used[i].y2 && box.y2 >= used[i].y1) show = false;
    lv_obj_set_hidden(object, !show);
    if (show) {
        used[(*count)++] = box;
        lv_obj_set_pos(object, x, y);
    }
    return show;
}

static bool place_map_badge(lv_obj_t *object, int x, int y, int width, int height,
                            lv_area_t *used, unsigned *count)
{
    if (place_map_label(object, x-width/2, y-height/2, used, count)) return true;
    /* Nearby offset badges retain a leader to their territory instead of disappearing. */
    for (int radius=1; radius<=3; ++radius) for (int dy=-radius; dy<=radius; ++dy)
        for (int dx=-radius; dx<=radius; ++dx) {
            if (dx!=-radius && dx!=radius && dy!=-radius && dy!=radius) continue;
            if (place_map_label(object, x-width/2+dx*32, y-height/2+dy*32, used, count)) return true;
        }
    for (int top=60; top<420; top+=60) for (int left=4; left<420; left+=84)
        if (place_map_label(object, left, top, used, count)) return true;
    return false;
}

static void draw_map_leaders(lv_event_t *event)
{
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.width=1;
    line.color=lv_color_hex(palette->colors[DASH_COLOR_WARNING]);
    line.opa=LV_OPA_60;
    for (unsigned i=0; i<sizeof(map_threats)/sizeof(map_threats[0]); ++i) {
        if (!map_threats[i] || lv_obj_has_flag(map_threats[i],LV_OBJ_FLAG_HIDDEN)) continue;
        int x,y;
        dashboard_map_project(ua_map_centers[i].x,ua_map_centers[i].y,&x,&y);
        lv_area_t box;
        lv_obj_get_coords(map_threats[i],&box);
        line.p1=(lv_point_precise_t){x,y};
        line.p2=(lv_point_precise_t){(box.x1+box.x2)/2,(box.y1+box.y2)/2};
        lv_draw_line(lv_event_get_layer(event),&line);
    }
}

static void draw_threat_icon(lv_layer_t *layer, dashboard_map_icon_t icon, int x, int y)
{
    lv_draw_triangle_dsc_t fill;
    lv_draw_triangle_dsc_init(&fill);
    fill.color = lv_color_hex(palette->colors[DASH_COLOR_TEXT]);
    lv_draw_line_dsc_t stroke;
    lv_draw_line_dsc_init(&stroke);
    stroke.color = fill.color;
    stroke.width = 2;
    stroke.round_start = stroke.round_end = true;
    const dashboard_icon_asset_t *asset = &dashboard_threat_icons[icon];
    for (unsigned n = 0; n < asset->triangle_count; ++n) {
        const dashboard_icon_triangle_t *t = &asset->triangles[n];
        fill.p[0] = (lv_point_precise_t){x+t->x1,y+t->y1};
        fill.p[1] = (lv_point_precise_t){x+t->x2,y+t->y2};
        fill.p[2] = (lv_point_precise_t){x+t->x3,y+t->y3};
        lv_draw_triangle(layer, &fill);
    }
    for (unsigned n = 0; n < asset->segment_count; ++n) {
        const dashboard_icon_segment_t *line = &asset->segments[n];
        stroke.p1 = (lv_point_precise_t){x+line->x1,y+line->y1};
        stroke.p2 = (lv_point_precise_t){x+line->x2,y+line->y2};
        lv_draw_line(layer, &stroke);
    }
}

static void draw_threat_group(lv_event_t *event)
{
    const unsigned region = (unsigned)(uintptr_t)lv_event_get_user_data(event);
    dashboard_map_icon_t icons[DASH_MAP_ICON_COUNT];
    const unsigned count = dashboard_map_categories(visible_threats[region], icons);
    lv_area_t bounds;
    lv_obj_get_coords(lv_event_get_target_obj(event), &bounds);
    for (unsigned i = 0; i < count; ++i)
        draw_threat_icon(lv_event_get_layer(event), icons[i],
            bounds.x1 + 3 + (int)(i % 3) * DASH_MAP_ICON_SIZE,
            bounds.y1 + 3 + (int)(i / 3) * DASH_MAP_ICON_SIZE);
}

static void draw_map_legend(lv_event_t *event)
{
    lv_area_t bounds;
    lv_obj_get_coords(lv_event_get_target_obj(event), &bounds);
    for (unsigned i = 0; i < DASH_MAP_ICON_COUNT; ++i)
        draw_threat_icon(lv_event_get_layer(event), (dashboard_map_icon_t)i,
            bounds.x1 + 3 + (int)(i % 3) * 112,
            bounds.y1 + 2 + (int)(i / 3) * 28);
}

static void update_full_map(void)
{
    if (!full_map_canvas || !full_map_dirty) return;
    const uint16_t background = lv_color_to_u16(lv_color_hex(palette->colors[DASH_COLOR_PANEL]));
    int columns[DASH_MAP_VIEW_WIDTH];
    for (int x = 0; x < DASH_MAP_VIEW_WIDTH; ++x) {
        int ignored;
        dashboard_map_source(x, DASH_MAP_FIT_TOP, &columns[x], &ignored);
    }
    for (int y = 0; y < DASH_MAP_VIEW_HEIGHT; ++y) {
        uint16_t *dest = lv_draw_buf_goto_xy(&full_map_buffer, 0, y);
        int sx, sy;
        if (!dashboard_map_source(0, y, &sx, &sy)) {
            for (int x = 0; x < DASH_MAP_VIEW_WIDTH; ++x) dest[x] = background;
        } else {
            const uint16_t *src = lv_draw_buf_goto_xy(&map_buffer, 0, sy);
            for (int x = 0; x < DASH_MAP_VIEW_WIDTH; ++x) dest[x] = src[columns[x]];
        }
    }
    lv_obj_invalidate(full_map_canvas);
    lv_area_t occupied[sizeof(ua_map_cities) / sizeof(ua_map_cities[0]) +
                       sizeof(ua_map_centers) / sizeof(ua_map_centers[0]) + 3];
    occupied[0]=(lv_area_t){0,0,479,55};
    occupied[1]=(lv_area_t){404,452,479,479};
    occupied[2]=(lv_area_t){8,408,364,471};
    unsigned count = 3;
    for (unsigned i = 0; i < sizeof(map_threats) / sizeof(map_threats[0]); ++i) {
        const ua_map_region_t *r = &ua_map_centers[i];
        const uint16_t types = visible_threats[r->region];
        if (!types) { lv_obj_set_hidden(map_threats[i], true); continue; }
        dashboard_map_icon_t icons[DASH_MAP_ICON_COUNT];
        const unsigned icon_count = dashboard_map_categories(types, icons);
        if (!icon_count) { lv_obj_set_hidden(map_threats[i], true); continue; }
        int width, height;
        dashboard_map_group_size(icon_count, &width, &height);
        lv_obj_set_size(map_threats[i], width, height);
        lv_obj_set_hidden(map_threats[i], false);
        lv_obj_set_style_border_color(map_threats[i], lv_color_hex(palette->colors[DASH_COLOR_WARNING]), 0);
        int x, y;
        dashboard_map_project(r->x, r->y, &x, &y);
        place_map_badge(map_threats[i], x, y, width, height, occupied, &count);
        lv_obj_invalidate(map_threats[i]);
    }
    for (unsigned i = 0; i < sizeof(map_cities) / sizeof(map_cities[0]); ++i) {
        int x, y;
        dashboard_map_project(ua_map_cities[i].x, ua_map_cities[i].y, &x, &y);
        lv_obj_set_hidden(map_cities[i], false);
        lv_obj_update_layout(map_cities[i]);
        const int name_width=lv_obj_get_width(map_cities[i]);
        const int name_x=x+name_width+10<DASH_MAP_VIEW_WIDTH ? x+6 : x-name_width-6;
        const bool visible = place_map_label(map_cities[i], name_x, y-9, occupied, &count);
        lv_obj_set_hidden(map_cities[i], !visible);
        lv_obj_set_hidden(map_city_dots[i], !visible);
        if (visible) lv_obj_set_pos(map_city_dots[i], x - 2, y - 2);
    }
    if(full_map_selected) {
        int x,y;
        dashboard_map_project(selected_city->map_x,selected_city->map_y,&x,&y);
        lv_obj_set_pos(full_map_selected,x-4,y-4);
    }
    full_map_dirty = false;
}

static void open_map(lv_event_t *event)
{
    (void)event;
    if (!map_canvas) return;
    if (!full_map_canvas) {
        const uint32_t stride = lv_draw_buf_width_to_stride(DASH_MAP_VIEW_WIDTH, LV_COLOR_FORMAT_RGB565);
        const uint32_t bytes = LV_DRAW_BUF_SIZE(DASH_MAP_VIEW_WIDTH, DASH_MAP_VIEW_HEIGHT, LV_COLOR_FORMAT_RGB565);
        void *storage = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!storage || lv_draw_buf_init(&full_map_buffer, DASH_MAP_VIEW_WIDTH, DASH_MAP_VIEW_HEIGHT,
                LV_COLOR_FORMAT_RGB565, stride, storage, bytes) != LV_RESULT_OK) {
            heap_caps_free(storage);
            set_label_if_changed(alert_threat, "Для карти бракує пам’яті");
            return;
        }
        full_map_canvas = lv_canvas_create(full_map_viewport);
        lv_canvas_set_draw_buf(full_map_canvas, &full_map_buffer);
        lv_obj_set_clickable(full_map_canvas, false);
        lv_obj_set_scrollable(full_map_canvas, false);
        lv_obj_add_event_cb(full_map_canvas, draw_map_leaders, LV_EVENT_DRAW_POST, NULL);
        for (unsigned i = 0; i < sizeof(map_threats) / sizeof(map_threats[0]); ++i) {
            map_threats[i] = surface(full_map_viewport, 0, 0, 30, 30, DASH_COLOR_PANEL, 5);
            lv_obj_set_style_radius(map_threats[i], 5, 0);
            lv_obj_set_style_border_width(map_threats[i], 1, 0);
            lv_obj_add_event_cb(map_threats[i], draw_threat_group, LV_EVENT_DRAW_POST,
                                (void *)(uintptr_t)ua_map_centers[i].region);
        }
        for (unsigned i = 0; i < sizeof(map_cities) / sizeof(map_cities[0]); ++i) {
            map_city_dots[i] = surface(full_map_viewport, 0, 0, 5, 5, DASH_COLOR_MAP_MARKER, 3);
            map_cities[i] = label(full_map_viewport, ua_map_cities[i].name, small_font, DASH_COLOR_TEXT, 0, 0);
        }
    }
    if(!full_map_selected)
        full_map_selected=surface(full_map_viewport,0,0,9,9,DASH_COLOR_MAP_MARKER,5);
    full_map_dirty = true;
    lv_screen_load(map_screen);
    update_remote_data(NULL);
}

static void add_map_screen(void)
{
    /* Overlay controls stay above the later canvas because the viewport is the first sibling. */
    full_map_viewport = surface(map_screen, 0, 0, DASH_MAP_VIEW_WIDTH, DASH_MAP_VIEW_HEIGHT, DASH_COLOR_PANEL, 0);
    lv_obj_set_style_border_width(full_map_viewport, 0, 0);
    lv_obj_set_style_radius(full_map_viewport, 0, 0);
    button(map_screen, "‹ Назад", 12, 10, 104, show_screen, home_screen);
    full_map_note = label(map_screen, "", small_font, DASH_COLOR_MUTED, 140, 18);
    lv_obj_set_width(full_map_note, 328);
    lv_label_set_long_mode(full_map_note, LV_LABEL_LONG_DOT);
    lv_obj_t *legend = surface(map_screen, 12, 412, 348, 56, DASH_COLOR_PANEL, 6);
    lv_obj_set_style_border_width(legend, 0, 0);
    lv_obj_set_style_radius(legend, 6, 0);
    static const char *const names[DASH_MAP_ICON_COUNT] = {
        [DASH_MAP_ICON_ROCKET]="Ракети", [DASH_MAP_ICON_UAV]="БпЛА",
        [DASH_MAP_ICON_AIRCRAFT]="Авіація", [DASH_MAP_ICON_BOMB]="КАБ",
        [DASH_MAP_ICON_SHIELD]="ППО", [DASH_MAP_ICON_UNKNOWN]="Інше",
    };
    for (unsigned i = 0; i < DASH_MAP_ICON_COUNT; ++i)
        label(legend, names[i], small_font, DASH_COLOR_MUTED,
            30 + (int)(i % 3) * 112, 4 + (int)(i / 3) * 28);
    lv_obj_add_event_cb(legend, draw_map_legend, LV_EVENT_DRAW_POST, NULL);
    label(map_screen, "© OSM", small_font, DASH_COLOR_MUTED, 416, 460);
}

static void update_remote_data(lv_timer_t *timer)
{
    /* Timer dispatch occurs after touch callbacks, so the clicked row is no longer in use. */
    if(timer && lv_screen_active()!=city_screen && lv_obj_get_child_count(city_list)) {
        city_rows_updating=true;
        city_region=NULL;
        lv_obj_clean(city_list);
        memset(city_rows,0,sizeof(city_rows));
        city_extent=NULL;
        city_count=0;
        city_rows_updating=false;
        ESP_LOGI("dashboard_screen","City rows released internal_free=%u largest=%u psram_free=%u",
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    }
    if(timer && lv_screen_active()!=location_screen && lv_obj_get_child_count(location_list)) {
        location_rows_updating=true;
        lv_obj_clean(location_list);
        memset(location_rows,0,sizeof(location_rows));
        location_extent=NULL;
        location_rows_updating=false;
        ESP_LOGI("dashboard_screen","Region rows released internal_free=%u largest=%u psram_free=%u",
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    }
    dashboard_data_snapshot_t data;
    dashboard_data_snapshot(&data);
    if(selected_generation!=data.location_generation) {
        selected_city=data.location;
        selected_generation=data.location_generation;
        display_mode.alert_known=false;
        set_label_if_changed(home.city,selected_city->name);
        set_label_if_changed(home.alert_heading,dashboard_region_find(selected_city->region)->name);
        char current[128];
        snprintf(current,sizeof(current),"Обрано: %s",selected_city->name);
        set_label_if_changed(location_current,current);
        if(home.map_marker) lv_obj_set_pos(home.map_marker,12+selected_city->map_x-4,78+selected_city->map_y-4);
        full_map_dirty=true;
        lv_obj_invalidate(home_screen);
    }
    if (!dashboard_data_alert_details(&screen_alert_details, &data)) data.alert_details_state = DASH_DATA_STALE;
    const int64_t now_ms = esp_timer_get_time() / 1000;
    dashboard_display_idle(&display_mode, now_ms);
    if (dashboard_display_alert(&display_mode, data.alerts_state == DASH_DATA_FRESH,
                                data.region_alerts[data.location->region], now_ms) &&
        !setup_in_progress(dashboard_wifi_state())) {
        lv_screen_load(home_screen);
    }
    refresh_display_mode();
    lv_obj_t *active = lv_screen_active();
    if (active == map_screen) {
        map_stale = data.alerts_state != DASH_DATA_FRESH;
        if (map_canvas) ESP_ERROR_CHECK_WITHOUT_ABORT(paint_map(data.region_alerts));
        uint16_t threats[DASHBOARD_REGION_COUNT] = {0};
        if (!map_stale && data.alert_details_state == DASH_DATA_FRESH)
            memcpy(threats, screen_alert_details.region_threats, sizeof(threats));
        /* No icons on areas that have already cleared in the current live map. */
        for (unsigned i = 0; i < DASHBOARD_REGION_COUNT; ++i)
            if (data.region_alerts[i] != 'R' && data.region_alerts[i] != 'Y' && data.region_alerts[i] != 'P') threats[i] = 0;
        if (memcmp(threats, visible_threats, sizeof(threats))) {
            memcpy(visible_threats, threats, sizeof(threats));
            full_map_dirty = true;
        }
        set_label_if_changed(full_map_note, data.alerts_state == DASH_DATA_NO_TOKEN ? "Потрібен токен Alerts.in.ua" :
            data.alerts_state == DASH_DATA_AUTH_ERROR ? "Токен Alerts.in.ua не прийнято" :
            map_stale ? (data.alerts_updated_us ? "Карта застаріла · перевірте зв’язок" : "Очікування актуальної карти") :
            data.alert_details_state == DASH_DATA_FRESH ? "Значки — загрози для територій" : "Карта актуальна · типи загроз недоступні");
        update_full_map();
        return;
    }
    if (active != home_screen && active != schedule_screen) return;
    update_energy_cards(&data);
    if (active != home_screen) {
        return;
    }
    update_clock();
    update_weather_card(&data);
    update_alert_card(&data);
    map_stale = data.alerts_state != DASH_DATA_FRESH;
    set_label_if_changed(map_state_note, data.alerts_state == DASH_DATA_FRESH ? "Карта актуальна" :
                         data.alerts_state == DASH_DATA_NO_TOKEN ? "Потрібен токен" :
                         data.alerts_updated_us ? "Карта застаріла" : "Статус невідомий");
    lv_obj_set_style_text_color(map_state_note, lv_color_hex(palette->colors[
        map_stale ? DASH_COLOR_WARNING : DASH_COLOR_MUTED]), 0);
    if (map_canvas) ESP_ERROR_CHECK_WITHOUT_ABORT(paint_map(data.region_alerts));
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

    dashboard_data_snapshot_t initial;
    dashboard_data_snapshot(&initial);
    selected_city=initial.location ? initial.location : dashboard_city_default();
    location_screen=create_screen();
    city_screen=create_screen();
    energy_settings_screen=create_screen();
    home_screen = create_screen();
    settings_screen = create_screen();
    system_screen = create_screen();
    reboot_screen = create_screen();
    schedule_screen = create_screen();
    wifi_screen = create_screen();
    display_settings_screen = create_screen();
    map_screen = create_screen();
    theme_screen = create_screen();

    home.header = surface(home_screen, MARGIN, 10, CARD_WIDTH, 48, DASH_COLOR_PANEL, 12);
    home.city = label(home_screen, selected_city->name, title_font, DASH_COLOR_TEXT, 16, 8);
    home_clock = label(home_screen, "--:--", title_font, DASH_COLOR_CLOCK, 0, 0);

    home.settings_key = button(home_screen, LV_SYMBOL_SETTINGS, 420, 10, 44, show_screen, settings_screen);
    lv_obj_set_height(home.settings_key, 44);
    lv_obj_t *gear = lv_obj_get_child(home.settings_key, 0);
    lv_obj_set_style_text_font(gear, LV_FONT_DEFAULT, 0);
    lv_obj_set_style_transform_pivot_x(gear, 7, 0);
    lv_obj_set_style_transform_pivot_y(gear, 7, 0);
    lv_obj_set_style_transform_scale(gear, 384, 0);
    weather_value = label(home_screen, "— °C", title_font, DASH_COLOR_TEMPERATURE, 278, 8);
    weather_note = label(home_screen, "", small_font, DASH_COLOR_MUTED, 278, 40);
    lv_obj_set_width(weather_value, 136);
    lv_label_set_long_mode(weather_value, LV_LABEL_LONG_DOT);
    lv_obj_set_width(weather_note, 136);
    lv_label_set_long_mode(weather_note, LV_LABEL_LONG_DOT);

    lv_obj_t *map = surface(home_screen, 16, 64, CARD_WIDTH, 330, DASH_COLOR_PANEL, 12);
    home.map_card = map;
    lv_obj_t *local_alert = surface(map, 0, 0, CARD_WIDTH, 76, DASH_COLOR_PANEL, 0);
    lv_obj_set_style_border_width(local_alert, 0, 0);
    lv_obj_set_style_bg_opa(local_alert, LV_OPA_TRANSP, 0);
    home.alert_card = local_alert;
    lv_obj_set_clickable(local_alert, true);
    lv_obj_add_event_cb(local_alert, open_map, LV_EVENT_CLICKED, NULL);
    home.alert_heading = label(local_alert, dashboard_region_find(selected_city->region)->name, small_font, DASH_COLOR_MUTED, 12, 4);
    alert_value = label(local_alert, "Статус невідомий", body_font, DASH_COLOR_MUTED, 12, 23);
    alert_note = label(local_alert, "", small_font, DASH_COLOR_MUTED, 12, 49);
    alert_threat = label(local_alert, "Торкніться карти ›", small_font, DASH_COLOR_MUTED, 12, 67);
    lv_obj_set_width(alert_value, 420);
    lv_label_set_long_mode(alert_value, LV_LABEL_LONG_DOT);
    lv_obj_set_width(alert_note, 178);
    lv_label_set_long_mode(alert_note, LV_LABEL_LONG_DOT);
    lv_obj_set_width(alert_threat, 217);
    lv_label_set_long_mode(alert_threat, LV_LABEL_LONG_DOT);

    map_state_note = label(map, "Очікування", small_font, DASH_COLOR_MUTED, 286, 4);
    lv_obj_set_width(map_state_note, 146);
    lv_label_set_long_mode(map_state_note, LV_LABEL_LONG_DOT);
    surface(map, 12, 310, 9, 9, DASH_COLOR_MAP_ALARM, 3);
    label(map, "Тривога", small_font, DASH_COLOR_TEXT, 26, 306);
    surface(map, 111, 310, 9, 9, DASH_COLOR_MAP_WARNING, 3);
    label(map, "Загроза", small_font, DASH_COLOR_TEXT, 125, 306);
    surface(map, 211, 310, 9, 9, DASH_COLOR_MAP_WARNING, 0);
    surface(map, 213, 310, 2, 9, DASH_COLOR_MAP_CLEAR, 0);
    surface(map, 217, 310, 2, 9, DASH_COLOR_MAP_CLEAR, 0);
    label(map, "Часткова", small_font, DASH_COLOR_TEXT, 225, 306);
    const esp_err_t result = add_map(map);
    if (result != ESP_OK) {
        ESP_LOGE("dashboard_screen", "Could not initialize the Ukraine map");
        label(map, "Карта недоступна", body_font, DASH_COLOR_MUTED, 100, 170);
    }
    label(map, "© OSM", small_font, DASH_COLOR_MUTED, 374, 306);

    lv_obj_t *power = surface(home_screen, 16, 400, CARD_WIDTH, 68, DASH_COLOR_PANEL, 12);
    lv_obj_set_clickable(power, true);
    lv_obj_add_event_cb(power, show_screen, LV_EVENT_CLICKED, schedule_screen);
    home.power_card = power;
    home.power_heading = label(power, "ЦЕК 5.2", body_font, DASH_COLOR_TEXT, 12, 4);
    lv_obj_set_width(home.power_heading,208);
    lv_obj_set_height(home.power_heading,lv_font_get_line_height(body_font));
    lv_label_set_long_mode(home.power_heading,LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    energy_home_note = label(power, "Оновлення…", small_font, DASH_COLOR_WARNING, 230, 6);
    energy_home_track = make_outage_track(power, 12, 29, CARD_WIDTH - 24);

    add_schedule_screen();
    add_map_screen();
    add_wifi_screen();
    add_location_screens();
    add_energy_settings_screen();
    add_settings_screen();
    add_system_screen();
    add_display_settings_screen();
    add_theme_screen();
    apply_home_layout();
    lv_obj_add_event_cb(home_screen, draw_home_instruments, LV_EVENT_DRAW_POST, NULL);
    read_touch = lv_indev_get_read_cb(touch);
    lv_indev_set_read_cb(touch, read_display_touch);
    lv_screen_load(home_screen);
    update_wifi(NULL);
    update_remote_data(NULL);
    lv_timer_create(update_wifi, 250, NULL);
    lv_timer_create(update_wifi_connection, 2000, NULL);
    lv_timer_create(update_remote_data, 1000, NULL);
    lv_timer_create(update_system, 1000, NULL);
    lv_timer_create(update_energy_settings,500,NULL);
    return ESP_OK;
}
