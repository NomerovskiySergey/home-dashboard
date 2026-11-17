#include "dashboard_data_parse.h"

#include <ctype.h>
#include <math.h>
#include <string.h>

#include "cJSON.h"

bool dashboard_parse_weather(const char *body, size_t length, int *temperature_tenths)
{
    const char *end = NULL;
    cJSON *root = cJSON_ParseWithLengthOpts(body, length + 1, &end, true);
    const cJSON *current = cJSON_GetObjectItemCaseSensitive(root, "current");
    const cJSON *units = cJSON_GetObjectItemCaseSensitive(root, "current_units");
    const cJSON *temperature = cJSON_GetObjectItemCaseSensitive(current, "temperature_2m");
    const cJSON *unit = cJSON_GetObjectItemCaseSensitive(units, "temperature_2m");

    const bool valid = root && end == body + length && cJSON_IsNumber(temperature) &&
                       temperature->valuedouble >= -100 && temperature->valuedouble <= 70 &&
                       cJSON_IsString(unit) && strcmp(unit->valuestring, "°C") == 0;
    if (valid) {
        *temperature_tenths = (int)lround(temperature->valuedouble * 10);
    }
    cJSON_Delete(root);
    return valid;
}

bool dashboard_parse_alerts(const char *body, size_t length, char out[DASHBOARD_REGION_COUNT])
{
    /* Alerts.in.ua oblast order, expressed as ISO 3166-2 numeric suffixes. */
    static const unsigned region_codes[] = {
        43, 7, 5, 12, 14, 18, 21, 23, 26, 30, 32, 35, 9, 46,
        48, 51, 53, 56, 40, 59, 61, 63, 65, 68, 71, 77, 74,
    };
    while (length && isspace((unsigned char)*body)) {
        ++body;
        --length;
    }
    while (length && isspace((unsigned char)body[length - 1])) {
        --length;
    }
    if (length != sizeof(region_codes) / sizeof(region_codes[0]) + 2 ||
        body[0] != '"' || body[length - 1] != '"') {
        return false;
    }

    char parsed[DASHBOARD_REGION_COUNT] = {0};
    for (size_t index = 0; index < sizeof(region_codes) / sizeof(region_codes[0]); ++index) {
        const char status = body[index + 1];
        if (!(status == 'R' || status == 'Y' || status == 'P' || status == 'N')) {
            return false;
        }
        parsed[region_codes[index]] = status;
    }
    memcpy(out, parsed, sizeof(parsed));
    return true;
}
