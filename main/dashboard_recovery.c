#include "dashboard_recovery.h"

void dashboard_recovery_init(dashboard_recovery_t *recovery, int64_t now)
{
    *recovery = (dashboard_recovery_t){ .heartbeat_us = now };
}

bool dashboard_recovery_request(dashboard_recovery_t *recovery, int64_t now, bool automatic)
{
    if (recovery->pending ||
        (recovery->restarts && now - recovery->restarted_us < DASH_RECOVERY_COOLDOWN_US)) return false;
    recovery->pending = true;
    recovery->automatic = automatic;
    recovery->state = DASH_RECOVERY_REQUESTED;
    recovery->requested_us = now;
    ++recovery->requests;
    return true;
}

void dashboard_recovery_heartbeat(dashboard_recovery_t *recovery, int64_t now)
{
    recovery->heartbeat_us = now;
}

void dashboard_recovery_poll(dashboard_recovery_t *recovery, int64_t now)
{
    if (recovery->pending) {
        if (now - recovery->requested_us >= DASH_RECOVERY_GRACE_US)
            recovery->state = DASH_RECOVERY_FAILED;
    } else if (now - recovery->heartbeat_us >= DASH_RECOVERY_STALL_US) {
        dashboard_recovery_request(recovery, now, true);
    }
}

void dashboard_recovery_begin(dashboard_recovery_t *recovery, int64_t now)
{
    recovery->state = DASH_RECOVERY_RESTARTING;
    recovery->heartbeat_us = now;
}

void dashboard_recovery_finish(dashboard_recovery_t *recovery, int64_t now)
{
    recovery->pending = false;
    recovery->state = DASH_RECOVERY_RUNNING;
    recovery->heartbeat_us = recovery->restarted_us = now;
    ++recovery->restarts;
}

bool dashboard_recovery_group_request(dashboard_recovery_t *workers, size_t count,
                                      dashboard_recovery_t *summary, int64_t now)
{
    /* Validate on copies first: a refused request must not restart only a subset. */
    for (size_t i = 0; i < count; ++i) {
        dashboard_recovery_t copy = workers[i];
        if (!dashboard_recovery_request(&copy, now, false)) return false;
    }
    if (!dashboard_recovery_request(summary, now, false)) return false;
    for (size_t i = 0; i < count; ++i) dashboard_recovery_request(&workers[i], now, false);
    return true;
}

void dashboard_recovery_group_update(const dashboard_recovery_t *workers, size_t count,
                                     dashboard_recovery_t *summary, int64_t now)
{
    bool pending = false, failed = false, restarting = false, automatic = false;
    for (size_t i = 0; i < count; ++i) {
        pending |= workers[i].pending;
        failed |= workers[i].state == DASH_RECOVERY_FAILED;
        restarting |= workers[i].state == DASH_RECOVERY_RESTARTING;
        automatic |= workers[i].pending && workers[i].automatic;
    }
    if (pending && !summary->pending) {
        /* Automatic source recovery must be visible even during UI cooldown. */
        summary->pending = true;
        summary->requested_us = now;
        summary->automatic = automatic;
        ++summary->requests;
    } else if (!pending && summary->pending) {
        dashboard_recovery_finish(summary, now);
    }
    if (pending) summary->state = failed ? DASH_RECOVERY_FAILED
        : restarting ? DASH_RECOVERY_RESTARTING : DASH_RECOVERY_REQUESTED;
}

void dashboard_recovery_group_poll(dashboard_recovery_t *workers, size_t count,
                                   dashboard_recovery_t *summary, int64_t now)
{
    for (size_t i = 0; i < count; ++i) dashboard_recovery_poll(&workers[i], now);
    dashboard_recovery_group_update(workers, count, summary, now);
}
