#pragma once

#include "dashboard_alert_details.h"

enum { DASH_HISTORY_RECORDS = 10, DASH_HISTORY_BODY_CAPACITY = 2 * 1024 * 1024, DASH_HISTORY_TTL_S = 900 };
typedef enum { DASH_HISTORY_UNKNOWN, DASH_HISTORY_UNFINISHED, DASH_HISTORY_FINISHED } dashboard_history_finish_t;
typedef struct {
    char id[32];
    char location[96];
    int64_t started_at;
    int64_t finished_at;
    dashboard_history_finish_t finish;
    bool location_truncated;
} dashboard_alert_record_history_t;
typedef struct {
    dashboard_alert_record_history_t records[DASH_HISTORY_RECORDS];
    unsigned count;
    unsigned scanned;
    unsigned air_raids;
    char first_type[32];
    char first_uid[32];
    char first_oblast_uid[32];
    char first_location[96];
    char first_oblast[96];
} dashboard_alert_history_t;
typedef struct {
    dashboard_alert_cache_t cache;
    int64_t attempts[2];
    int64_t last_attempt_s;
    unsigned attempt_count;
    bool attempted;
    bool pending;
    bool in_flight;
    bool failed;
} dashboard_history_request_t;

bool dashboard_alert_history_parse(const char *body, size_t length, dashboard_alert_history_t *out);
int64_t dashboard_alert_history_minutes(const dashboard_alert_record_history_t *record, int64_t now);
void dashboard_alert_history_format(const dashboard_alert_record_history_t *record, int64_t now,
                                    char *text, size_t capacity);
bool dashboard_history_open(dashboard_history_request_t *request, int64_t now_s);
bool dashboard_history_begin(dashboard_history_request_t *request, dashboard_alert_rate_t *shared,
                             int64_t now_s, bool online);
bool dashboard_history_complete(dashboard_history_request_t *request, int status, bool parsed,
                                const char *modified, int64_t now_s);
