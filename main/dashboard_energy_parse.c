#include "dashboard_data_parse.h"
#include "dashboard_json.h"

#include <ctype.h>
#include <string.h>

#include "cJSON.h"

static bool is_text(const cJSON *node, const char *expected)
{
    return cJSON_IsString(node) && strcmp(node->valuestring, expected) == 0;
}

static bool valid_date(const char *date)
{
    if (strlen(date) != 10 || date[4] != '-' || date[7] != '-') {
        return false;
    }
    for (size_t index = 0; index < 10; ++index) {
        if (index != 4 && index != 7 && !isdigit((unsigned char)date[index])) {
            return false;
        }
    }
    const int month = (date[5] - '0') * 10 + date[6] - '0';
    const int day = (date[8] - '0') * 10 + date[9] - '0';
    return month >= 1 && month <= 12 && day >= 1 && day <= 31;
}

static bool apply_hours(const cJSON *hours, dashboard_energy_day_t *day, uint8_t category)
{
    if (!cJSON_IsArray(hours) || cJSON_GetArraySize(hours) != 24) {
        return false;
    }

    int position = 0;
    const cJSON *value;
    cJSON_ArrayForEach(value, hours) {
        if (!cJSON_IsNumber(value) || value->valuedouble < 0 || value->valuedouble > 3 ||
            value->valuedouble != value->valueint) {
            return false;
        }
        const int code = value->valueint;
        if (code == 1 || code == 2) {
            day->slots[position] = category;
        }
        if (code == 1 || code == 3) {
            day->slots[position + 1] = category;
        }
        position += 2;
    }
    return true;
}

static bool parse_day(const cJSON *item, const char *date_key, const char *state_key,
                      const char *hours_key, const char *possible_key, dashboard_energy_day_t *out)
{
    const cJSON *date = cJSON_GetObjectItemCaseSensitive(item, date_key);
    const cJSON *state = cJSON_GetObjectItemCaseSensitive(item, state_key);
    if (!cJSON_IsString(date) || !valid_date(date->valuestring)) {
        return false;
    }
    memcpy(out->date, date->valuestring, sizeof(out->date));

    if (is_text(state, "NoShutdowns")) {
        out->state = DASH_ENERGY_NO_SHUTDOWNS;
    } else if (is_text(state, "WaitingForSchedule")) {
        out->state = DASH_ENERGY_WAITING;
    } else if (is_text(state, "EmergencyShutdowns")) {
        out->state = DASH_ENERGY_EMERGENCY;
    } else if (is_text(state, "ScheduleApplies")) {
        out->state = DASH_ENERGY_PLANNED;
        const cJSON *possible = cJSON_GetObjectItemCaseSensitive(item, possible_key);
        if (possible && !cJSON_IsNull(possible) &&
            !(cJSON_IsArray(possible) && cJSON_GetArraySize(possible) == 0) &&
            !apply_hours(possible, out, DASH_SLOT_POSSIBLE)) {
            return false;
        }
        return apply_hours(cJSON_GetObjectItemCaseSensitive(item, hours_key), out, DASH_SLOT_OFF);
    } else {
        return false;
    }
    return true;
}

static const char *skip_space(const char *cursor, const char *end)
{
    while (cursor < end && isspace((unsigned char)*cursor)) {
        ++cursor;
    }
    return cursor;
}

bool dashboard_parse_energy_selected(const char *body, size_t length, const char *initiator,
                                    const char *queue, dashboard_energy_schedule_t *out)
{
    if(!body || !initiator || !queue || !out) return false;
    const char *const end = body + length;
    const char *cursor = skip_space(body, end);
    if (cursor == end || *cursor++ != '[') {
        return false;
    }

    /* Parse one row at a time so the regional response does not build a large JSON tree. */
    dashboard_energy_schedule_t parsed = {0};
    bool found = false;
    for (;;) {
        cursor = skip_space(cursor, end);
        if (cursor == end || *cursor != '{') {
            return false;
        }
        const char *next = dashboard_json_object_end(cursor, end);
        if (!next) {
            return false;
        }
        cJSON *item = cJSON_ParseWithLength(cursor, (size_t)(next - cursor));
        if (!cJSON_IsObject(item)) {
            cJSON_Delete(item);
            return false;
        }
        const bool match =
            is_text(cJSON_GetObjectItemCaseSensitive(item, "initiator"), initiator) &&
            is_text(cJSON_GetObjectItemCaseSensitive(item, "queue"), queue);
        if (match) {
            if (found ||
                !parse_day(item, "today_date", "today_status", "today", "today_possible", &parsed.today) ||
                !parse_day(item, "tomorrow_date", "tomorrow_status", "tomorrow", "tomorrow_possible",
                           &parsed.tomorrow)) {
                cJSON_Delete(item);
                return false;
            }
            found = true;
        }
        cJSON_Delete(item);

        cursor = skip_space(next, end);
        if (cursor == end) {
            return false;
        }
        const char separator = *cursor++;
        if (separator == ']') {
            if (!found || skip_space(cursor, end) != end) {
                return false;
            }
            *out = parsed;
            return true;
        }
        if (separator != ',') {
            return false;
        }
    }
}

bool dashboard_parse_energy(const char *body,size_t length,dashboard_energy_schedule_t *out)
{
    return dashboard_parse_energy_selected(body,length,"dnipropetrovska_oblast_cek","5.2",out);
}
