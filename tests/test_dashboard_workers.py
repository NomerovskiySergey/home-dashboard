"""Run actual service workers concurrently against a fake ESP-IDF (MSVC/Windows)."""
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_dashboard_recovery_source import SDK, ROOT

THREAD_SDK = '#include <windows.h>\n' + SDK.replace(
    '#define portMUX_INITIALIZER_UNLOCKED 0', '#define portMUX_INITIALIZER_UNLOCKED SRWLOCK_INIT'
).replace('#define portENTER_CRITICAL(x) (++critical_depth)', '#define portENTER_CRITICAL(x) (AcquireSRWLockExclusive(x), ++critical_depth)').replace(
    '#define portEXIT_CRITICAL(x) (--critical_depth)', '#define portEXIT_CRITICAL(x) (--critical_depth, ReleaseSRWLockExclusive(x))'
).replace('typedef int portMUX_TYPE;', 'typedef SRWLOCK portMUX_TYPE;')

FIXTURE = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <process.h>
#include "sdk_stubs.h"
#include "dashboard_data.h"
#include "dashboard_wifi.h"

static void tracked_free(void *);
#define free tracked_free
#include "dashboard_data.c"
#undef free

typedef struct { void (*fn)(void *); void *arg; int service; } task_t;
static task_t tasks[8];
static int creates, registered, fail_create, stalled=-1, oom=-1;
static LONG stop, live_clients, requests[3], sntp_inits, sntp_starts;
static LONG64 offset_us;
static bool missing_keys, offline, automatic_case;
static int location_case;
static bool catalog_case;
static char catalog_json[8192];
static HANDLE catalog_inflight,release_catalog;
static HANDLE release_request;
static __declspec(thread) int service=-1;
static SRWLOCK memory_lock=SRWLOCK_INIT;
static void *allocations[16];
static unsigned allocation_count;
typedef struct { esp_http_client_config_t config; int service; } client_t;

int64_t esp_timer_get_time(void) { return (int64_t)GetTickCount64()*1000 + InterlockedCompareExchange64(&offset_us,0,0); }
const char *esp_err_to_name(esp_err_t err) { return "fake"; }
esp_err_t esp_crt_bundle_attach(void *p) { return ESP_OK; }
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config)
{
    assert(config->is_async);
    client_t *client=malloc(sizeof(*client)); assert(client);
    client->config=*config;
    client->service=strstr(config->url,"open-meteo")?0:strstr(config->url,"alerts.energy")?2:1;
    InterlockedIncrement(&live_clients);
    return client;
}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t c,const char *k,const char *v) { return ESP_OK; }
esp_err_t esp_http_client_perform(esp_http_client_handle_t handle)
{
    client_t *client=handle;
    InterlockedIncrement(&requests[client->service]);
    if (client->service==stalled) assert(WaitForSingleObject(release_request,10000)==WAIT_OBJECT_0);
    const char *body=client->service==0
        ? "{\"current_units\":{\"temperature_2m\":\"\xC2\xB0" "C\"},\"current\":{\"temperature_2m\":17}}"
        : client->service==2
        ? "[{\"initiator\":\"dnipropetrovska_oblast_cek\",\"queue\":\"5.2\",\"today_date\":\"2026-11-22\",\"today_status\":\"NoShutdowns\",\"today\":[],\"tomorrow_date\":\"2026-11-23\",\"tomorrow_status\":\"WaitingForSchedule\",\"tomorrow\":[]}]"
        : strstr(client->config.url,"active.json") ? "{\"alerts\":[]}" : "\"NNNNNNNNNNNNNNNNNNNNNNNNNNN\"";
    if(catalog_case && strstr(client->config.url,"/overview")) {
        SetEvent(catalog_inflight);
        assert(WaitForSingleObject(release_catalog,10000)==WAIT_OBJECT_0);
        body=catalog_json;
    }
    esp_http_client_event_t event={.event_id=HTTP_EVENT_ON_DATA,.data=(char *)body,
        .data_len=(int)strlen(body),.user_data=client->config.user_data};
    client->config.event_handler(&event);
    return ESP_OK;
}
int esp_http_client_get_status_code(esp_http_client_handle_t c) { return 200; }
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t c)
{ free(c); InterlockedDecrement(&live_clients); return ESP_OK; }
void *heap_caps_malloc(size_t size,int caps)
{
    if (oom>=0 && service==oom) return NULL;
    void *p=malloc(size); assert(p);
    AcquireSRWLockExclusive(&memory_lock);
    assert(allocation_count<16); allocations[allocation_count++]=p;
    ReleaseSRWLockExclusive(&memory_lock);
    return p;
}
static void tracked_free(void *p)
{
    if(!p) return;
    AcquireSRWLockExclusive(&memory_lock);
    unsigned i; for(i=0;i<allocation_count;++i) if(allocations[i]==p) break;
    assert(i<allocation_count); allocations[i]=allocations[--allocation_count];
    ReleaseSRWLockExclusive(&memory_lock); free(p);
}
esp_err_t esp_netif_sntp_init(const esp_sntp_config_t *c) { InterlockedIncrement(&sntp_inits); return ESP_OK; }
esp_err_t esp_netif_sntp_start(void) { InterlockedIncrement(&sntp_starts); return ESP_OK; }
int xTaskCreate(void (*f)(void *),const char *name,unsigned stack,void *arg,unsigned priority,TaskHandle_t *h)
{
    if(++creates==fail_create) return 0;
    tasks[registered]=(task_t){f,arg,strstr(name,"weather")?0:strstr(name,"alerts")?1:strstr(name,"energy")?2:-1};
    if(h) *h=&tasks[registered]; registered++; return pdPASS;
}
void vTaskDelete(TaskHandle_t h) { assert(!h); _endthreadex(0); }
void vTaskDelay(unsigned ms) { if(InterlockedCompareExchange(&stop,0,0)) _endthreadex(0); Sleep(ms<10?ms:10); }
esp_err_t nvs_flash_init_partition(const char *p) { return ESP_OK; }
esp_err_t nvs_open_from_partition(const char *p,const char *ns,int mode,nvs_handle_t *h) { *h=1;return ESP_OK; }
esp_err_t nvs_get_str(nvs_handle_t h,const char *key,char *out,size_t *n)
{ if(missing_keys) return ESP_FAIL;strcpy(out,"fake-key");*n=9;return ESP_OK; }
esp_err_t nvs_open(const char *ns,int m,nvs_handle_t *h) { *h=1; return ESP_OK; }
esp_err_t nvs_set_str(nvs_handle_t h,const char *k,const char *v) { return ESP_OK; }
esp_err_t nvs_commit(nvs_handle_t h) { return ESP_OK; }
void nvs_close(nvs_handle_t h) {}
dashboard_wifi_state_t dashboard_wifi_state(void) { return offline?DASH_WIFI_OFFLINE:DASH_WIFI_CONNECTED; }
static unsigned __stdcall run(void *p) { task_t *task=p;service=task->service;task->fn(task->arg);return 0; }
static bool fresh_except(int excluded)
{
    dashboard_data_snapshot_t s;dashboard_data_snapshot(&s);
    return (excluded==0||s.weather_updated_us) && (excluded==1||(s.alerts_updated_us&&s.alert_details_updated_us)) && (excluded==2||s.energy_updated_us);
}
static void wait_fresh(int excluded)
{
    const ULONGLONG deadline=GetTickCount64()+3000;
    while(!fresh_except(excluded)&&GetTickCount64()<deadline) Sleep(5);
    assert(fresh_except(excluded));
}
int main(int argc,char **argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    stalled=argc>1?atoi(argv[1]):-1;
    if(argc>2) fail_create=atoi(argv[2]);
    if(argc>3) oom=atoi(argv[3]);
    if(argc>4) missing_keys=atoi(argv[4])!=0;
    if(argc>5) offline=atoi(argv[5])!=0;
    if(argc>6) automatic_case=atoi(argv[6])!=0;
    if(argc>7) location_case=atoi(argv[7]);
    if(argc>8) catalog_case=atoi(argv[8])!=0;
    release_request=CreateEvent(NULL,TRUE,FALSE,NULL);assert(release_request);
    if(catalog_case) {
        catalog_inflight=CreateEvent(NULL,TRUE,FALSE,NULL);
        release_catalog=CreateEvent(NULL,TRUE,FALSE,NULL);
        strcpy(catalog_json,"[");
        for(unsigned i=0;i<64;++i) {
            char row[96];snprintf(row,sizeof(row),"%s{\"slug\":\"area-%u\",\"name\":\"Area %u\"}",i?",":"",i,i);
            strcat(catalog_json,row);
        }
        strcat(catalog_json,"]");
    }
    const esp_err_t started=dashboard_data_start();
    assert(started==(fail_create?ESP_ERR_NO_MEM:ESP_OK));
    if(!fail_create) {
        assert(registered==4); /* exactly three service workers and one supervisor */
        assert(dashboard_data_start()==ESP_ERR_INVALID_STATE&&registered==4);
    }
    HANDLE threads[8];
    for(int i=0;i<registered;++i) threads[i]=(HANDLE)_beginthreadex(NULL,0,run,&tasks[i],0,NULL);
    if(fail_create) {
        if(registered) assert(WaitForMultipleObjects(registered,threads,TRUE,3000)==WAIT_OBJECT_0);
        assert(live_clients==0&&allocation_count==0&&sntp_inits==0);
        dashboard_recovery_t r;dashboard_data_recovery(&r);assert(r.state==DASH_RECOVERY_FAILED);
    } else if(location_case) {
        if(!offline) wait_fresh(stalled);
        const dashboard_city_t *kyiv=dashboard_city_find("UA80000000000093317");
        dashboard_data_snapshot_t before,after;
        dashboard_data_snapshot(&before);
        assert(dashboard_data_select_location(kyiv)==ESP_OK);
        if(location_case==2) assert(dashboard_data_select_location(dashboard_city_default())==ESP_OK);
        dashboard_data_snapshot(&after);
        assert(after.location_generation>before.location_generation);
        assert(!after.weather_updated_us);
        if(offline) { Sleep(50); assert(!fresh_except(-1)); offline=false; }
        if(stalled>=0) SetEvent(release_request);
        wait_fresh(-1);
        dashboard_data_snapshot(&after);
        assert(after.location==(location_case==2?dashboard_city_default():kyiv));
        if(before.energy_updated_us) assert(after.energy_updated_us==before.energy_updated_us);
        dashboard_recovery_t r;dashboard_data_recovery(&r);assert(!r.pending&&!r.restarts);
        /* A completed normal request is scheduled 600s away; switch must refresh promptly. */
        const uint32_t old_generation=after.location_generation;
        assert(dashboard_data_select_location(after.location==kyiv?dashboard_city_default():kyiv)==ESP_OK);
        wait_fresh(-1);dashboard_data_snapshot(&after);
        assert(after.location_generation==old_generation+1);
    } else if(offline||missing_keys) {
        if(missing_keys&&!offline) {
            dashboard_data_snapshot_t s;
            const ULONGLONG deadline=GetTickCount64()+3000;
            do { dashboard_data_snapshot(&s);Sleep(5); } while(!s.weather_updated_us&&GetTickCount64()<deadline);
            assert(s.weather_updated_us);
        }
        Sleep(100);
        dashboard_data_snapshot_t s;dashboard_data_snapshot(&s);
        if(offline) assert(!s.weather_updated_us&&!s.alerts_updated_us&&!s.energy_updated_us);
        if(missing_keys) assert(s.alerts_state==DASH_DATA_NO_TOKEN&&s.energy_state==DASH_DATA_NO_TOKEN);
        assert(dashboard_data_restart());Sleep(100);
        dashboard_recovery_t r;dashboard_data_recovery(&r);assert(!r.pending&&r.state==DASH_RECOVERY_RUNNING);
    } else {
        wait_fresh(stalled>=0?stalled:oom);
        if(catalog_case) {
            dashboard_energy_catalog_t *copy=malloc(sizeof(*copy));assert(copy);
            for(unsigned round=0;round<10;++round) {
                ResetEvent(catalog_inflight);ResetEvent(release_catalog);
                uint32_t stale=dashboard_data_energy_catalog_request(DASH_ENERGY_AREAS,NULL,NULL);assert(stale);
                assert(WaitForSingleObject(catalog_inflight,3000)==WAIT_OBJECT_0);
                /* Cancel/free while the worker is in HTTPS, then request A again. */
                dashboard_data_energy_catalog_cancel();
                uint32_t generation=dashboard_data_energy_catalog_request(DASH_ENERGY_AREAS,NULL,NULL);
                dashboard_data_state_t state;
                assert(generation!=stale && !dashboard_data_energy_catalog_copy(stale,copy,&state));
                SetEvent(release_catalog);
                const ULONGLONG deadline=GetTickCount64()+3000;
                do {
                    assert(dashboard_data_energy_catalog_copy(generation,copy,&state));
                    Sleep(1);
                } while(state!=DASH_DATA_FRESH && GetTickCount64()<deadline);
                assert(state==DASH_DATA_FRESH && copy->count==64);
                for(unsigned i=0;i<64;++i) assert(!strncmp(copy->entries[i].id,"area-",5));
                dashboard_data_energy_catalog_cancel();
            }
            free(copy);CloseHandle(catalog_inflight);CloseHandle(release_catalog);
        }
        if(stalled>=0) {
            dashboard_data_snapshot_t s;dashboard_data_snapshot(&s);
            assert(stalled!=0||!s.weather_updated_us);
            assert(stalled!=1||(!s.alerts_updated_us&&!s.alert_details_updated_us));
            assert(stalled!=2||!s.energy_updated_us);
            Sleep(25); /* GetTickCount64 has a coarser resolution than the worker tick. */
            dashboard_recovery_t r;
            if(automatic_case) {
                /* Inject elapsed silence only for the blocked owner; the real supervisor polls it. */
                portENTER_CRITICAL(&data_lock);
                worker_recovery[stalled].heartbeat_us=esp_timer_get_time()-DASH_RECOVERY_STALL_US;
                portEXIT_CRITICAL(&data_lock);
                const ULONGLONG deadline=GetTickCount64()+1000;
                do { dashboard_data_recovery(&r);Sleep(5); } while(!r.pending&&GetTickCount64()<deadline);
                assert(r.pending&&r.automatic&&!dashboard_data_restart());
                portENTER_CRITICAL(&data_lock);
                for(int i=0;i<3;++i) assert(worker_recovery[i].pending==(i==stalled));
                worker_recovery[stalled].requested_us=esp_timer_get_time()-DASH_RECOVERY_GRACE_US;
                portEXIT_CRITICAL(&data_lock);
                do { dashboard_data_recovery(&r);Sleep(5); } while(r.state!=DASH_RECOVERY_FAILED&&GetTickCount64()<deadline);
                assert(r.state==DASH_RECOVERY_FAILED);
            } else {
                assert(dashboard_data_restart());assert(!dashboard_data_restart());Sleep(100);
                dashboard_data_recovery(&r);assert(r.pending&&r.restarts==0);
            }
            dashboard_data_snapshot_t healthy;dashboard_data_snapshot(&healthy);
            if(stalled!=0) assert(healthy.weather_state==DASH_DATA_FRESH&&(automatic_case||healthy.weather_updated_us>s.weather_updated_us));
            if(stalled!=1) assert(healthy.alerts_state==DASH_DATA_FRESH&&healthy.alert_details_state==DASH_DATA_FRESH&&(automatic_case||healthy.alerts_updated_us>s.alerts_updated_us));
            if(stalled!=2) assert(healthy.energy_state==DASH_DATA_FRESH&&(automatic_case||healthy.energy_updated_us>s.energy_updated_us));
            SetEvent(release_request);wait_fresh(-1);
        } else if(oom<0) {
            assert(dashboard_data_restart());
        }
        Sleep(100);
        dashboard_recovery_t r;dashboard_data_recovery(&r);assert(!r.pending);
        assert(registered==4&&sntp_inits==1&&sntp_starts==1);
        assert(allocation_count<=(missing_keys?0:2));
        if(oom==1) {
            dashboard_data_snapshot_t s;dashboard_data_snapshot(&s);
            assert(s.alerts_updated_us&&!s.alert_details_updated_us&&s.alert_details_state==DASH_DATA_UNAVAILABLE);
        }
    }
    InterlockedExchange(&stop,1);SetEvent(release_request);
    if(registered) assert(WaitForMultipleObjects(registered,threads,TRUE,3000)==WAIT_OBJECT_0);
    for(int i=0;i<registered;++i) CloseHandle(threads[i]);
    assert(live_clients==0);
    while(allocation_count) tracked_free(allocations[0]);CloseHandle(release_request);
    puts("concurrent service workers: PASS");return 0;
}
'''


class Workers(unittest.TestCase):
    def test_actual_service_workers(self):
        with tempfile.TemporaryDirectory(prefix="workers-test-", dir=ROOT / "build") as directory:
            path = Path(directory)
            (path / "sdk_stubs.h").write_text(THREAD_SDK, encoding="utf-8")
            for header in ["esp_err.h", "esp_crt_bundle.h", "esp_http_client.h", "esp_heap_caps.h",
                           "esp_log.h", "esp_netif_sntp.h", "esp_timer.h", "nvs.h", "nvs_flash.h",
                           "freertos/FreeRTOS.h", "freertos/task.h", "freertos/semphr.h"]:
                target = path / header
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_text('#include "sdk_stubs.h"\n', encoding="utf-8")
            (path / "strings.h").write_text('#include <string.h>\n#define strcasecmp _stricmp\n')
            (path / "fixture.c").write_text(FIXTURE, encoding="utf-8")
            cjson = ROOT / "managed_components/espressif__cjson/cJSON"
            sources = [path / "fixture.c", ROOT / "main/dashboard_recovery.c", ROOT / "main/dashboard_location.c",
                       ROOT / "main/dashboard_data_parse.c", ROOT / "main/dashboard_energy_parse.c", ROOT / "main/dashboard_energy.c",
                       ROOT / "main/dashboard_alert_details.c", cjson / "cJSON.c"]
            executable = path / "workers.exe"
            result = subprocess.run(["cl", "/nologo", "/std:c11", "/utf-8", "/D_CRT_SECURE_NO_WARNINGS",
                                     "/I" + str(path), "/I" + str(ROOT / "main"), "/I" + str(cjson),
                                     *map(str, sources), "/Fe:" + str(executable)], cwd=path,
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            for arguments in [[-1], [0], [1], [2], *[[-1, n] for n in range(1, 5)],
                              [-1, 0, 2], [-1, 0, 1], [-1, 0, -1, 1, 1],
                              [-1, 0, -1, 1, 0], [-1, 0, -1, 0, 1],
                              *[[n, 0, -1, 0, 0, 1] for n in range(3)],
                              [-1,0,-1,0,0,0,1], [0,0,-1,0,0,0,1],
                              [0,0,-1,0,0,0,2], [-1,0,-1,0,1,0,1],
                              [-1,0,-1,0,0,0,0,1]]:
                with self.subTest(arguments=arguments):
                    run = subprocess.run([str(executable), *map(str, arguments)], cwd=path,
                                         capture_output=True, text=True, timeout=15)
                    self.assertEqual(run.returncode, 0, run.stdout + run.stderr)


if __name__ == "__main__":
    unittest.main()
