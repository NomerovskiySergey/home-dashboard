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
typedef enum { WORKER_WEATHER, WORKER_ALERTS, WORKER_ENERGY, WORKER_COUNT } worker_id_t;
typedef enum { START_NOT_STARTED, START_PENDING, START_READY, START_FAILED } startup_state_t;
typedef struct {
    worker_id_t id;
    const char *name;
    data_source_t primary;
    bool has_key;
    char authorization[sizeof("Bearer ") + TOKEN_CAPACITY];
} data_worker_t;
static data_worker_t workers[WORKER_COUNT] = {
    { .id = WORKER_WEATHER, .name = "weather_data", .primary = SOURCE_WEATHER, .has_key = true },
    { .id = WORKER_ALERTS, .name = "alerts_data", .primary = SOURCE_ALERTS },
    { .id = WORKER_ENERGY, .name = "energy_data", .primary = SOURCE_ENERGY },
};

static const char *const tag = "dashboard_data";
static const char alerts_url[] =
    "https://api.alerts.in.ua/v2/iot/active_air_raid_alerts_by_oblast.json";
static const char energy_url[] =
    "https://alerts.energy/api/v1/source-registry/areas/dnipropetrovska-oblast/shutdowns";
static const char alert_details_url[] = "https://api.alerts.in.ua/v1/alerts/active.json";
static dashboard_alert_details_t alert_details;
static dashboard_alert_cache_t map_cache, details_cache;
static dashboard_alert_rate_t alerts_rate;
static unsigned map_generation, details_generation;
static uint32_t details_location_generation;
static bool details_urgent = true;
static portMUX_TYPE data_lock = portMUX_INITIALIZER_UNLOCKED;
static dashboard_recovery_t recovery;
static dashboard_recovery_t worker_recovery[WORKER_COUNT];
static startup_state_t startup_state;
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
    const char *url;
    const char *if_modified;
    char modified[80];
    bool cancelled;
} http_body_t;

static worker_id_t source_worker(data_source_t source)
{
    return source == SOURCE_WEATHER ? WORKER_WEATHER
         : source == SOURCE_ENERGY ? WORKER_ENERGY : WORKER_ALERTS;
}

static bool recovery_pending(data_source_t source)
{
    portENTER_CRITICAL(&data_lock);
    const bool pending = worker_recovery[source_worker(source)].pending;
    portEXIT_CRITICAL(&data_lock);
    return pending;
}

static void data_heartbeat(worker_id_t worker)
{
    const int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&data_lock);
    dashboard_recovery_heartbeat(&worker_recovery[worker], now);
    portEXIT_CRITICAL(&data_lock);
}

/* All tasks wait here until creation of the entire group succeeds.
   Partial startup failure is cleaned up by the waiting tasks themselves. */
static bool await_startup(void)
{
    while (true) {
        portENTER_CRITICAL(&data_lock);
        const startup_state_t state = startup_state;
        portEXIT_CRITICAL(&data_lock);
        if (state == START_READY) return true;
        if (state == START_FAILED) {
            vTaskDelete(NULL);
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

bool dashboard_data_restart(void)
{
    const int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&data_lock);
    const bool accepted = data_started && startup_state == START_READY &&
        dashboard_recovery_group_request(worker_recovery, WORKER_COUNT, &recovery, now);
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
    if (!await_startup()) return;
    /* One SNTP instance for all services; workers never initialize it. */
    esp_sntp_config_t sntp = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    sntp.start = false;
    const bool sntp_initialized = esp_netif_sntp_init(&sntp) == ESP_OK;
    bool sntp_started = false;
    while (true) {
        if (!sntp_started && sntp_initialized && dashboard_wifi_state() == DASH_WIFI_CONNECTED)
            sntp_started = esp_netif_sntp_start() == ESP_OK;
        const int64_t now = esp_timer_get_time();
        for (int i = 0; i < WORKER_COUNT; ++i) {
            portENTER_CRITICAL(&data_lock);
            const dashboard_recovery_state_t previous = worker_recovery[i].state;
            dashboard_recovery_poll(&worker_recovery[i], now);
            const dashboard_recovery_state_t state = worker_recovery[i].state;
            portEXIT_CRITICAL(&data_lock);
            if (previous != state)
                ESP_LOGW(tag, "%s recovery state %d -> %d", workers[i].name, previous, state);
        }
        portENTER_CRITICAL(&data_lock);
        dashboard_recovery_group_update(worker_recovery, WORKER_COUNT, &recovery, now);
        portEXIT_CRITICAL(&data_lock);
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
    body->cancelled = recovery_pending(source);
    if (body->cancelled) return 0;
    if (source != SOURCE_WEATHER && !authorization) {
        return 0;
    }

    if (source == SOURCE_WEATHER && !body->url) return 0;
    static const char *const urls[] = { NULL, alerts_url, energy_url, alert_details_url };
    const esp_http_client_config_t config = {
        .url = body->url ? body->url : urls[source],
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
        if (recovery_pending(source)) {
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
    body->cancelled = body->cancelled || recovery_pending(source);
    const int status = result == ESP_OK && !body->overflow
                       && !body->cancelled ? raw_status : 0;
    esp_http_client_cleanup(client);
    data_heartbeat(source_worker(source));
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
    dashboard_data_snapshot_t selection;
    dashboard_data_snapshot(&selection);
    char url[192];
    if (!dashboard_city_weather_url(selection.location,url,sizeof(url)))
        return esp_timer_get_time()+SECONDS_US(60);
    body->url=url;
    const int status = fetch_json(SOURCE_WEATHER, NULL, body);
    body->url=NULL;
    if (body->cancelled) return esp_timer_get_time();
    int temperature = 0;
    const bool success = status == 200 &&
                         dashboard_parse_weather(body->text, body->length, &temperature);
    const int64_t now = esp_timer_get_time();

    if (!success && retry_not_before[SOURCE_WEATHER]<now+SECONDS_US(60))
        retry_not_before[SOURCE_WEATHER]=now+SECONDS_US(60);
    portENTER_CRITICAL(&data_lock);
    if (selection.location_generation != latest.location_generation) {
        portEXIT_CRITICAL(&data_lock);
        return now;
    }
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
            if (latest.alert_details_state == DASH_DATA_FRESH)
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
    dashboard_data_snapshot_t selection;
    dashboard_data_snapshot(&selection);
    const bool matching_cache=details_location_generation==selection.location_generation;
    body->if_modified = details_cache.valid && matching_cache ? details_cache.modified : NULL;
    if (!matching_cache) details_cache.valid=false;
    const int status = fetch_json(SOURCE_ALERT_DETAILS, authorization, body);
    body->if_modified = NULL;
    if (body->cancelled) return esp_timer_get_time();
    const bool parsed = status == 200 && dashboard_alert_details_parse_region(body->text, body->length, selection.location->region, &parsed_details);
    const int64_t now = esp_timer_get_time();
    const bool success = dashboard_alert_cache_update(&details_cache, status, parsed, body->modified, now / 1000000);
    dashboard_alert_rate_pause(&alerts_rate, now / 1000000, status, body->retry_after);
    if (!success && retry_not_before[SOURCE_ALERT_DETAILS]<now+SECONDS_US(60))
        retry_not_before[SOURCE_ALERT_DETAILS]=now+SECONDS_US(60);
    portENTER_CRITICAL(&data_lock);
    if (selection.location_generation != latest.location_generation) {
        portEXIT_CRITICAL(&data_lock);
        details_cache.valid=false;
        return now;
    }
    latest.alert_details_state = success ? DASH_DATA_FRESH : status == 401 ? DASH_DATA_AUTH_ERROR : DASH_DATA_UNAVAILABLE;
    if (success) {
        if (parsed) alert_details = parsed_details;
        details_generation = map_generation;
        details_location_generation = selection.location_generation;
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

static void reset_worker_bodies(const data_worker_t *worker, http_body_t *body,
                                 http_body_t *details, char *normal_storage, size_t normal_capacity)
{
    if (worker->id == WORKER_ENERGY) free(body->text);
    free(details->text);
    *body = (http_body_t){ .text = normal_storage, .capacity = normal_capacity };
    *details = (http_body_t){ .capacity = DASH_ALERT_BODY_CAPACITY };
    if (worker->id == WORKER_ENERGY) {
        body->capacity = ENERGY_BODY_CAPACITY;
        body->text = worker->has_key
            ? heap_caps_malloc(body->capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : NULL;
    }
    if (worker->id == WORKER_ALERTS && worker->has_key)
        details->text = heap_caps_malloc(details->capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    portENTER_CRITICAL(&data_lock);
    if (worker->id == WORKER_ENERGY && worker->has_key && !body->text)
        latest.energy_state = DASH_DATA_UNAVAILABLE;
    if (worker->id == WORKER_ALERTS && worker->has_key && !details->text)
        latest.alert_details_state = DASH_DATA_UNAVAILABLE;
    portEXIT_CRITICAL(&data_lock);
}

static void mark_worker_stale(worker_id_t worker)
{
    if (worker == WORKER_WEATHER && latest.weather_state == DASH_DATA_FRESH)
        latest.weather_state = DASH_DATA_STALE;
    if (worker == WORKER_ENERGY && latest.energy_state == DASH_DATA_FRESH)
        latest.energy_state = DASH_DATA_STALE;
    if (worker == WORKER_ALERTS) {
        if (latest.alerts_state == DASH_DATA_FRESH) latest.alerts_state = DASH_DATA_STALE;
        if (latest.alert_details_state == DASH_DATA_FRESH) latest.alert_details_state = DASH_DATA_STALE;
    }
}

static void data_task(void *argument)
{
    const data_worker_t *worker = argument;
    if (!await_startup()) return;
    char normal_storage[2048];
    http_body_t body = {0}, details_body = {0};
    reset_worker_bodies(worker, &body, &details_body, normal_storage, sizeof(normal_storage));
    int64_t next_primary = 0, next_details = 0;
    bool online_last = false;
    uint32_t location_seen=UINT32_MAX;
    ESP_LOGI(tag, "%s ready", worker->name);
    while (true) {
        data_heartbeat(worker->id);
        if (recovery_pending(worker->primary)) {
            const int64_t now = esp_timer_get_time();
            portENTER_CRITICAL(&data_lock);
            dashboard_recovery_begin(&worker_recovery[worker->id], now);
            mark_worker_stale(worker->id);
            dashboard_recovery_group_update(worker_recovery, WORKER_COUNT, &recovery, now);
            portEXIT_CRITICAL(&data_lock);
            /* This worker's fetch_json already cleaned its own client.
               Another service continues running throughout this cleanup. */
            reset_worker_bodies(worker, &body, &details_body, normal_storage, sizeof(normal_storage));
            next_primary = next_details = 0;
            if (worker->id == WORKER_ALERTS) details_urgent = true;
            portENTER_CRITICAL(&data_lock);
            const int64_t finished = esp_timer_get_time();
            dashboard_recovery_finish(&worker_recovery[worker->id], finished);
            dashboard_recovery_group_update(worker_recovery, WORKER_COUNT, &recovery, finished);
            portEXIT_CRITICAL(&data_lock);
            ESP_LOGI(tag, "%s polling restarted; cache and service backoff retained", worker->name);
        }
        portENTER_CRITICAL(&data_lock);
        const uint32_t location_now=latest.location_generation;
        portEXIT_CRITICAL(&data_lock);
        if (location_seen!=location_now) {
            location_seen=location_now;
            if(worker->id==WORKER_WEATHER) next_primary=0;
            if(worker->id==WORKER_ALERTS) {
                details_cache.valid=false;
                details_urgent=true;
            }
        }
        const bool online = dashboard_wifi_state() == DASH_WIFI_CONNECTED;
        const int64_t now = esp_timer_get_time();
        if (online && !online_last) {
            if (worker->id == WORKER_ALERTS) next_primary = next_details = now;
            if (worker->id == WORKER_WEATHER && next_primary > now + SECONDS_US(60))
                next_primary = now;
        }
        if (online && worker->has_key && time(NULL) >= 1700000000) {
            if (!recovery_pending(worker->primary) && body.text && now >= next_primary &&
                now >= retry_not_before[worker->primary]) {
                switch (worker->id) {
                    case WORKER_WEATHER:
                        next_primary = update_weather(&body);
                        break;
                    case WORKER_ENERGY:
                        next_primary = update_energy(worker->authorization, &body);
                        break;
                    case WORKER_ALERTS:
                        if (dashboard_alert_rate_take(&alerts_rate, now / 1000000))
                            next_primary = update_alerts(worker->authorization, &body);
                        break;
                    default: break;
                }
            }
            /* Map and active.json share this one owner, cache generation and rate budget. */
            if (worker->id == WORKER_ALERTS && details_body.text &&
                !recovery_pending(SOURCE_ALERT_DETAILS) && dashboard_wifi_state() == DASH_WIFI_CONNECTED &&
                (esp_timer_get_time() >= next_details || details_urgent) &&
                esp_timer_get_time() >= retry_not_before[SOURCE_ALERT_DETAILS] &&
                dashboard_alert_rate_take(&alerts_rate, esp_timer_get_time() / 1000000)) {
                details_urgent = false;
                next_details = update_alert_details(worker->authorization, &details_body);
            }
        }
        online_last = online;
        data_heartbeat(worker->id);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void dashboard_data_snapshot(dashboard_data_snapshot_t *out)
{
    portENTER_CRITICAL(&data_lock);
    *out = latest;
    if (worker_recovery[WORKER_WEATHER].pending && out->weather_state == DASH_DATA_FRESH)
        out->weather_state = DASH_DATA_STALE;
    if (worker_recovery[WORKER_ENERGY].pending && out->energy_state == DASH_DATA_FRESH)
        out->energy_state = DASH_DATA_STALE;
    if (worker_recovery[WORKER_ALERTS].pending) {
        if (out->alerts_state == DASH_DATA_FRESH) out->alerts_state = DASH_DATA_STALE;
        if (out->alert_details_state == DASH_DATA_FRESH) out->alert_details_state = DASH_DATA_STALE;
    }
    if (out->alert_details_updated_us && (details_generation != map_generation ||
        details_location_generation != out->location_generation))
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
    const bool same_snapshot = latest.location_generation == expected->location_generation &&
                               details_location_generation == expected->location_generation &&
                               latest.alert_details_updated_us == expected->alert_details_updated_us &&
                               memcmp(latest.region_alerts, expected->region_alerts,
                                      sizeof(latest.region_alerts)) == 0;
    *out = alert_details;
    portEXIT_CRITICAL(&data_lock);
    return same_snapshot;
}

esp_err_t dashboard_data_select_location(const dashboard_city_t *city)
{
    if (!city || dashboard_city_find(city->id)!=city) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&data_lock);
    const bool unchanged=latest.location==city;
    portEXIT_CRITICAL(&data_lock);
    if(unchanged) return ESP_OK;
    const esp_err_t saved=dashboard_location_save(city);
    if(saved!=ESP_OK) return saved;
    portENTER_CRITICAL(&data_lock);
    latest.location=city;
    ++latest.location_generation;
    latest.weather_updated_us=0;
    latest.temperature_tenths=0;
    latest.weather_state=DASH_DATA_WAITING;
    latest.alert_details_updated_us=0;
    if(workers[WORKER_ALERTS].has_key) latest.alert_details_state=DASH_DATA_WAITING;
    /* Endpoint caches and scheduler flags remain Alerts-worker owned. */
    portEXIT_CRITICAL(&data_lock);
    return ESP_OK;
}

esp_err_t dashboard_data_start(void)
{
    portENTER_CRITICAL(&data_lock);
    if (startup_state != START_NOT_STARTED) {
        portEXIT_CRITICAL(&data_lock);
        return ESP_ERR_INVALID_STATE;
    }
    startup_state = START_PENDING;
    dashboard_recovery_init(&recovery, esp_timer_get_time());
    for (int i = 0; i < WORKER_COUNT; ++i)
        dashboard_recovery_init(&worker_recovery[i], esp_timer_get_time());
    portEXIT_CRITICAL(&data_lock);

    latest.location=dashboard_location_load();
    /* Read each service key once, before releasing any task from the barrier. */
    workers[WORKER_ALERTS].has_key = read_bearer("dash_keys", "alerts_token",
                                                workers[WORKER_ALERTS].authorization);
    workers[WORKER_ENERGY].has_key = read_bearer("energy_keys", "energy_key",
                                                workers[WORKER_ENERGY].authorization);
    portENTER_CRITICAL(&data_lock);
    latest.alerts_state = latest.alert_details_state =
        workers[WORKER_ALERTS].has_key ? DASH_DATA_WAITING : DASH_DATA_NO_TOKEN;
    latest.energy_state = workers[WORKER_ENERGY].has_key ? DASH_DATA_WAITING : DASH_DATA_NO_TOKEN;
    portEXIT_CRITICAL(&data_lock);

    bool created = xTaskCreate(data_supervisor, "data_supervisor", 3072, NULL, 3, NULL) == pdPASS;
    for (int i = 0; created && i < WORKER_COUNT; ++i)
        created = xTaskCreate(data_task, workers[i].name, 10240, &workers[i], 3, NULL) == pdPASS;
    portENTER_CRITICAL(&data_lock);
    startup_state = created ? START_READY : START_FAILED;
    data_started = created;
    if (!created) recovery.state = DASH_RECOVERY_FAILED;
    portEXIT_CRITICAL(&data_lock);
    /* On failure waiting tasks self-delete; no active HTTP task is ever killed.
       A failed startup requires reboot before trying creation again. */
    return created ? ESP_OK : ESP_ERR_NO_MEM;
}
