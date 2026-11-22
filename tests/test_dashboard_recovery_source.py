"""Exercise the real data worker against a fake ESP-IDF runtime (MSVC required)."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

SDK = r"""
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_TIMEOUT 0x107
#define ESP_ERR_HTTP_EAGAIN 0x7007
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
#define pdMS_TO_TICKS(x) (x)
#define pdPASS 1
#define NVS_READONLY 0
#define HTTP_EVENT_ON_HEADER 1
#define HTTP_EVENT_ON_DATA 2
typedef int portMUX_TYPE;
typedef void *TaskHandle_t;
typedef int nvs_handle_t;
typedef struct {
    int event_id;
    void *user_data;
    char *header_key, *header_value;
    int data_len;
    char *data;
} esp_http_client_event_t;
typedef struct {
    const char *url;
    int timeout_ms;
    bool disable_auto_redirect, is_async;
    esp_err_t (*crt_bundle_attach)(void *);
    esp_err_t (*event_handler)(esp_http_client_event_t *);
    void *user_data;
} esp_http_client_config_t;
typedef void *esp_http_client_handle_t;
typedef struct { bool start; } esp_sntp_config_t;
#define ESP_NETIF_SNTP_DEFAULT_CONFIG(server) { true }
esp_err_t esp_crt_bundle_attach(void *);
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *);
esp_err_t esp_http_client_set_header(esp_http_client_handle_t,const char *,const char *);
esp_err_t esp_http_client_perform(esp_http_client_handle_t);
int esp_http_client_get_status_code(esp_http_client_handle_t);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t);
void *heap_caps_malloc(size_t, int);
esp_err_t esp_netif_sntp_init(const esp_sntp_config_t *);
esp_err_t esp_netif_sntp_start(void);
int64_t esp_timer_get_time(void);
int xTaskCreate(void (*)(void *), const char *, unsigned, void *, unsigned, TaskHandle_t *);
void vTaskDelete(TaskHandle_t);
void vTaskDelay(unsigned);
esp_err_t nvs_flash_init_partition(const char *);
esp_err_t nvs_open_from_partition(const char *, const char *, int, nvs_handle_t *);
esp_err_t nvs_get_str(nvs_handle_t,const char *,char *,size_t *);
void nvs_close(nvs_handle_t);
const char *esp_err_to_name(esp_err_t);
"""

FIXTURE = r"""
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "sdk_stubs.h"

static void tracked_free(void *);
#define free tracked_free
#include "dashboard_data.c"
#undef free

static int64_t clock_us = 1000000;
static int creates, deletes, cleanups, performs, clients, fail_create;
static int mode, response_status = 200, delays, stop_after;
static bool drive_worker;
static jmp_buf worker_exit;
static esp_http_client_config_t http_config;
static void *allocations[32];
static unsigned live_allocations;

int64_t esp_timer_get_time(void) { return clock_us; }
const char *esp_err_to_name(esp_err_t err) { return "fake"; }
esp_err_t esp_crt_bundle_attach(void *p) { return ESP_OK; }
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config)
{
    assert(config->is_async);
    assert(clients == 0);
    clients++;
    http_config = *config;
    return &http_config;
}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t c,const char *k,const char *v)
{ return ESP_OK; }
esp_err_t esp_http_client_perform(esp_http_client_handle_t c)
{
    performs++;
    if (mode == 1) { assert(dashboard_data_restart()); return ESP_ERR_HTTP_EAGAIN; }
    if (mode == 2) return ESP_ERR_HTTP_EAGAIN; /* peer never completes */
    if (response_status == 429) {
        esp_http_client_event_t event = { .event_id=HTTP_EVENT_ON_HEADER,
            .header_key="Retry-After", .header_value="120", .user_data=http_config.user_data };
        http_config.event_handler(&event);
    }
    if (mode == 3) assert(dashboard_data_restart()); /* response completes as reset arrives */
    return ESP_OK;
}
int esp_http_client_get_status_code(esp_http_client_handle_t c) { return response_status; }
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t c)
{ assert(clients == 1); clients--; cleanups++; return ESP_OK; }
void *heap_caps_malloc(size_t size, int caps)
{
    void *p = malloc(size);
    assert(p && live_allocations < 32);
    allocations[live_allocations++] = p;
    return p;
}
static void tracked_free(void *p)
{
    if (!p) return;
    unsigned i;
    for (i=0; i<live_allocations; ++i) if (allocations[i]==p) break;
    assert(i < live_allocations);
    allocations[i] = allocations[--live_allocations];
    free(p);
}
esp_err_t esp_netif_sntp_init(const esp_sntp_config_t *c) { return ESP_OK; }
esp_err_t esp_netif_sntp_start(void) { return ESP_OK; }
int xTaskCreate(void (*f)(void *),const char *n,unsigned stack,void *arg,unsigned prio,TaskHandle_t *h)
{ creates++; if(creates == fail_create) return 0; if(h) *h=(void *)1; return pdPASS; }
void vTaskDelete(TaskHandle_t task) { deletes++; }
void vTaskDelay(unsigned ms)
{
    clock_us += (int64_t)ms * 1000;
    if (drive_worker && ms == 1000) {
        delays++;
        dashboard_recovery_poll(&recovery, clock_us);
        assert(recovery.state != DASH_RECOVERY_FAILED);
        if (delays == stop_after) longjmp(worker_exit, 1);
        /* Repeated manual requests throughout 120 seconds offline. */
        dashboard_data_restart();
    }
}
esp_err_t nvs_flash_init_partition(const char *p) { return ESP_OK; }
esp_err_t nvs_open_from_partition(const char *p,const char *ns,int mode,nvs_handle_t *h)
{ *h=1; return ESP_OK; }
esp_err_t nvs_get_str(nvs_handle_t h,const char *k,char *out,size_t *n)
{ strcpy(out,"fake-key"); *n=9; return ESP_OK; }
void nvs_close(nvs_handle_t h) {}
dashboard_wifi_state_t dashboard_wifi_state(void) { return DASH_WIFI_OFFLINE; }

int main(void)
{
    assert(dashboard_data_start() == ESP_OK && creates == 2);
    assert(dashboard_data_start() == ESP_ERR_INVALID_STATE && creates == 2);
    char storage[256];
    http_body_t body = { .text=storage, .capacity=sizeof(storage) };
    latest.weather_updated_us=123;
    latest.temperature_tenths=170;
    latest.weather_state=DASH_DATA_FRESH;
    mode=1;
    update_weather(&body);
    assert(body.cancelled && performs==1 && cleanups==1 && clients==0);
    assert(latest.weather_updated_us==123 && latest.temperature_tenths==170);
    dashboard_data_snapshot_t snapshot;
    dashboard_data_snapshot(&snapshot);
    assert(snapshot.weather_state==DASH_DATA_STALE);
    assert(!dashboard_data_restart() && creates==2);
    dashboard_recovery_begin(&recovery, clock_us);
    dashboard_recovery_finish(&recovery, clock_us);
    mode=0;
    response_status=429;
    assert(fetch_json(SOURCE_ENERGY,"fake",&body)==429);
    const int64_t paused_until=retry_not_before[SOURCE_ENERGY];
    assert(paused_until==clock_us+120000000);
    response_status=200;
    mode=2;
    const int before=cleanups;
    fetch_json(SOURCE_WEATHER,NULL,&body);
    assert(!body.cancelled && cleanups==before+1 && clients==0);
    assert(clock_us>=21000000); /* absolute request deadline */
    /* Existing rate limits/cache remain through repeated worker-owned cleanup. */
    map_cache.valid=true;
    strcpy(map_cache.modified,"cached");
    alerts_rate.count=1;
    alerts_rate.requests[0]=clock_us/1000000;
    alerts_rate.blocked_until=clock_us/1000000+300;
    const int64_t blocked_until=alerts_rate.blocked_until;
    mode=0;
    drive_worker=true;
    stop_after=120;
    if (setjmp(worker_exit)==0) data_task(NULL);
    drive_worker=false;
    assert(recovery.restarts>10 && live_allocations==2);
    assert(creates==2 && deletes==0 && clients==0);
    assert(map_cache.valid && !strcmp(map_cache.modified,"cached"));
    assert(alerts_rate.count==1 && alerts_rate.blocked_until==blocked_until);
    assert(retry_not_before[SOURCE_ENERGY]==paused_until);
    assert(latest.weather_updated_us==123 && latest.temperature_tenths==170);
    while(live_allocations) tracked_free(allocations[0]);
    clock_us += DASH_RECOVERY_COOLDOWN_US;
    alerts_rate=(dashboard_alert_rate_t){0};
    mode=3;
    response_status=429;
    assert(fetch_json(SOURCE_ALERTS,"fake",&body)==0 && body.cancelled);
    assert(alerts_rate.blocked_until>=clock_us/1000000+120);
    assert(!dashboard_alert_rate_take(&alerts_rate,clock_us/1000000));
    /* Failure to allocate a worker never leaves a supervisor or duplicates. */
    data_started=false;
    fail_create=creates+2;
    assert(dashboard_data_start()==ESP_ERR_NO_MEM);
    assert(!data_started && deletes==1 && recovery.state==DASH_RECOVERY_FAILED);
    assert(!dashboard_data_restart());
    puts("real data worker recovery: PASS");
    return 0;
}
"""


class RecoveryIntegration(unittest.TestCase):
    def test_worker_cancellation_cleanup_backoff_and_offline(self):
        with tempfile.TemporaryDirectory(prefix="recovery-test-", dir=ROOT / "build") as directory:
            path = Path(directory)
            (path / "sdk_stubs.h").write_text(SDK, encoding="utf-8")
            headers = ["esp_err.h", "esp_crt_bundle.h", "esp_http_client.h",
                       "esp_heap_caps.h", "esp_log.h", "esp_netif_sntp.h", "esp_timer.h",
                       "nvs.h", "nvs_flash.h", "freertos/FreeRTOS.h", "freertos/task.h"]
            for header in headers:
                target = path / header
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_text('#include "sdk_stubs.h"\n', encoding="utf-8")
            (path / "strings.h").write_text(
                '#include <string.h>\n#define strcasecmp _stricmp\n', encoding="utf-8")
            fixture = path / "fixture.c"
            fixture.write_text(FIXTURE, encoding="utf-8")
            cjson = ROOT / "managed_components/espressif__cjson/cJSON"
            sources = [fixture, ROOT / "main/dashboard_recovery.c",
                       ROOT / "main/dashboard_data_parse.c", ROOT / "main/dashboard_energy_parse.c",
                       ROOT / "main/dashboard_alert_details.c", cjson / "cJSON.c"]
            command = ["cl", "/nologo", "/std:c11", "/utf-8", "/D_CRT_SECURE_NO_WARNINGS",
                       "/I" + str(path), "/I" + str(ROOT / "main"), "/I" + str(cjson),
                       *map(str, sources), "/Fe:" + str(path / "recovery_integration.exe")]
            compiled = subprocess.run(command, cwd=path, capture_output=True, text=True)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            tested = subprocess.run([str(path / "recovery_integration.exe")],
                                    cwd=path, capture_output=True, text=True, timeout=20)
            self.assertEqual(tested.returncode, 0, tested.stdout + tested.stderr)
            print(tested.stdout.strip())


if __name__ == "__main__":
    unittest.main()
