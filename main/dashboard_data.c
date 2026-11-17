#include "dashboard_data.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <time.h>

#include "dashboard_data_parse.h"
#include "dashboard_wifi.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

#define SECONDS_US(value) (INT64_C(1000000) * (value))

enum { TOKEN_CAPACITY = 257, ENERGY_BODY_CAPACITY = 192 * 1024 };
typedef enum { SOURCE_WEATHER, SOURCE_ALERTS, SOURCE_ENERGY } data_source_t;

static const char *const tag = "dashboard_data";
static const char weather_url[] =
    "https://api.open-meteo.com/v1/forecast?latitude=48.4647&longitude=35.0462"
    "&current=temperature_2m&temperature_unit=celsius";
static const char alerts_url[] =
    "https://api.alerts.in.ua/v2/iot/active_air_raid_alerts_by_oblast.json";
static const char energy_url[] =
    "https://alerts.energy/api/v1/source-registry/areas/dnipropetrovska-oblast/shutdowns";

static portMUX_TYPE data_lock = portMUX_INITIALIZER_UNLOCKED;
static dashboard_data_snapshot_t latest = {
    .weather_state = DASH_DATA_WAITING,
    .alerts_state = DASH_DATA_NO_TOKEN,
    .energy_state = DASH_DATA_NO_TOKEN,
};

typedef struct {
    char *text;
    size_t capacity;
    size_t length;
    long retry_after;
    bool overflow;
} http_body_t;

static esp_err_t collect_response(esp_http_client_event_t *event)
{
    http_body_t *body = event->user_data;
    if (event->event_id == HTTP_EVENT_ON_HEADER && event->header_key && event->header_value &&
        strcasecmp(event->header_key, "Retry-After") == 0) {
        char *end = NULL;
        const long seconds = strtol(event->header_value, &end, 10);
        if (end && !*end && seconds > 0) {
            body->retry_after = seconds;
        }
    }
    if (event->event_id == HTTP_EVENT_ON_DATA && event->data_len > 0) {
        const size_t size = (size_t)event->data_len;
        if (size >= body->capacity - body->length) {
            body->overflow = true;
        } else if (!body->overflow) {
            memcpy(body->text + body->length, event->data, size);
            body->length += size;
        }
    }
    return ESP_OK;
}

static int fetch_json(data_source_t source, const char *authorization, http_body_t *body)
{
    body->length = 0;
    body->overflow = false;
    body->retry_after = 0;
    if (source != SOURCE_WEATHER && !authorization) {
        return 0;
    }

    static const char *const urls[] = { weather_url, alerts_url, energy_url };
    const esp_http_client_config_t config = {
        .url = urls[source],
        .timeout_ms = source == SOURCE_ENERGY ? 6500 : 4500,
        .disable_auto_redirect = true,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = collect_response,
        .user_data = body,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        return 0;
    }

    esp_err_t result = esp_http_client_set_header(client, "Accept", "application/json");
    if (result == ESP_OK && source != SOURCE_WEATHER) {
        result = esp_http_client_set_header(client, "Authorization", authorization);
    }
    if (result == ESP_OK) {
        result = esp_http_client_perform(client);
    }
    const int status = result == ESP_OK && !body->overflow
                       ? esp_http_client_get_status_code(client) : 0;
    esp_http_client_cleanup(client);
    body->text[body->length] = '\0';
    return status;
}

/* Each authorized service key has a separate partition and a fixed HTTPS destination. */
static bool read_bearer(const char *partition, const char *key,
                        char header[sizeof("Bearer ") + TOKEN_CAPACITY])
{
    const esp_err_t init = nvs_flash_init_partition(partition);
    if (init != ESP_OK) {
        ESP_LOGW(tag, "Service key partition unavailable: %s", esp_err_to_name(init));
        return false;
    }

    nvs_handle_t keys;
    if (nvs_open_from_partition(partition, "credentials", NVS_READONLY, &keys) != ESP_OK) {
        return false;
    }
    char token[TOKEN_CAPACITY];
    size_t size = sizeof(token);
    const esp_err_t result = nvs_get_str(keys, key, token, &size);
    nvs_close(keys);
    if (result != ESP_OK || size < 2) {
        return false;
    }
    for (size_t index = 0; index < size - 1; ++index) {
        if ((unsigned char)token[index] < 0x21 || (unsigned char)token[index] > 0x7e) {
            return false;
        }
    }
    snprintf(header, sizeof("Bearer ") + TOKEN_CAPACITY, "Bearer %s", token);
    return true;
}

static int64_t update_weather(http_body_t *body)
{
    const int status = fetch_json(SOURCE_WEATHER, NULL, body);
    int temperature = 0;
    const bool success = status == 200 &&
                         dashboard_parse_weather(body->text, body->length, &temperature);
    const int64_t now = esp_timer_get_time();

    portENTER_CRITICAL(&data_lock);
    latest.weather_state = success ? DASH_DATA_FRESH : DASH_DATA_UNAVAILABLE;
    if (success) {
        latest.temperature_tenths = temperature;
        latest.weather_updated_us = now;
    }
    portEXIT_CRITICAL(&data_lock);

    if (success) {
        ESP_LOGI(tag, "Open-Meteo temperature updated");
    } else {
        ESP_LOGW(tag, "Open-Meteo update failed (HTTP %d)", status);
    }
    return now + SECONDS_US(success ? 600 : 60);
}

static int64_t update_alerts(const char *authorization, http_body_t *body)
{
    const int status = fetch_json(SOURCE_ALERTS, authorization, body);
    char regions[DASHBOARD_REGION_COUNT];
    const bool success = status == 200 && dashboard_parse_alerts(body->text, body->length, regions);
    const int64_t now = esp_timer_get_time();

    portENTER_CRITICAL(&data_lock);
    latest.alerts_state = success ? DASH_DATA_FRESH
                                  : status == 401 ? DASH_DATA_AUTH_ERROR : DASH_DATA_UNAVAILABLE;
    if (success) {
        memcpy(latest.region_alerts, regions, sizeof(regions));
        latest.alerts_updated_us = now;
    }
    portEXIT_CRITICAL(&data_lock);

    if (success) {
        ESP_LOGI(tag, "Alerts.in.ua map updated");
        return now + SECONDS_US(25);
    }
    ESP_LOGW(tag, "Alerts.in.ua update failed (HTTP %d)", status);
    return now + SECONDS_US(status == 401 || status == 403 ? 300 : status == 429 ? 90 : 30);
}

static int64_t update_energy(const char *authorization, http_body_t *body)
{
    const int status = fetch_json(SOURCE_ENERGY, authorization, body);
    dashboard_energy_schedule_t next;
    const bool success = status == 200 && dashboard_parse_energy(body->text, body->length, &next);
    const int64_t now = esp_timer_get_time();

    portENTER_CRITICAL(&data_lock);
    latest.energy_state = success ? DASH_DATA_FRESH
                                  : status == 401 || status == 403 ? DASH_DATA_AUTH_ERROR
                                                                   : DASH_DATA_UNAVAILABLE;
    if (success) {
        latest.energy = next;
        latest.energy_updated_us = now;
    }
    portEXIT_CRITICAL(&data_lock);

    if (success) {
        ESP_LOGI(tag, "Alerts.energy schedule updated for CEK 5.2");
        return now + SECONDS_US(300);
    }
    ESP_LOGW(tag, "Alerts.energy update failed (HTTP %d)", status);
    if (status == 429) {
        return now + SECONDS_US(body->retry_after > 0 ? body->retry_after : 300);
    }
    return now + SECONDS_US(status == 401 || status == 403 ? 900 : 120);
}

static void data_task(void *argument)
{
    char authorization[sizeof("Bearer ") + TOKEN_CAPACITY];
    const bool has_token = read_bearer("dash_keys", "alerts_token", authorization);
    char energy_authorization[sizeof("Bearer ") + TOKEN_CAPACITY];
    const bool has_energy_key = read_bearer("energy_keys", "energy_key", energy_authorization);
    if (has_token) {
        portENTER_CRITICAL(&data_lock);
        latest.alerts_state = DASH_DATA_WAITING;
        portEXIT_CRITICAL(&data_lock);
    }

    char normal_storage[2048];
    http_body_t body = { .text = normal_storage, .capacity = sizeof(normal_storage) };
    http_body_t energy_body = {0};
    if (has_energy_key) {
        energy_body.text = heap_caps_malloc(ENERGY_BODY_CAPACITY, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        energy_body.capacity = ENERGY_BODY_CAPACITY;
        portENTER_CRITICAL(&data_lock);
        latest.energy_state = energy_body.text ? DASH_DATA_WAITING : DASH_DATA_UNAVAILABLE;
        portEXIT_CRITICAL(&data_lock);
    }

    esp_sntp_config_t sntp = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    sntp.start = false;
    const bool sntp_initialized = esp_netif_sntp_init(&sntp) == ESP_OK;
    bool sntp_started = false;
    bool online_last = false;
    int64_t next_weather = 0;
    int64_t next_alerts = 0;
    int64_t next_energy = 0;

    while (true) {
        const bool online = dashboard_wifi_state() == DASH_WIFI_CONNECTED;
        if (online) {
            if (!sntp_started && sntp_initialized) {
                sntp_started = esp_netif_sntp_start() == ESP_OK;
            }
            const int64_t now = esp_timer_get_time();
            if (!online_last && next_weather > now + SECONDS_US(60)) {
                next_weather = now;
            }

            /* The HTTPS certificate requires a synchronized wall clock. */
            if (time(NULL) >= 1700000000) {
                if (has_token && now >= next_alerts) {
                    next_alerts = update_alerts(authorization, &body);
                }
                if (now >= next_weather && dashboard_wifi_state() == DASH_WIFI_CONNECTED) {
                    next_weather = update_weather(&body);
                }
                if (energy_body.text && now >= next_energy &&
                    dashboard_wifi_state() == DASH_WIFI_CONNECTED) {
                    next_energy = update_energy(energy_authorization, &energy_body);
                }
            }
        }
        online_last = online;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void dashboard_data_snapshot(dashboard_data_snapshot_t *out)
{
    portENTER_CRITICAL(&data_lock);
    *out = latest;
    portEXIT_CRITICAL(&data_lock);

    const int64_t now = esp_timer_get_time();
    const bool online = dashboard_wifi_state() == DASH_WIFI_CONNECTED;
    if (out->weather_updated_us && (!online || out->weather_state != DASH_DATA_FRESH ||
                                    now - out->weather_updated_us > SECONDS_US(1200))) {
        out->weather_state = DASH_DATA_STALE;
    }
    if (out->alerts_updated_us && (!online || out->alerts_state != DASH_DATA_FRESH ||
                                   now - out->alerts_updated_us > SECONDS_US(90)) &&
        out->alerts_state != DASH_DATA_AUTH_ERROR) {
        out->alerts_state = DASH_DATA_STALE;
    }
    if (out->energy_updated_us && (!online || out->energy_state != DASH_DATA_FRESH ||
                                   now - out->energy_updated_us > SECONDS_US(900)) &&
        out->energy_state != DASH_DATA_AUTH_ERROR) {
        out->energy_state = DASH_DATA_STALE;
    }
}

esp_err_t dashboard_data_start(void)
{
    return xTaskCreate(data_task, "dashboard_data", 10240, NULL, 3, NULL) == pdPASS
           ? ESP_OK : ESP_ERR_NO_MEM;
}
