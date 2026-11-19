#include "dashboard_screen.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

#include "assets/ukraine_map_data.h"
#include "dashboard_data.h"
#include "dashboard_display_mode.h"
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

static const uint32_t background = 0x101820;
static const uint32_t panel = 0x1B2632;
static const uint32_t night_background = 0x070A0F;
static const uint32_t night_panel = 0x101820;
static const uint32_t text_color = 0xF4F7FA;
static const uint32_t muted = 0xAFBECC;
static const uint32_t warm = 0xF8C576;
static const uint32_t alarm = 0xE87A74;
static const uint32_t blue = 0xA8CFEC;
static const char display_settings_namespace[] = "dash_display";
_Static_assert(sizeof(display_settings_namespace) <= NVS_NS_NAME_MAX_SIZE,
               "Display settings namespace exceeds the NVS limit");

extern const uint8_t ubuntu_medium_start[] asm("_binary_Ubuntu_Medium_ttf_start");
extern const uint8_t ubuntu_medium_end[] asm("_binary_Ubuntu_Medium_ttf_end");

static lv_font_t *small_font;
static lv_font_t *body_font;
static lv_font_t *title_font;
static lv_obj_t *home_screen;
static lv_obj_t *home_clock;
static lv_obj_t *home_wifi_status;
static lv_obj_t *schedule_screen;
static lv_obj_t *wifi_screen;
static lv_obj_t *display_settings_screen;
static lv_obj_t *display_settings_note;
static lv_obj_t *wifi_status;
static lv_obj_t *wifi_instructions;
static lv_obj_t *wifi_change_button;
static lv_draw_buf_t map_buffer;
static lv_obj_t *map_canvas;
static lv_obj_t *weather_value;
static lv_obj_t *weather_note;
static lv_obj_t *alert_value;
static lv_obj_t *alert_note;
static lv_obj_t *alert_threat;
static lv_obj_t *alert_detail_screen;
static lv_obj_t *alert_detail_content;
static lv_obj_t *map_state_note;
static lv_obj_t *map_stale_note;
static bool map_stale;
static dashboard_alert_details_t screen_alert_details;
static lv_obj_t *alert_history_screen;
static lv_obj_t *alert_history_content;
static lv_obj_t *alert_history_note;
static dashboard_alert_history_t screen_alert_history;

static lv_obj_t *energy_home_track;
static lv_obj_t *energy_home_note;
static lv_obj_t *energy_detail_track;
static lv_obj_t *energy_detail_date;
static lv_obj_t *energy_detail_note;
static lv_obj_t *energy_detail_age;
static lv_obj_t *energy_intervals;
static bool energy_show_tomorrow;
static lv_style_t screen_style;
static lv_style_t panel_style;
static bool night_palette;
static dashboard_display_mode_t display_mode;
static nvs_handle_t display_settings;
static lv_indev_read_cb_t read_touch;
static esp_err_t (*apply_backlight)(int percent);

static lv_obj_t *label(lv_obj_t *parent, const char *text, const lv_font_t *font,
                       uint32_t color, int x, int y)
{
    lv_obj_t *object = lv_label_create(parent);
    lv_obj_set_style_text_font(object, font, 0);
    lv_obj_set_style_text_color(object, lv_color_hex(color), 0);
    lv_obj_set_pos(object, x, y);
    lv_label_set_text(object, text);
    return object;
}

static lv_obj_t *surface(lv_obj_t *parent, int x, int y, int width, int height,
                         uint32_t color, int radius)
{
    lv_obj_t *object = lv_obj_create(parent);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_size(object, width, height);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_radius(object, radius, 0);
    if (color == panel) {
        lv_obj_add_style(object, &panel_style, 0);
    } else {
        lv_obj_set_style_bg_color(object, lv_color_hex(color), 0);
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
    lv_obj_set_style_radius(object, 10, 0);
    lv_obj_set_style_bg_color(object, lv_color_hex(0x30465B), 0);
    lv_obj_t *caption = label(object, text, body_font, text_color, 0, 0);
    lv_obj_center(caption);
    lv_obj_add_event_cb(object, action, LV_EVENT_CLICKED, data);
    return object;
}

static lv_obj_t *create_screen(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_add_style(screen, &screen_style, 0);
    lv_obj_set_scrollable(screen, false);
    return screen;
}

static esp_err_t paint_map(const char regions[DASHBOARD_REGION_COUNT])
{
    static char painted_regions[DASHBOARD_REGION_COUNT];
    static bool painted;
    static bool painted_night;
    static bool painted_stale;
    if (painted && regions && painted_night == night_palette && painted_stale == map_stale &&
        memcmp(regions, painted_regions, sizeof(painted_regions)) == 0) {
        return ESP_OK;
    }

    uint16_t region_color[UA_MAP_BORDER + 1] = {0};
    region_color[0] = lv_color_to_u16(lv_color_hex(night_palette ? night_panel : panel));
    region_color[UA_MAP_BORDER] = lv_color_to_u16(lv_color_hex(night_palette ? 0x1C2936 : 0x263848));
    for (unsigned index = 1; index < DASHBOARD_REGION_COUNT; ++index) {
        uint32_t color = night_palette ? 0x304759 : 0x436078;
        if (regions) {
            if (regions[index] == 'R') {
                color = 0xBE504C;
            } else if (regions[index] == 'Y' || regions[index] == 'P') {
                color = 0xBE944A;
            }
        }
        if (map_stale) color = night_palette ? 0x263340 : 0x344454;
        region_color[index] = lv_color_to_u16(lv_color_hex(color));
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
                         ? lv_color_to_u16(lv_color_hex(night_palette ? 0x304759 : 0x436078))
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
    painted = true;
    painted_night = night_palette;
    painted_stale = map_stale;
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
    lv_obj_set_pos(map_canvas, 50, 49);
    lv_obj_set_style_transform_pivot_x(map_canvas, 0, 0);
    lv_obj_set_style_transform_pivot_y(map_canvas, 0, 0);
    lv_obj_set_style_transform_scale(map_canvas, 210, 0);
    surface(card, 50 + UA_MAP_DNIPRO_X * 210 / 256 - 4, 49 + UA_MAP_DNIPRO_Y * 210 / 256 - 4,
            9, 9, 0xF4D17E, 5);
    return ESP_OK;
}

static lv_obj_t *make_outage_track(lv_obj_t *parent, int x, int y, int width)
{
    lv_obj_t *track = surface(parent, x, y, width, 12, 0x3E556A, 4);
    static const char *const times[] = {"00", "06", "12", "18", "24"};
    for (int index = 0; index < 5; ++index) {
        lv_obj_t *tick = label(parent, times[index], small_font, muted, 0, y + 15);
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

static uint32_t outage_color(uint8_t kind)
{
    return kind == DASH_SLOT_OFF ? warm : 0xB92F83;
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
    if (night != night_palette) {
        night_palette = night;
        lv_style_set_bg_color(&screen_style, lv_color_hex(night ? night_background : background));
        lv_style_set_bg_color(&panel_style, lv_color_hex(night ? night_panel : panel));
        lv_obj_report_style_change(&screen_style);
        lv_obj_report_style_change(&panel_style);
        dashboard_data_snapshot_t data;
        dashboard_data_snapshot(&data);
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
    lv_obj_t *card = surface(display_settings_screen, MARGIN, y, CARD_WIDTH, 80, panel, 12);
    label(card, name, body_font, text_color, 14, 9);
    label(card, description, small_font, muted, 14, 44);
    lv_obj_t *control = lv_switch_create(card);
    lv_obj_set_size(control, 52, 28);
    lv_obj_set_pos(control, CARD_WIDTH - 67, 10);
    lv_obj_set_state(control, LV_STATE_CHECKED, (display_flags() & setting) != 0);
    lv_obj_add_event_cb(control, change_display_setting, LV_EVENT_VALUE_CHANGED, (void *)(uintptr_t)setting);
}

static void add_display_settings_screen(void)
{
    label(display_settings_screen, "Екран", title_font, text_color, 16, 15);
    button(display_settings_screen, "Назад", 370, 14, 94, show_screen, home_screen);
    add_display_switch(69, "Нічний режим", "22:00–07:00 за Києвом · яскравість 8%", SETTING_NIGHT);
    add_display_switch(159, "Економний режим", "Підсвітка гасне через 60 с без дотиків", SETTING_ECO);
    add_display_switch(249, "Пробудження від тривоги", "Коли в області починається нова тривога", SETTING_ALERT_WAKE);
    display_settings_note = label(display_settings_screen, "", body_font, alarm, 16, 351);
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
    lv_obj_set_style_bg_color(track, lv_color_hex(has_plan(day) ? 0x3E556A : 0x303843), 0);
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
        surface(track, now_minutes * (width - 2) / MINUTES_PER_DAY, 0, 2, 12, text_color, 0);
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
            label(energy_intervals, "Планових інтервалів немає", body_font, muted, 14, 6);
        }
    }
}

static void change_schedule_day(lv_event_t *event)
{
    energy_show_tomorrow = lv_event_get_user_data(event) != NULL;
}

static void add_schedule_screen(void)
{
    label(schedule_screen, "Відключення ЦЕК 5.2", title_font, text_color, 16, 15);
    energy_detail_date = label(schedule_screen, "", body_font, muted, 16, 53);
    button(schedule_screen, "Назад", 370, 14, 94, show_screen, home_screen);
    button(schedule_screen, "Сьогодні", 16, 81, 105, change_schedule_day, NULL);
    button(schedule_screen, "Завтра", 133, 81, 105, change_schedule_day, schedule_screen);
    energy_detail_note = label(schedule_screen, "Оновлення…", body_font, warm, 16, 130);

    lv_obj_t *timeline = surface(schedule_screen, 16, 160, CARD_WIDTH, 92, panel, 12);
    surface(timeline, 12, 14, 10, 10, warm, 3);
    label(timeline, "Заплановано", small_font, text_color, 29, 10);
    surface(timeline, 157, 14, 10, 10, 0xB92F83, 3);
    label(timeline, "Можливо", small_font, text_color, 174, 10);
    energy_detail_track = make_outage_track(timeline, 12, 44, CARD_WIDTH - 24);

    energy_intervals = surface(schedule_screen, 16, 262, CARD_WIDTH, 167, panel, 12);
    lv_obj_set_scrollable(energy_intervals, true);
    lv_obj_set_scroll_dir(energy_intervals, LV_DIR_VER);
    energy_detail_age = label(schedule_screen, "Alerts.energy", small_font, muted, 16, 446);
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
    label(wifi_screen, "Мережа Wi-Fi", title_font, text_color, 16, 15);
    label(wifi_screen, "Підключення до домашньої мережі", body_font, muted, 16, 53);
    button(wifi_screen, "Назад", 370, 14, 94, show_screen, home_screen);

    lv_obj_t *card = surface(wifi_screen, 16, 98, CARD_WIDTH, 226, panel, 12);
    wifi_status = label(card, "", body_font, text_color, 15, 14);
    lv_obj_set_width(wifi_status, CARD_WIDTH - 30);
    wifi_instructions = label(card, "", body_font, muted, 15, 59);
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
    uint32_t color = warm;
    switch (state) {
    case DASH_WIFI_CONNECTED:
        caption = "Підключено";
        color = 0xACEBC9;
        break;
    case DASH_WIFI_SETUP:
        caption = "Налаштування";
        break;
    case DASH_WIFI_SETUP_FAILED:
    case DASH_WIFI_ERROR:
        caption = "Помилка";
        color = alarm;
        break;
    case DASH_WIFI_OFFLINE:
        caption = "Немає зв’язку";
        color = alarm;
        break;
    default:
        break;
    }
    lv_label_set_text_fmt(home_wifi_status, "Wi-Fi\n%s", caption);
    lv_obj_set_style_text_color(home_wifi_status, lv_color_hex(color), 0);
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
    uint32_t color = muted;
    if (data->alerts_state == DASH_DATA_FRESH) {
        switch (data->region_alerts[DNIPRO_REGION]) {
        case 'R':
            status = "Повітряна тривога";
            color = alarm;
            break;
        case 'Y':
            status = "Попередня загроза";
            color = warm;
            break;
        case 'P':
            status = "Часткова тривога в області";
            color = warm;
            break;
        case 'N':
            status = "Тривог не повідомлено";
            break;
        }
    } else if (data->alerts_state == DASH_DATA_STALE) {
        status = "Дані тривог застаріли";
        color = warm;
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
    const lv_color_t status_color = lv_color_hex(color);
    if (!lv_color_eq(lv_obj_get_style_text_color(alert_value, LV_PART_MAIN), status_color)) {
        lv_obj_set_style_text_color(alert_value, status_color, 0);
    }
    if (data->alerts_state == DASH_DATA_FRESH || data->alerts_state == DASH_DATA_STALE) {
        update_age(alert_note, data->alerts_updated_us, data->alerts_state == DASH_DATA_STALE);
    } else {
        set_label_if_changed(alert_note, note);
    }
    set_label_if_changed(alert_threat, "Торкніться для деталей ›");
    if (data->alerts_state != DASH_DATA_FRESH || data->region_alerts[DNIPRO_REGION] == 'N') return;
    if (data->alert_details_state != DASH_DATA_FRESH) {
        set_label_if_changed(alert_threat, data->alert_details_updated_us ? "Деталі застаріли ›" : "Деталі недоступні ›");
        return;
    }
    const dashboard_alert_record_t *primary = dashboard_alert_primary(&screen_alert_details,
                                                                      data->region_alerts[DNIPRO_REGION]);
    if (primary) {
        char clock[6], text[96];
        const int64_t minutes = dashboard_alert_elapsed(primary->started_at, (int64_t)time(NULL));
        if (minutes >= 0 && dashboard_alert_kyiv_clock(primary->started_at, clock)) {
            snprintf(text, sizeof(text), "Від %s · %lld хв", clock, (long long)minutes);
            set_label_if_changed(alert_note, text);
        } else set_label_if_changed(alert_note, "Час початку невідомий");
        if (primary->threat_count) {
            snprintf(text, sizeof(text), "%s%s ›", dashboard_alert_threat_name(primary->threats[0]),
                     primary->threat_count > 1 || primary->threats_omitted ? " та інші" : "");
            set_label_if_changed(alert_threat, text);
        } else set_label_if_changed(alert_threat, "Деталі не надано ›");
    } else if (screen_alert_details.count) {
        set_label_if_changed(alert_note, screen_alert_details.count > 1 ? "Кілька локальних тривог" : "Окрема локальна тривога");
    }
}

static void append_detail(char *buffer, size_t capacity, const char *format, ...)
{
    const size_t used = strlen(buffer);
    if (used >= capacity - 1) return;
    va_list args;
    va_start(args, format);
    vsnprintf(buffer + used, capacity - used, format, args);
    va_end(args);
}

static void update_alert_details_screen(const dashboard_data_snapshot_t *data)
{
    static char text[8192];
    text[0] = 0;
    if (data->alerts_state != DASH_DATA_FRESH) {
        append_detail(text, sizeof(text), "Поточний стан невідомий.\nПеревірте зв’язок і актуальність карти.");
    } else if (data->region_alerts[DNIPRO_REGION] == 'N') {
        append_detail(text, sizeof(text), "Тривог не повідомлено.\nДніпропетровська область.");
    } else if (data->alert_details_state != DASH_DATA_FRESH) {
        append_detail(text, sizeof(text), "Статус карти актуальний.\n%s\nЧас початку й загрози не підтверджено.",
                      data->alert_details_updated_us ? "Деталі застаріли." : "Деталі тимчасово недоступні.");
    } else {
        if (!screen_alert_details.count) append_detail(text, sizeof(text), "Деталі не надано.\nСтатус карти актуальний.\n");
        for (unsigned i = 0; i < screen_alert_details.count; ++i) {
            const dashboard_alert_record_t *r = &screen_alert_details.records[i];
            append_detail(text, sizeof(text), "%s%s\n%s\n", i ? "\n\n" : "", r->location,
                          r->level == 'R' ? "Повітряна тривога" : r->level == 'Y' ? "Попередня загроза" : "Рівень не вказано");
            char clock[6];
            const int64_t minutes = dashboard_alert_elapsed(r->started_at, (int64_t)time(NULL));
            if (minutes >= 0 && dashboard_alert_kyiv_clock(r->started_at, clock))
                append_detail(text, sizeof(text), "Від %s за Києвом · %lld хв\n", clock, (long long)minutes);
            else append_detail(text, sizeof(text), "Час початку невідомий\n");
            if (!r->oblast) append_detail(text, sizeof(text), "Локальний запис: не підтверджує\nтривогу для всього міста Дніпро.\n");
            append_detail(text, sizeof(text), "\nЗагрози:\n");
            if (!r->threat_count) append_detail(text, sizeof(text), "Деталі не надано\n");
            for (unsigned t = 0; t < r->threat_count; ++t)
                append_detail(text, sizeof(text), "• %s\n", dashboard_alert_threat_name(r->threats[t]));
            if (r->threats_omitted) append_detail(text, sizeof(text), "Ще загроз: %u\n", r->threats_omitted);
            if (*r->message) append_detail(text, sizeof(text), "\nПовідомлення джерела:\n%s\n", r->message);
            if (r->text_truncated) append_detail(text, sizeof(text), "Довгий текст скорочено.\n");
        }
        if (screen_alert_details.omitted) append_detail(text, sizeof(text), "\nЩе локальних записів: %u\n", screen_alert_details.omitted);
    }
    if (data->alert_details_updated_us) append_detail(text, sizeof(text), "\n\nДеталі перевірено %lld хв тому",
        (long long)((esp_timer_get_time() - data->alert_details_updated_us) / 60000000));
    append_detail(text, sizeof(text), "\nДжерело: Alerts.in.ua");
    set_label_if_changed(alert_detail_content, text);
}

static void open_alert_history(lv_event_t *event)
{
    (void)event;
    dashboard_data_request_alert_history();
    lv_screen_load(alert_history_screen);
}

static void update_alert_history_screen(const dashboard_data_snapshot_t *data)
{
    const bool coherent = dashboard_data_alert_history(&screen_alert_history, data);
    const dashboard_data_state_t state = coherent ? data->alert_history_state : DASH_DATA_STALE;
    char note[128];
    if (state == DASH_DATA_WAITING) {
        snprintf(note, sizeof(note), "%s%s", data->alert_history_updated_us ? "Збережена копія · " : "",
                 dashboard_wifi_state() == DASH_WIFI_CONNECTED ? "Очікування оновлення…" : "Очікування Wi-Fi");
    } else if (state == DASH_DATA_NO_TOKEN) {
        snprintf(note, sizeof(note), "Потрібен токен Alerts.in.ua");
    } else if (state == DASH_DATA_AUTH_ERROR) {
        snprintf(note, sizeof(note), "Токен не прийнято%s", data->alert_history_updated_us ? " · збережена копія" : "");
    } else if (data->alert_history_updated_us) {
        snprintf(note, sizeof(note), "%s · перевірено %lld хв тому",
                 state == DASH_DATA_FRESH ? "Кеш" : "Застаріла копія",
                 (long long)((esp_timer_get_time() - data->alert_history_updated_us) / 60000000));
    } else {
        snprintf(note, sizeof(note), "Історія недоступна · спробуйте відкрити пізніше");
    }
    set_label_if_changed(alert_history_note, note);
    static char text[8192];
    text[0] = 0;
    if (data->alert_history_updated_us && coherent) {
        if (!screen_alert_history.count) append_detail(text, sizeof(text), "За доступний місячний період\nзаписів повітряних тривог не знайдено.");
        for (unsigned i = 0; i < screen_alert_history.count; ++i) {
            char row[512];
            dashboard_alert_history_format(&screen_alert_history.records[i], (int64_t)time(NULL), row, sizeof(row));
            append_detail(text, sizeof(text), "%s%s\n", i ? "\n\n" : "", row);
        }
        append_detail(text, sizeof(text), "\nОстанні записи за місячний період.\nЧас за Києвом · Alerts.in.ua\nІсторія не визначає поточний стан карти.");
    } else if (state == DASH_DATA_WAITING) {
        append_detail(text, sizeof(text), "Завантаження історії…");
    } else {
        append_detail(text, sizeof(text), "Немає перевіреної історії.\nПоверніться та відкрийте її пізніше.");
    }
    set_label_if_changed(alert_history_content, text);
}

static void alert_scroll_end(lv_event_t *event)
{
    lv_obj_t *object = lv_event_get_target_obj(event);
    ESP_LOGI("dashboard_scroll", "%s y=%ld", (const char *)lv_event_get_user_data(event),
             (long)lv_obj_get_scroll_y(object));
}

static void add_alert_history_screen(void)
{
    button(alert_history_screen, "‹ Назад", 16, 10, 108, show_screen, alert_detail_screen);
    label(alert_history_screen, "Історія тривог", title_font, text_color, 140, 14);
    alert_history_note = label(alert_history_screen, "", small_font, muted, 16, 59);
    lv_obj_set_width(alert_history_note, CARD_WIDTH);
    lv_label_set_long_mode(alert_history_note, LV_LABEL_LONG_WRAP);
    lv_obj_t *scroll = surface(alert_history_screen, 16, 103, CARD_WIDTH, 361, panel, 12);
    /* surface() disables hit testing; a scroll viewport must receive the drag. */
    lv_obj_set_clickable(scroll, true);
    lv_obj_set_scrollable(scroll, true);
    lv_obj_set_scroll_dir(scroll, LV_DIR_VER);
    lv_obj_add_event_cb(scroll, alert_scroll_end, LV_EVENT_SCROLL_END, "history");
    alert_history_content = label(scroll, "Очікування даних", body_font, text_color, 12, 12);
    lv_obj_set_width(alert_history_content, CARD_WIDTH - 32);
    lv_label_set_long_mode(alert_history_content, LV_LABEL_LONG_WRAP);
}

static void add_alert_details_screen(void)
{
    button(alert_detail_screen, "‹ Назад", 16, 10, 108, show_screen, home_screen);
    label(alert_detail_screen, "Деталі", title_font, text_color, 140, 14);
    button(alert_detail_screen, "Історія", 350, 10, 114, open_alert_history, NULL);
    lv_obj_t *scroll = surface(alert_detail_screen, 16, 64, CARD_WIDTH, 400, panel, 12);
    lv_obj_set_clickable(scroll, true);
    lv_obj_set_scrollable(scroll, true);
    lv_obj_set_scroll_dir(scroll, LV_DIR_VER);
    lv_obj_add_event_cb(scroll, alert_scroll_end, LV_EVENT_SCROLL_END, "details");
    alert_detail_content = label(scroll, "Очікування даних", body_font, text_color, 12, 12);
    lv_obj_set_width(alert_detail_content, CARD_WIDTH - 32);
    lv_label_set_long_mode(alert_detail_content, LV_LABEL_LONG_WRAP);
}

static void update_remote_data(lv_timer_t *timer)
{
    dashboard_data_snapshot_t data;
    dashboard_data_snapshot(&data);
    if (!dashboard_data_alert_details(&screen_alert_details, &data)) data.alert_details_state = DASH_DATA_STALE;
    const int64_t now_ms = esp_timer_get_time() / 1000;
    dashboard_display_idle(&display_mode, now_ms);
    if (dashboard_display_alert(&display_mode, data.alerts_state == DASH_DATA_FRESH,
                                data.region_alerts[DNIPRO_REGION], now_ms) &&
        !setup_in_progress(dashboard_wifi_state())) {
        lv_screen_load(home_screen);
    }
    refresh_display_mode();
    lv_obj_t *active = lv_screen_active();
    if (active == alert_history_screen) {
        update_alert_history_screen(&data);
        return;
    }
    if (active == alert_detail_screen) {
        update_alert_details_screen(&data);
        return;
    }
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
    map_stale = data.alerts_state != DASH_DATA_FRESH;
    set_label_if_changed(map_state_note, data.alerts_state == DASH_DATA_FRESH ? "Актуально" :
                         data.alerts_state == DASH_DATA_NO_TOKEN ? "Потрібен токен" : "Немає свіжих даних");
    set_label_if_changed(map_stale_note, data.alerts_updated_us ? "Застарілі дані" : "Статус невідомий");
    lv_obj_set_hidden(map_stale_note, !map_stale);
    ESP_ERROR_CHECK_WITHOUT_ABORT(paint_map(data.region_alerts));
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

    uint8_t flags = 0;
    esp_err_t settings_error = nvs_open(display_settings_namespace, NVS_READWRITE, &display_settings);
    if (settings_error == ESP_OK) {
        settings_error = nvs_get_u8(display_settings, "flags", &flags);
    }
    if (settings_error != ESP_OK && settings_error != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW("dashboard_screen", "Display settings read failed: %s", esp_err_to_name(settings_error));
    }
    use_display_flags(flags);
    display_mode.last_activity_ms = esp_timer_get_time() / 1000;
    apply_backlight = set_backlight;
    lv_style_init(&screen_style);
    lv_style_set_bg_color(&screen_style, lv_color_hex(background));
    lv_style_init(&panel_style);
    lv_style_set_bg_color(&panel_style, lv_color_hex(panel));

    home_screen = create_screen();
    schedule_screen = create_screen();
    wifi_screen = create_screen();
    display_settings_screen = create_screen();
    alert_detail_screen = create_screen();
    alert_history_screen = create_screen();

    label(home_screen, "Дніпро", title_font, text_color, 16, 8);
    home_clock = label(home_screen, "--:--", title_font, text_color, 0, 0);
    lv_obj_align(home_clock, LV_ALIGN_TOP_MID, -60, 10);

    button(home_screen, "Екран", 238, 10, 84, show_screen, display_settings_screen);
    lv_obj_t *home_wifi_button = button(home_screen, "Wi-Fi", 334, 10, 130, show_screen, wifi_screen);
    lv_obj_set_style_pad_all(home_wifi_button, 0, 0);
    home_wifi_status = lv_obj_get_child(home_wifi_button, 0);
    lv_obj_set_style_text_font(home_wifi_status, small_font, 0);
    lv_obj_set_style_text_align(home_wifi_status, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *weather = surface(home_screen, 16, 64, 140, 88, panel, 12);
    label(weather, "На вулиці", small_font, muted, 12, 3);
    weather_value = label(weather, "— °C", title_font, blue, 12, 18);
    weather_note = label(weather, "", small_font, muted, 12, 49);

    lv_obj_t *local_alert = surface(home_screen, 164, 64, 300, 88, panel, 12);
    lv_obj_set_clickable(local_alert, true);
    lv_obj_add_event_cb(local_alert, show_screen, LV_EVENT_CLICKED, alert_detail_screen);
    label(local_alert, "Дніпропетровська область", small_font, muted, 12, 4);
    alert_value = label(local_alert, "Статус невідомий", body_font, muted, 12, 23);
    alert_note = label(local_alert, "", small_font, muted, 12, 49);
    alert_threat = label(local_alert, "Торкніться для деталей ›", small_font, muted, 12, 67);
    lv_obj_set_width(alert_value, 276);
    lv_label_set_long_mode(alert_value, LV_LABEL_LONG_DOT);
    lv_obj_set_width(alert_note, 276);
    lv_label_set_long_mode(alert_note, LV_LABEL_LONG_DOT);
    lv_obj_set_width(alert_threat, 276);
    lv_label_set_long_mode(alert_threat, LV_LABEL_LONG_DOT);

    lv_obj_t *map = surface(home_screen, 16, 160, CARD_WIDTH, 234, panel, 12);
    label(map, "Тривоги по областях", body_font, text_color, 12, 5);
    map_state_note = label(map, "Очікування", small_font, muted, 276, 7);
    surface(map, 12, 33, 9, 9, 0xBE504C, 3);
    label(map, "Тривога", small_font, text_color, 26, 29);
    surface(map, 111, 33, 9, 9, 0xBE944A, 3);
    label(map, "Загроза", small_font, text_color, 125, 29);
    surface(map, 211, 33, 9, 9, 0xBE944A, 0);
    surface(map, 213, 33, 2, 9, 0x436078, 0);
    surface(map, 217, 33, 2, 9, 0x436078, 0);
    label(map, "Часткова", small_font, text_color, 225, 29);
    const esp_err_t result = add_map(map);
    if (result != ESP_OK) {
        ESP_LOGE("dashboard_screen", "Could not initialize the Ukraine map");
        return result;
    }
    label(map, "© OSM", small_font, muted, 12, 212);
    map_stale_note = label(map, "Статус невідомий", body_font, warm, 120, 122);
    lv_obj_set_style_bg_color(map_stale_note, lv_color_hex(panel), 0);
    lv_obj_set_style_bg_opa(map_stale_note, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(map_stale_note, 8, 0);

    lv_obj_t *power = surface(home_screen, 16, 400, CARD_WIDTH, 68, panel, 12);
    lv_obj_set_clickable(power, true);
    lv_obj_add_event_cb(power, show_screen, LV_EVENT_CLICKED, schedule_screen);
    label(power, "ЦЕК 5.2", body_font, text_color, 12, 4);
    energy_home_note = label(power, "Оновлення…", small_font, warm, 230, 6);
    energy_home_track = make_outage_track(power, 12, 29, CARD_WIDTH - 24);

    add_schedule_screen();
    add_alert_details_screen();
    add_alert_history_screen();
    add_wifi_screen();
    add_display_settings_screen();
    read_touch = lv_indev_get_read_cb(touch);
    lv_indev_set_read_cb(touch, read_display_touch);
    lv_screen_load(home_screen);
    update_wifi(NULL);
    update_remote_data(NULL);
    lv_timer_create(update_wifi, 250, NULL);
    lv_timer_create(update_remote_data, 1000, NULL);
    return ESP_OK;
}
