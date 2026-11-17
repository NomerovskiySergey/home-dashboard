#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "dashboard_data_parse.h"

static void test_weather(void)
{
    const char negative[] =
        "{\"current_units\":{\"temperature_2m\":\"°C\"},"
        "\"current\":{\"temperature_2m\":-2.1}}";
    const char zero[] =
        "{\"current\":{\"temperature_2m\":0},\"current_units\":{\"temperature_2m\":\"°C\"}}\r\n";
    const char fahrenheit[] =
        "{\"current\":{\"temperature_2m\":32},\"current_units\":{\"temperature_2m\":\"°F\"}}";
    const char wrong_type[] =
        "{\"current\":{\"temperature_2m\":\"0\"},\"current_units\":{\"temperature_2m\":\"°C\"}}";
    const char truncated[] = "{\"current\":";
    int temperature = 0;

    assert(dashboard_parse_weather(negative, strlen(negative), &temperature) && temperature == -21);
    assert(dashboard_parse_weather(zero, strlen(zero), &temperature) && temperature == 0);
    assert(!dashboard_parse_weather(fahrenheit, strlen(fahrenheit), &temperature) && temperature == 0);
    assert(!dashboard_parse_weather(wrong_type, strlen(wrong_type), &temperature));
    assert(!dashboard_parse_weather(truncated, strlen(truncated), &temperature));
}

static void test_alerts(void)
{
    const char all_clear[] = "\"NNNNNNNNNNNNNNNNNNNNNNNNNNN\"";
    const char mixed[] = " \r\n\"RNNPYNNNNNNNNNNNNNNNNNNNNNR\"\r\n";
    const char unknown[] = "\"NNNXNNNNNNNNNNNNNNNNNNNNNNN\"";
    const char short_response[] = "\"NN\"";
    const char error[] = "{\"message\":\"Unauthorized\"}";
    char regions[DASHBOARD_REGION_COUNT] = {0};

    assert(dashboard_parse_alerts(all_clear, strlen(all_clear), regions));
    assert(regions[12] == 'N' && regions[43] == 'N' && regions[74] == 'N');
    assert(dashboard_parse_alerts(mixed, strlen(mixed), regions));
    assert(regions[43] == 'R' && regions[12] == 'P' && regions[14] == 'Y' && regions[74] == 'R');
    assert(!dashboard_parse_alerts(unknown, strlen(unknown), regions) && regions[12] == 'P');
    assert(!dashboard_parse_alerts(short_response, strlen(short_response), regions));
    assert(!dashboard_parse_alerts(error, strlen(error), regions));
}

static void test_energy(void)
{
    const char valid[] =
        "[{\"initiator\":\"dnipropetrovska_oblast_oblenergo\",\"queue\":\"5.2\"},"
        "{\"initiator\":\"dnipropetrovska_oblast_cek\",\"queue\":\"5.1\"},"
        "{\"initiator\":\"dnipropetrovska_oblast_cek\",\"queue\":\"5.2\","
        "\"today_date\":\"2026-11-17\",\"today_status\":\"ScheduleApplies\","
        "\"today\":[2,3,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],"
        "\"today_possible\":[1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],"
        "\"tomorrow_date\":\"2026-11-18\",\"tomorrow_status\":\"EmergencyShutdowns\","
        "\"tomorrow\":[],\"ignored\":{\"detail\":\"brace } inside a string\"}}]";
    const char waiting[] =
        "[{\"initiator\":\"dnipropetrovska_oblast_cek\",\"queue\":\"5.2\","
        "\"today_date\":\"2026-11-17\",\"today_status\":\"NoShutdowns\",\"today\":[],"
        "\"tomorrow_date\":\"2026-11-18\",\"tomorrow_status\":\"WaitingForSchedule\",\"tomorrow\":[]}]";
    const char invalid[] =
        "[{\"initiator\":\"dnipropetrovska_oblast_cek\",\"queue\":\"5.2\","
        "\"today_date\":\"2026-11-17\",\"today_status\":\"ScheduleApplies\",\"today\":[]}]";
    dashboard_energy_schedule_t schedule = {0};

    assert(dashboard_parse_energy(valid, strlen(valid), &schedule));
    assert(strcmp(schedule.today.date, "2026-11-17") == 0);
    assert(schedule.today.state == DASH_ENERGY_PLANNED);
    assert(schedule.today.slots[0] == DASH_SLOT_OFF && schedule.today.slots[1] == DASH_SLOT_POSSIBLE);
    assert(schedule.today.slots[2] == DASH_SLOT_CLEAR && schedule.today.slots[3] == DASH_SLOT_OFF);
    assert(schedule.today.slots[4] == DASH_SLOT_OFF && schedule.today.slots[5] == DASH_SLOT_OFF);
    assert(schedule.tomorrow.state == DASH_ENERGY_EMERGENCY);
    assert(!dashboard_parse_energy(valid, strlen(valid) - 1, &schedule));
    assert(!dashboard_parse_energy(invalid, strlen(invalid), &schedule));
    assert(schedule.today.state == DASH_ENERGY_PLANNED);
    assert(dashboard_parse_energy(waiting, strlen(waiting), &schedule));
    assert(schedule.today.state == DASH_ENERGY_NO_SHUTDOWNS);
    assert(schedule.tomorrow.state == DASH_ENERGY_WAITING);
}

int main(void)
{
    test_weather();
    test_alerts();
    test_energy();
    puts("Dashboard data parser tests passed");
    return 0;
}
