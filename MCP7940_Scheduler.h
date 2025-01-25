#ifndef MCP7940_SCHEDULER_H
#define MCP7940_SCHEDULER_H

#include <Arduino.h>
#include <MCP7940.h>

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

    // Set a watering schedule
    bool setWateringSchedule(const WateringSchedule& schedule);

    // Check if a specific alarm is triggered
    bool alarmTriggered(ALARM alarm);

    // Handle alarms: set the next interval and adjust for missed periods
    void handleAlarm();

    // Set the missed time threshold (in seconds)
    void setMissedThreshold(uint16_t threshold);

    // Get the missed time threshold
    uint16_t getMissedThreshold();

    // Save the configuration to RTC RAM
    bool saveConfiguration();

    // Load the configuration from RTC RAM
    bool loadConfiguration();

    // Get the current date and time as a string
    String getCurrentTimestamp();

private:
    MCP7940_Class rtc;
    WateringSchedule currentSchedule;

    uint16_t missedThreshold;  // Maximum missed time threshold in seconds
    float timezoneOffset;     // Time zone offset in hours

    // Helper to calculate the missed duration
    uint32_t calculateMissedTime();

    // Set RTC alarms
    bool setAlarm0(const DateTime& alarmTime);
    bool setAlarm1(const DateTime& alarmTime);

    // Read and write configuration parameters to RTC RAM
    void writeToRAM(uint8_t address, const uint8_t* data, uint8_t length);
    void readFromRAM(uint8_t address, uint8_t* data, uint8_t length);
};

#endif // MCP7940_SCHEDULER_H