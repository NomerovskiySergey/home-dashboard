#include "dashboard_wifi.h"
#include "dashboard_wifi_web.h"

#include <inttypes.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "network_provisioning/manager.h"
#include "network_provisioning/scheme_softap.h"
#include "nvs.h"
#include "nvs_flash.h"

ESP_EVENT_DEFINE_BASE(DASH_WIFI_EVENT);

enum { REQUEST_SETUP, RETRY_CONNECTION };
static const uint64_t retry_delay_us = 5000000;
static const char *const tag = "dashboard_wifi";
static const char *const config_key = "station_v1";

static atomic_int state = DASH_WIFI_CONNECTING;
static esp_timer_handle_t retry_timer;
static nvs_handle_t settings;
static char setup_name[20];
static char setup_code[17];

/* Only the default ESP event loop accesses this after startup. */
static bool setup_active;
static esp_netif_t *setup_ap;
static httpd_handle_t setup_http;

static void set_state(dashboard_wifi_state_t next)
{
    atomic_store(&state, next);
}

dashboard_wifi_state_t dashboard_wifi_state(void)
{
    return atomic_load(&state);
}

const char *dashboard_wifi_setup_name(void)
{
    return setup_name;
}

const char *dashboard_wifi_setup_code(void)
{
    return setup_code;
}

static void schedule_retry(void)
{
    set_state(DASH_WIFI_OFFLINE);
    if (!esp_timer_is_active(retry_timer)) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_timer_start_once(retry_timer, retry_delay_us));
    }
}

static void retry_timer_callback(void *argument)
{
    if (esp_event_post(DASH_WIFI_EVENT, RETRY_CONNECTION, NULL, 0, 0) != ESP_OK) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_timer_start_once(retry_timer, retry_delay_us));
    }
}

static void stop_setup_http(void)
{
    network_prov_scheme_softap_set_httpd_handle(NULL);
    if (setup_http) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(httpd_stop(setup_http));
        setup_http = NULL;
    }
}

static esp_err_t start_setup(void)
{
    if (setup_active) {
        return ESP_OK;
    }

    const network_prov_mgr_config_t config = {
        .scheme = network_prov_scheme_softap,
        .scheme_event_handler = NETWORK_PROV_EVENT_HANDLER_NONE,
    };
    esp_err_t err = network_prov_mgr_init(config);
    if (err != ESP_OK) {
        return err;
    }

    err = dashboard_wifi_web_start(setup_ap, setup_code, &setup_http);
    if (err != ESP_OK) {
        network_prov_mgr_deinit();
        return err;
    }
    /* Protocomm dereferences the address of the HTTP server handle. */
    network_prov_scheme_softap_set_httpd_handle(&setup_http);
    setup_active = true;
    if (esp_timer_is_active(retry_timer)) {
        esp_timer_stop(retry_timer);
    }
    err = network_prov_mgr_start_provisioning(NETWORK_PROV_SECURITY_1,
                                               setup_code, setup_name, setup_code);
    if (err != ESP_OK) {
        network_prov_mgr_deinit();
        stop_setup_http();
        setup_active = false;
        return err;
    }
    set_state(DASH_WIFI_SETUP);
    return ESP_OK;
}

static esp_err_t save_station(void)
{
    wifi_config_t config = {0};
    esp_err_t err = esp_wifi_get_config(WIFI_IF_STA, &config);
    if (err == ESP_OK) {
        err = nvs_set_blob(settings, config_key, &config.sta, sizeof(config.sta));
    }
    if (err == ESP_OK) {
        err = nvs_commit(settings);
    }
    memset(&config, 0, sizeof(config));
    return err;
}

static void handle_provisioning(int32_t event)
{
    switch (event) {
    case NETWORK_PROV_WIFI_CRED_RECV:
        set_state(DASH_WIFI_SETUP);
        break;
    case NETWORK_PROV_WIFI_CRED_FAIL:
        set_state(DASH_WIFI_SETUP_FAILED);
        ESP_ERROR_CHECK_WITHOUT_ABORT(network_prov_mgr_reset_wifi_sm_state_on_failure());
        break;
    case NETWORK_PROV_WIFI_CRED_SUCCESS:
        if (save_station() == ESP_OK) {
            set_state(DASH_WIFI_CONNECTED);
        } else {
            ESP_LOGE(tag, "Could not save Wi-Fi configuration");
            set_state(DASH_WIFI_ERROR);
        }
        break;
    case NETWORK_PROV_END: {
        network_prov_mgr_deinit();
        stop_setup_http();
        setup_active = false;
        wifi_ap_record_t access_point;
        if (dashboard_wifi_state() == DASH_WIFI_CONNECTED &&
            esp_wifi_sta_get_ap_info(&access_point) != ESP_OK) {
            schedule_retry();
        }
        break;
    }
    default:
        break;
    }
}

static void handle_event(void *argument, esp_event_base_t base, int32_t event, void *data)
{
    if (base == NETWORK_PROV_EVENT) {
        if (setup_active) {
            handle_provisioning(event);
        }
    } else if (base == DASH_WIFI_EVENT && event == REQUEST_SETUP) {
        esp_err_t err = start_setup();
        if (err != ESP_OK) {
            ESP_LOGE(tag, "Could not start Wi-Fi setup: %s", esp_err_to_name(err));
            set_state(DASH_WIFI_ERROR);
        }
    } else if (setup_active) {
        return;
    } else if (base == WIFI_EVENT && event == WIFI_EVENT_STA_DISCONNECTED) {
        schedule_retry();
    } else if (base == IP_EVENT && event == IP_EVENT_STA_GOT_IP) {
        set_state(DASH_WIFI_CONNECTED);
    } else if (base == DASH_WIFI_EVENT && event == RETRY_CONNECTION &&
               dashboard_wifi_state() == DASH_WIFI_OFFLINE) {
        if (esp_wifi_connect() != ESP_OK) {
            schedule_retry();
        }
    }
}

esp_err_t dashboard_wifi_request_setup(void)
{
    return esp_event_post(DASH_WIFI_EVENT, REQUEST_SETUP, NULL, 0, 0);
}

esp_err_t dashboard_wifi_start(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(nvs_open("dashboard_wifi", NVS_READWRITE, &settings));
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    if (!esp_netif_create_default_wifi_sta()) {
        return ESP_ERR_NO_MEM;
    }
    setup_ap = esp_netif_create_default_wifi_ap();
    if (!setup_ap) {
        return ESP_ERR_NO_MEM;
    }

    const wifi_init_config_t wifi_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_config));
    /* The dashboard restores only settings that previously obtained an IP. */
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    const esp_timer_create_args_t timer = {
        .callback = retry_timer_callback,
        .name = "wifi_retry",
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer, &retry_timer));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, handle_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, handle_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(NETWORK_PROV_EVENT, ESP_EVENT_ANY_ID, handle_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(DASH_WIFI_EVENT, ESP_EVENT_ANY_ID, handle_event, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_STA, mac));
    snprintf(setup_name, sizeof(setup_name), "PROV_DASH_%02X%02X%02X",
             (unsigned)mac[3], (unsigned)mac[4], (unsigned)mac[5]);
    const uint32_t code_high = esp_random();
    const uint32_t code_low = esp_random();
    snprintf(setup_code, sizeof(setup_code), "%08" PRIX32 "%08" PRIX32, code_high, code_low);

    wifi_config_t saved = {0};
    size_t size = sizeof(saved.sta);
    esp_err_t err = nvs_get_blob(settings, config_key, &saved.sta, &size);
    if (err == ESP_ERR_NVS_NOT_FOUND || err == ESP_ERR_NVS_INVALID_LENGTH ||
        (err == ESP_OK && size != sizeof(saved.sta))) {
        return dashboard_wifi_request_setup();
    }
    if (err != ESP_OK) {
        return err;
    }

    err = esp_wifi_set_config(WIFI_IF_STA, &saved);
    memset(&saved, 0, sizeof(saved));
    if (err != ESP_OK) {
        return err;
    }
    if (esp_wifi_connect() != ESP_OK) {
        schedule_retry();
    }
    return ESP_OK;
}
