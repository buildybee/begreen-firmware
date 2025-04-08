#ifndef MCP7940_SCHEDULER_H
#define MCP7940_SCHEDULER_H

#include <Arduino.h>
#include "MCP7940.h"

// NTP Server (Hardcoded)
#define NTP_SERVER "pool.ntp.org"

// Structure for storing watering schedules
struct WateringSchedule {
    uint8_t hour;              // Watering start hour
    uint8_t minute;            // Watering start minute
    uint16_t duration_sec;     // Duration of watering in seconds
    uint16_t interval_minute;  // Interval between watering in minutes
};

// Enumeration for Alarm State
enum ALARM {
    ONTRIGGER,
    OFFTRIGGER
};

class MCP7940Scheduler {
public:
    MCP7940Scheduler();

    // Initialize the RTC and set initial time if available from NTP
    void begin();

    // Set time zone
    void setTimeZone(float timezoneOffsetHours);

    // Get the current time zone
    float getTimeZone();

    // Update time from NTP (called at startup or once a week)
    bool updateTimeFromNTP();

    // Get the current date and time as a string
    String getCurrentTimestamp();

    // Set a watering schedule
    bool setWateringSchedule(WateringSchedule* ws);

    // Get a watering schedule
    bool getWateringSchedule(WateringSchedule* ws);

    // Check if a specific alarm is triggered
    bool alarmTriggered(ALARM alarm);

    // Set next watering schedule
    bool setNextAlarm(bool autoNextInetrval = true);

    // Get the current alarms (returns both Alarm 0 and Alarm 1)
    void getAlarms(DateTime &alarm0, DateTime &alarm1);


private:
  MCP7940_Class rtc;
  float timezoneOffset;     // Time zone offset in hours
  uint8_t ALARM_TYPE = 7;
};

#endif // MCP7940_SCHEDULER_H