#include <assert.h>
#include <stdio.h>
#include "dashboard_recovery.h"

static void manual_restart(void)
{
    dashboard_recovery_t r;
    dashboard_recovery_init(&r, 100);
    assert(dashboard_recovery_request(&r, 200, false));
    assert(r.state == DASH_RECOVERY_REQUESTED && r.pending);
    assert(!dashboard_recovery_request(&r, 201, false));
    assert(r.requests == 1);
    dashboard_recovery_begin(&r, 300);
    assert(r.state == DASH_RECOVERY_RESTARTING && r.pending);
    assert(!dashboard_recovery_request(&r, 301, false));
    dashboard_recovery_finish(&r, 400);
    assert(!r.pending && r.state == DASH_RECOVERY_RUNNING && r.restarts == 1);
    assert(!dashboard_recovery_request(&r, 401, false)); /* debounce after completion */
    assert(dashboard_recovery_request(&r, 400 + DASH_RECOVERY_COOLDOWN_US, false));
}

static void watchdog(void)
{
    dashboard_recovery_t r;
    dashboard_recovery_init(&r, 100);
    /* Working loop heartbeats also occur while offline or waiting for SNTP.
       Successful network responses are deliberately irrelevant. */
    for (int i = 1; i < 200; ++i) {
        const int64_t now = 100 + (int64_t)i * 1000000;
        dashboard_recovery_heartbeat(&r, now);
        dashboard_recovery_poll(&r, now);
        assert(!r.pending && r.requests == 0);
    }
    const int64_t stalled = r.heartbeat_us + DASH_RECOVERY_STALL_US;
    dashboard_recovery_poll(&r, stalled - 1);
    assert(!r.pending);
    dashboard_recovery_poll(&r, stalled);
    assert(r.pending && r.automatic && r.requests == 1);
    dashboard_recovery_poll(&r, stalled + DASH_RECOVERY_GRACE_US - 1);
    assert(r.state == DASH_RECOVERY_REQUESTED);
    dashboard_recovery_poll(&r, stalled + DASH_RECOVERY_GRACE_US);
    assert(r.state == DASH_RECOVERY_FAILED);
    for (int i = 1; i < 100; ++i)
        dashboard_recovery_poll(&r, stalled + DASH_RECOVERY_GRACE_US + i * 1000000LL);
    assert(r.requests == 1); /* a hung owner never gets a second worker */
    dashboard_recovery_heartbeat(&r, stalled + 200000000);
    assert(r.state == DASH_RECOVERY_FAILED && r.pending); /* heartbeat cannot acknowledge cleanup */
    dashboard_recovery_begin(&r, stalled + 201000000);
    dashboard_recovery_finish(&r, stalled + 202000000);
    assert(r.state == DASH_RECOVERY_RUNNING && !r.pending && r.restarts == 1);
}

static void cleanup_deadline(void)
{
    dashboard_recovery_t r;
    dashboard_recovery_init(&r, 0);
    assert(dashboard_recovery_request(&r, 100, false));
    dashboard_recovery_begin(&r, 200);
    dashboard_recovery_poll(&r, 100 + DASH_RECOVERY_GRACE_US);
    assert(r.state == DASH_RECOVERY_FAILED && r.pending);
    dashboard_recovery_finish(&r, 100 + DASH_RECOVERY_GRACE_US + 1);
    assert(r.state == DASH_RECOVERY_RUNNING);
    /* A new source error / absence of a response never requests recovery. */
    dashboard_recovery_poll(&r, r.heartbeat_us + 1000);
    assert(!r.pending && r.requests == 1);
}

static void service_isolation(void)
{
    dashboard_recovery_t workers[3], group;
    dashboard_recovery_init(&group, 100);
    for (int i = 0; i < 3; ++i) dashboard_recovery_init(&workers[i], 100);
    const int64_t now = 100 + DASH_RECOVERY_STALL_US;
    dashboard_recovery_heartbeat(&workers[0], now);
    dashboard_recovery_heartbeat(&workers[2], now);
    dashboard_recovery_group_poll(workers, 3, &group, now);
    assert(!workers[0].pending && workers[1].pending && !workers[2].pending);
    assert(group.pending && group.automatic);
    dashboard_recovery_group_poll(workers, 3, &group, now + DASH_RECOVERY_GRACE_US);
    assert(group.state == DASH_RECOVERY_FAILED);
    dashboard_recovery_begin(&workers[1], now + DASH_RECOVERY_GRACE_US + 1);
    dashboard_recovery_finish(&workers[1], now + DASH_RECOVERY_GRACE_US + 2);
    dashboard_recovery_group_update(workers, 3, &group, now + DASH_RECOVERY_GRACE_US + 2);
    assert(!group.pending && group.restarts == 1);
    assert(!dashboard_recovery_group_request(workers, 3, &group, group.restarted_us));
    const int64_t manual = group.restarted_us + DASH_RECOVERY_COOLDOWN_US;
    assert(dashboard_recovery_group_request(workers, 3, &group, manual));
    assert(!dashboard_recovery_group_request(workers, 3, &group, manual + 1));
    for (int i = 0; i < 3; ++i) assert(workers[i].pending);
    dashboard_recovery_finish(&workers[0], manual + 1);
    dashboard_recovery_finish(&workers[2], manual + 2);
    dashboard_recovery_group_update(workers, 3, &group, manual + 2);
    assert(group.pending && group.restarts == 1); /* last worker must acknowledge */
    dashboard_recovery_finish(&workers[1], manual + 3);
    dashboard_recovery_group_update(workers, 3, &group, manual + 3);
    assert(!group.pending && group.restarts == 2);
}

int main(void)
{
    manual_restart();
    watchdog();
    cleanup_deadline();
    service_isolation();
    puts("dashboard recovery: PASS");
    return 0;
}
