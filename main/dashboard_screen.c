#include "dashboard_screen.h"

#include <stdbool.h>
#include <time.h>

#include "assets/ukraine_map_data.h"
#include "dashboard_data.h"
#include "dashboard_wifi.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

enum {
    SCREEN_WIDTH = 480,
    MARGIN = 16,
    CARD_WIDTH = SCREEN_WIDTH - MARGIN * 2,
    MINUTES_PER_DAY = 24 * 60,
};

static const uint32_t background = 0x101820;
static const uint32_t panel = 0x1B2632;
static const uint32_t text_color = 0xF4F7FA;
static const uint32_t muted = 0xAFBECC;
static const uint32_t warm = 0xF8C576;
static const uint32_t alarm = 0xE87A74;
static const uint32_t blue = 0xA8CFEC;

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
    lv_obj_set_style_bg_color(object, lv_color_hex(color), 0);
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
    lv_obj_set_style_bg_color(screen, lv_color_hex(background), 0);
    lv_obj_set_scrollable(screen, false);
    return screen;
}

static esp_err_t paint_map(const char regions[DASHBOARD_REGION_COUNT])
{
    uint16_t region_color[UA_MAP_BORDER + 1] = {0};
    region_color[0] = lv_color_to_u16(lv_color_hex(panel));
    region_color[UA_MAP_BORDER] = lv_color_to_u16(lv_color_hex(0x263848));
    for (unsigned index = 1; index < DASHBOARD_REGION_COUNT; ++index) {
        uint32_t color = 0x436078;
        if (regions) {
            if (regions[index] == 'R') {
                color = 0xBE504C;
            } else if (regions[index] == 'Y' || regions[index] == 'P') {
                color = 0xBE944A;
            }
        }
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
                row[x] = region_color[region];
            }
        }
    }
    if (run != sizeof(ua_map_runs)) {
        return ESP_ERR_INVALID_SIZE;
    }
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
    surface(card, 12 + UA_MAP_DNIPRO_X - 4, 30 + UA_MAP_DNIPRO_Y - 4,
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

static void update_age(lv_obj_t *object, int64_t updated_us, bool stale)
{
    const long long minutes = (esp_timer_get_time() - updated_us) / 60000000;
    if (minutes == 0) {
        lv_label_set_text(object, stale ? "Щойно · застар." : "Щойно оновлено");
    } else {
        lv_label_set_text_fmt(object, "%lld хв%s", minutes, stale ? " · застар." : " тому");
    }
}

static void update_weather_card(const dashboard_data_snapshot_t *data)
{
    if (data->weather_updated_us) {
        const int value = data->temperature_tenths;
        const int magnitude = value < 0 ? -value : value;
        lv_label_set_text_fmt(weather_value, "%s%d,%d °C", value < 0 ? "−" : "",
                              magnitude / 10, magnitude % 10);
        update_age(weather_note, data->weather_updated_us, data->weather_state == DASH_DATA_STALE);
        return;
    }

    lv_label_set_text(weather_value, "— °C");
    const char *message = "Оновлення…";
    if (dashboard_wifi_state() != DASH_WIFI_CONNECTED) {
        message = "Очікує Wi-Fi";
    } else if (data->weather_state == DASH_DATA_UNAVAILABLE) {
        message = "Немає даних";
    }
    lv_label_set_text(weather_note, message);
}

static void update_alert_card(const dashboard_data_snapshot_t *data)
{
    const char *status = "Статус невідомий";
    const char *note = "Очікування даних";
    uint32_t color = muted;
    if (data->alerts_state == DASH_DATA_FRESH) {
        switch (data->region_alerts[12]) {
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

    lv_label_set_text(alert_value, status);
    lv_obj_set_style_text_color(alert_value, lv_color_hex(color), 0);
    if (data->alerts_state == DASH_DATA_FRESH || data->alerts_state == DASH_DATA_STALE) {
        update_age(alert_note, data->alerts_updated_us, data->alerts_state == DASH_DATA_STALE);
    } else {
        lv_label_set_text(alert_note, note);
    }
}

static void update_remote_data(lv_timer_t *timer)
{
    lv_obj_t *active = lv_screen_active();
    if (active != home_screen && active != schedule_screen) {
        return;
    }
    dashboard_data_snapshot_t data;
    dashboard_data_snapshot(&data);
    update_energy_cards(&data);
    if (active != home_screen) {
        return;
    }
    update_clock();
    static int64_t painted_at;
    update_weather_card(&data);
    update_alert_card(&data);
    if (data.alerts_updated_us != painted_at && paint_map(data.region_alerts) == ESP_OK) {
        painted_at = data.alerts_updated_us;
    }
}

esp_err_t dashboard_screen_create(void)
{
    const size_t font_size = (size_t)(ubuntu_medium_end - ubuntu_medium_start);
    small_font = lv_tiny_ttf_create_data_ex(ubuntu_medium_start, font_size, 14, LV_FONT_KERNING_NONE, 48);
    body_font = lv_tiny_ttf_create_data_ex(ubuntu_medium_start, font_size, 17, LV_FONT_KERNING_NONE, 48);
    title_font = lv_tiny_ttf_create_data_ex(ubuntu_medium_start, font_size, 27, LV_FONT_KERNING_NONE, 48);
    if (!small_font || !body_font || !title_font) {
        return ESP_ERR_NO_MEM;
    }

    home_screen = create_screen();
    schedule_screen = create_screen();
    wifi_screen = create_screen();

    label(home_screen, "Дніпро", title_font, text_color, 16, 8);
    home_clock = label(home_screen, "--:--", title_font, text_color, 0, 0);
    lv_obj_align(home_clock, LV_ALIGN_TOP_MID, 0, 10);

    lv_obj_t *home_wifi_button = button(home_screen, "Wi-Fi", 334, 10, 130, show_screen, wifi_screen);
    lv_obj_set_style_pad_all(home_wifi_button, 0, 0);
    home_wifi_status = lv_obj_get_child(home_wifi_button, 0);
    lv_obj_set_style_text_font(home_wifi_status, small_font, 0);
    lv_obj_set_style_text_align(home_wifi_status, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *weather = surface(home_screen, 16, 64, 140, 68, panel, 12);
    label(weather, "На вулиці", small_font, muted, 12, 3);
    weather_value = label(weather, "— °C", title_font, blue, 12, 18);
    weather_note = label(weather, "", small_font, muted, 12, 49);

    lv_obj_t *local_alert = surface(home_screen, 164, 64, 300, 68, panel, 12);
    label(local_alert, "Дніпропетровська область", small_font, muted, 12, 4);
    alert_value = label(local_alert, "Статус невідомий", body_font, muted, 12, 23);
    alert_note = label(local_alert, "", small_font, muted, 12, 49);

    lv_obj_t *map = surface(home_screen, 16, 140, CARD_WIDTH, 254, panel, 12);
    label(map, "Тривоги по областях", body_font, text_color, 12, 5);
    label(map, "© OSM", small_font, muted, 188, 8);
    surface(map, 250, 11, 9, 9, 0xBE504C, 3);
    label(map, "Тривога", small_font, text_color, 264, 7);
    surface(map, 329, 11, 9, 9, 0xBE944A, 3);
    label(map, "Частк. / загроза", small_font, text_color, 343, 7);
    const esp_err_t result = add_map(map);
    if (result != ESP_OK) {
        ESP_LOGE("dashboard_screen", "Could not initialize the Ukraine map");
        return result;
    }

    lv_obj_t *power = surface(home_screen, 16, 400, CARD_WIDTH, 68, panel, 12);
    lv_obj_set_clickable(power, true);
    lv_obj_add_event_cb(power, show_screen, LV_EVENT_CLICKED, schedule_screen);
    label(power, "ЦЕК 5.2", body_font, text_color, 12, 4);
    energy_home_note = label(power, "Оновлення…", small_font, warm, 230, 6);
    energy_home_track = make_outage_track(power, 12, 29, CARD_WIDTH - 24);

    add_schedule_screen();
    add_wifi_screen();
    lv_screen_load(home_screen);
    update_wifi(NULL);
    update_remote_data(NULL);
    lv_timer_create(update_wifi, 250, NULL);
    lv_timer_create(update_remote_data, 1000, NULL);
    return ESP_OK;
}
