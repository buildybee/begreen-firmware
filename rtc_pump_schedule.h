#ifndef RTC_PUMP_SCHEDULE_H
#define RTC_PUMP_SCHEDULE_H

#include "MCP7940.h"

// Structure to hold watering schedule for each day of the week
// 8 byte required
// Total size 64byte
// Need 7 days, monrnig/evening schedule : 7 * 8 = 56byte
// left 8 byte
struct WateringSchedule {
    uint8_t morning_hour;
    uint8_t morning_minute;
    uint8_t morning_duration_minutes;
    uint8_t morning_duration_seconds;
    uint8_t evening_hour;
    uint8_t evening_minute;
    uint8_t evening_duration_minutes;
    uint8_t evening_duration_seconds;
};

class RTCPumpScheduler {
public:
    RTCPumpScheduler();

    bool start(); // Replaces setup()
    bool setSchedule(uint8_t day, WateringSchedule * schedule);
    void getSchedule(uint8_t day , WateringSchedule * schedule);
    bool setNextAlarm(bool nextDay);
    bool handlePowerFailure(uint8_t thresholdHours);
    bool startWatering();
    bool stopWatering();
    bool stopAlarm(uint8_t alarm);

private:
    MCP7940_Class rtc;
    bool resetAlarm(uint8_t alarm);
    static WateringSchedule schedules[7]; // Array for schedules, one for each day of the week
    const uint8_t START_ALARM = 0;
    const uint8_t STOP_ALARM = 1; 
    uint8_t ALARM_TYPE = 7;  //exact date time
};

#endif
