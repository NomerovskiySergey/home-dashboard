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
typedef enum { SOURCE_WEATHER, SOURCE_ALERTS, SOURCE_ENERGY, SOURCE_ALERT_DETAILS } data_source_t;

static const char *const tag = "dashboard_data";
static const char weather_url[] =
    "https://api.open-meteo.com/v1/forecast?latitude=48.4647&longitude=35.0462"
    "&current=temperature_2m&temperature_unit=celsius";
static const char alerts_url[] =
    "https://api.alerts.in.ua/v2/iot/active_air_raid_alerts_by_oblast.json";
static const char energy_url[] =
    "https://alerts.energy/api/v1/source-registry/areas/dnipropetrovska-oblast/shutdowns";
static const char alert_details_url[] = "https://api.alerts.in.ua/v1/alerts/active.json";
static dashboard_alert_details_t alert_details;
static dashboard_alert_cache_t map_cache, details_cache;
static dashboard_alert_rate_t alerts_rate;
static unsigned map_generation, details_generation;
static bool details_urgent = true;
static portMUX_TYPE data_lock = portMUX_INITIALIZER_UNLOCKED;
static dashboard_recovery_t recovery;
static bool data_started;
/* Worker-owned service backoff survives a recovery cycle. */
static int64_t retry_not_before[4];
static dashboard_data_snapshot_t latest = {
    .weather_state = DASH_DATA_WAITING,
    .alerts_state = DASH_DATA_NO_TOKEN,
    .energy_state = DASH_DATA_NO_TOKEN,
    .alert_details_state = DASH_DATA_NO_TOKEN,
};

typedef struct {
    char *text;
    size_t capacity;
    size_t length;
    size_t received_length;
    long retry_after;
    bool overflow;
    const char *if_modified;
    char modified[80];
    bool cancelled;
} http_body_t;

static bool recovery_pending(void)
{
    portENTER_CRITICAL(&data_lock);
    const bool pending = recovery.pending;
    portEXIT_CRITICAL(&data_lock);
    return pending;
}

static void data_heartbeat(void)
{
    const int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&data_lock);
    dashboard_recovery_heartbeat(&recovery, now);
    portEXIT_CRITICAL(&data_lock);
}

bool dashboard_data_restart(void)
{
    const int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&data_lock);
    const bool accepted = data_started && dashboard_recovery_request(&recovery, now, false);
    portEXIT_CRITICAL(&data_lock);
    return accepted;
}

void dashboard_data_recovery(dashboard_recovery_t *out)
{
    portENTER_CRITICAL(&data_lock);
    *out = recovery;
    portEXIT_CRITICAL(&data_lock);
}

static void data_supervisor(void *argument)
{
    while (true) {
        const int64_t now = esp_timer_get_time();
        portENTER_CRITICAL(&data_lock);
        const dashboard_recovery_state_t previous = recovery.state;
        dashboard_recovery_poll(&recovery, now);
        const dashboard_recovery_state_t state = recovery.state;
        portEXIT_CRITICAL(&data_lock);
        if (previous != state) {
            ESP_LOGW(tag, "Data recovery state %d -> %d", previous, state);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static esp_err_t collect_response(esp_http_client_event_t *event)
{
    http_body_t *body = event->user_data;
    if (event->event_id == HTTP_EVENT_ON_HEADER && event->header_key && event->header_value &&
        strcasecmp(event->header_key, "Last-Modified") == 0 &&
        strlen(event->header_value) < sizeof(body->modified)) {
        dashboard_alert_copy_text(body->modified, sizeof(body->modified), event->header_value);
    }
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
        body->received_length += size;
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
    body->received_length = 0;
    body->overflow = false;
    body->retry_after = 0;
    body->modified[0] = 0;
    body->cancelled = recovery_pending();
    if (body->cancelled) return 0;
    if (source != SOURCE_WEATHER && !authorization) {
        return 0;
    }

    static const char *const urls[] = { weather_url, alerts_url, energy_url, alert_details_url };
    const esp_http_client_config_t config = {
        .url = urls[source],
        .timeout_ms = source == SOURCE_ENERGY ? 6500 : 4500,
        .disable_auto_redirect = true,
        .is_async = true,
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
    if (result == ESP_OK && body->if_modified && *body->if_modified) {
        result = esp_http_client_set_header(client, "If-Modified-Since", body->if_modified);
    }
    /* Total deadline also bounds a peer that keeps sending tiny chunks.
       Only this worker touches the client, including its cleanup. */
    const int64_t deadline = esp_timer_get_time() + SECONDS_US(20);
    if (result == ESP_OK) do {
        if (recovery_pending()) {
            body->cancelled = true;
            result = ESP_FAIL;
            break;
        }
        if (esp_timer_get_time() >= deadline) {
            ESP_LOGW(tag, "Source %d exceeded total request deadline", source);
            result = ESP_ERR_TIMEOUT;
            break;
        }
        result = esp_http_client_perform(client);
        if (result == ESP_ERR_HTTP_EAGAIN) vTaskDelay(pdMS_TO_TICKS(20));
    } while (result == ESP_ERR_HTTP_EAGAIN);
    const int raw_status = esp_http_client_get_status_code(client);
    const int64_t now = esp_timer_get_time();
    if (source == SOURCE_ALERTS || source == SOURCE_ALERT_DETAILS)
        dashboard_alert_rate_pause(&alerts_rate, now / 1000000, raw_status, body->retry_after);
    const int64_t pause = raw_status == 401 || raw_status == 403 ? (source == SOURCE_ENERGY ? 900 : 300)
                  : raw_status == 429 ? (body->retry_after > 0 ? body->retry_after
                      : source == SOURCE_ENERGY ? 300 : 90) : 0;
    if (pause > 0 && now + SECONDS_US(pause) > retry_not_before[source])
        retry_not_before[source] = now + SECONDS_US(pause);
    /* A request arriving just as perform() completes still cancels publication. */
    body->cancelled = body->cancelled || recovery_pending();
    const int status = result == ESP_OK && !body->overflow
                       && !body->cancelled ? raw_status : 0;
    esp_http_client_cleanup(client);
    data_heartbeat();
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
    if (body->cancelled) return esp_timer_get_time();
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
    body->if_modified = map_cache.valid ? map_cache.modified : NULL;
    const int status = fetch_json(SOURCE_ALERTS, authorization, body);
    body->if_modified = NULL;
    if (body->cancelled) return esp_timer_get_time();
    char regions[DASHBOARD_REGION_COUNT];
    const bool parsed = status == 200 && dashboard_parse_alerts(body->text, body->length, regions);
    const int64_t now = esp_timer_get_time();
    const bool success = dashboard_alert_cache_update(&map_cache, status, parsed, body->modified, now / 1000000);
    dashboard_alert_rate_pause(&alerts_rate, now / 1000000, status, body->retry_after);

    portENTER_CRITICAL(&data_lock);
    latest.alerts_state = success ? DASH_DATA_FRESH
                                  : status == 401 ? DASH_DATA_AUTH_ERROR : DASH_DATA_UNAVAILABLE;
    if (parsed) {
        if (memcmp(latest.region_alerts, regions, sizeof(regions)) != 0) {
            ++map_generation;
            details_urgent = true;
            details_cache.valid = false;
            details_cache.modified[0] = 0;
            latest.alert_details_state = DASH_DATA_STALE;
        }
        memcpy(latest.region_alerts, regions, sizeof(regions));
    }
    if (success) {
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

static int64_t update_alert_details(const char *authorization, http_body_t *body)
{
    /* Static scratch and record-at-a-time parsing keep task stacks bounded. */
    static dashboard_alert_details_t parsed_details;
    body->if_modified = details_cache.valid ? details_cache.modified : NULL;
    const int status = fetch_json(SOURCE_ALERT_DETAILS, authorization, body);
    body->if_modified = NULL;
    if (body->cancelled) return esp_timer_get_time();
    const bool parsed = status == 200 && dashboard_alert_details_parse(body->text, body->length, &parsed_details);
    const int64_t now = esp_timer_get_time();
    const bool success = dashboard_alert_cache_update(&details_cache, status, parsed, body->modified, now / 1000000);
    dashboard_alert_rate_pause(&alerts_rate, now / 1000000, status, body->retry_after);
    portENTER_CRITICAL(&data_lock);
    latest.alert_details_state = success ? DASH_DATA_FRESH : status == 401 ? DASH_DATA_AUTH_ERROR : DASH_DATA_UNAVAILABLE;
    if (success) {
        if (parsed) alert_details = parsed_details;
        details_generation = map_generation;
        latest.alert_details_updated_us = now;
    }
    portEXIT_CRITICAL(&data_lock);
    ESP_LOGI(tag, "Alert details HTTP %d, valid=%d, bytes=%u", status, success, (unsigned)body->length);
    return now + SECONDS_US(success ? 60 : status == 401 || status == 403 ? 300 : status == 429 ? 90 : 60);
}

static int64_t update_energy(const char *authorization, http_body_t *body)
{
    const int status = fetch_json(SOURCE_ENERGY, authorization, body);
    if (body->cancelled) return esp_timer_get_time();
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

static void reset_http_bodies(http_body_t *energy, http_body_t *details,
                              bool has_energy_key, bool has_token)
{
    free(energy->text);
    free(details->text);
    *energy = (http_body_t){ .capacity = ENERGY_BODY_CAPACITY };
    *details = (http_body_t){ .capacity = DASH_ALERT_BODY_CAPACITY };
    if (has_energy_key)
        energy->text = heap_caps_malloc(energy->capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (has_token)
        details->text = heap_caps_malloc(details->capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    portENTER_CRITICAL(&data_lock);
    if (has_energy_key && !energy->text) latest.energy_state = DASH_DATA_UNAVAILABLE;
    if (has_token && !details->text) latest.alert_details_state = DASH_DATA_UNAVAILABLE;
    portEXIT_CRITICAL(&data_lock);
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
        latest.alert_details_state = DASH_DATA_WAITING;
        portEXIT_CRITICAL(&data_lock);
    }

    char normal_storage[2048];
    http_body_t body = { .text = normal_storage, .capacity = sizeof(normal_storage) };
    http_body_t energy_body = {0};
    http_body_t details_body = {0};
    if (has_energy_key) {
        portENTER_CRITICAL(&data_lock);
        latest.energy_state = DASH_DATA_WAITING;
        portEXIT_CRITICAL(&data_lock);
    }
    reset_http_bodies(&energy_body, &details_body, has_energy_key, has_token);

    esp_sntp_config_t sntp = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    sntp.start = false;
    const bool sntp_initialized = esp_netif_sntp_init(&sntp) == ESP_OK;
    bool sntp_started = false;
    bool online_last = false;
    int64_t next_weather = 0;
    int64_t next_alerts = 0;
    int64_t next_energy = 0;
    int64_t next_details = 0;

    while (true) {
        data_heartbeat();
        if (recovery_pending()) {
            const int64_t now = esp_timer_get_time();
            portENTER_CRITICAL(&data_lock);
            dashboard_recovery_begin(&recovery, now);
            if (latest.weather_state == DASH_DATA_FRESH) latest.weather_state = DASH_DATA_STALE;
            if (latest.alerts_state == DASH_DATA_FRESH) latest.alerts_state = DASH_DATA_STALE;
            if (latest.energy_state == DASH_DATA_FRESH) latest.energy_state = DASH_DATA_STALE;
            if (latest.alert_details_state == DASH_DATA_FRESH) latest.alert_details_state = DASH_DATA_STALE;
            portEXIT_CRITICAL(&data_lock);
            /* fetch_json has already cleaned up its client before control gets here.
               Caches, rate budget, Retry-After and SNTP are intentionally retained. */
            reset_http_bodies(&energy_body, &details_body, has_energy_key, has_token);
            body = (http_body_t){ .text = normal_storage, .capacity = sizeof(normal_storage) };
            next_weather = next_alerts = next_energy = next_details = 0;
            details_urgent = true;
            portENTER_CRITICAL(&data_lock);
            dashboard_recovery_finish(&recovery, esp_timer_get_time());
            portEXIT_CRITICAL(&data_lock);
            ESP_LOGI(tag, "Data polling restarted; cached data and service backoff retained");
        }
        const bool online = dashboard_wifi_state() == DASH_WIFI_CONNECTED;
        if (online) {
            if (!sntp_started && sntp_initialized) {
                sntp_started = esp_netif_sntp_start() == ESP_OK;
            }
            const int64_t now = esp_timer_get_time();
            if (!online_last && next_weather > now + SECONDS_US(60)) {
                next_weather = now;
            }
            if (!online_last) { next_alerts = now; next_details = now; }

            /* The HTTPS certificate requires a synchronized wall clock. */
            if (time(NULL) >= 1700000000) {
                if (!recovery_pending() && has_token && now >= next_alerts &&
                    esp_timer_get_time() >= retry_not_before[SOURCE_ALERTS] &&
                    dashboard_alert_rate_take(&alerts_rate, esp_timer_get_time() / 1000000)) {
                    next_alerts = update_alerts(authorization, &body);
                }
                if (!recovery_pending() && now >= next_weather &&
                    esp_timer_get_time() >= retry_not_before[SOURCE_WEATHER] &&
                    dashboard_wifi_state() == DASH_WIFI_CONNECTED) {
                    next_weather = update_weather(&body);
                }
                if (!recovery_pending() && energy_body.text && now >= next_energy &&
                    esp_timer_get_time() >= retry_not_before[SOURCE_ENERGY] &&
                    dashboard_wifi_state() == DASH_WIFI_CONNECTED) {
                    next_energy = update_energy(energy_authorization, &energy_body);
                }
                if (!recovery_pending() && details_body.text && (now >= next_details || details_urgent) &&
                    esp_timer_get_time() >= retry_not_before[SOURCE_ALERT_DETAILS] &&
                    dashboard_wifi_state() == DASH_WIFI_CONNECTED &&
                    dashboard_alert_rate_take(&alerts_rate, esp_timer_get_time() / 1000000)) {
                    details_urgent = false;
                    next_details = update_alert_details(authorization, &details_body);
                }
            }
        }
        online_last = online;
        data_heartbeat();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void dashboard_data_snapshot(dashboard_data_snapshot_t *out)
{
    portENTER_CRITICAL(&data_lock);
    *out = latest;
    if (recovery.pending) {
        if (out->weather_updated_us) out->weather_state = DASH_DATA_STALE;
        if (out->alerts_updated_us && out->alerts_state == DASH_DATA_FRESH) out->alerts_state = DASH_DATA_STALE;
        if (out->energy_updated_us && out->energy_state == DASH_DATA_FRESH) out->energy_state = DASH_DATA_STALE;
        if (out->alert_details_updated_us && out->alert_details_state == DASH_DATA_FRESH)
            out->alert_details_state = DASH_DATA_STALE;
    }
    if (out->alert_details_updated_us && details_generation != map_generation)
        out->alert_details_state = DASH_DATA_STALE;
    portEXIT_CRITICAL(&data_lock);

    const int64_t now = esp_timer_get_time();
    const bool online = dashboard_wifi_state() == DASH_WIFI_CONNECTED;
    if (out->alert_details_updated_us && (!online || out->alert_details_state != DASH_DATA_FRESH ||
        now - out->alert_details_updated_us > SECONDS_US(150)) && out->alert_details_state != DASH_DATA_AUTH_ERROR) {
        out->alert_details_state = DASH_DATA_STALE;
    }
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

bool dashboard_data_alert_details(dashboard_alert_details_t *out, const dashboard_data_snapshot_t *expected)
{
    portENTER_CRITICAL(&data_lock);
    const bool same_snapshot = latest.alert_details_updated_us == expected->alert_details_updated_us &&
                               memcmp(latest.region_alerts, expected->region_alerts,
                                      sizeof(latest.region_alerts)) == 0;
    *out = alert_details;
    portEXIT_CRITICAL(&data_lock);
    return same_snapshot;
}

esp_err_t dashboard_data_start(void)
{
    portENTER_CRITICAL(&data_lock);
    if (data_started) {
        portEXIT_CRITICAL(&data_lock);
        return ESP_ERR_INVALID_STATE;
    }
    data_started = true;
    dashboard_recovery_init(&recovery, esp_timer_get_time());
    portEXIT_CRITICAL(&data_lock);
    TaskHandle_t supervisor = NULL;
    if (xTaskCreate(data_supervisor, "data_supervisor", 3072, NULL, 3, &supervisor) == pdPASS) {
        if (xTaskCreate(data_task, "dashboard_data", 10240, NULL, 3, NULL) == pdPASS) return ESP_OK;
        vTaskDelete(supervisor);
    }
    portENTER_CRITICAL(&data_lock);
    data_started = false;
    recovery.state = DASH_RECOVERY_FAILED;
    portEXIT_CRITICAL(&data_lock);
    return ESP_ERR_NO_MEM;
}
