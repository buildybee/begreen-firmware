#include "MCP7940_Scheduler.h"
#include <NTPClient.h>
#include <WiFiUdp.h>


// NTP client setup
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, NTP_SERVER);

MCP7940Scheduler::MCP7940Scheduler() 
    : timezoneOffset(5.5) {}

void MCP7940Scheduler::begin() {
  rtc.begin();
  while (!rtc.deviceStatus())  // Turn oscillator on if necessary
  {
    Serial.println(F("Oscillator is off, turning it on."));
    bool deviceStatus = rtc.deviceStart();  // Start oscillator and return new state
    if (!deviceStatus) {
      Serial.println(F("Oscillator did not start, trying again."));
      delay(1000);
    }                // of if-then oscillator didn't start
  }           
  if (updateTimeFromNTP()) {
      Serial.println("Time updated from NTP.");
  } else {
      Serial.println("Failed to update time from NTP.");
  }
}

void MCP7940Scheduler::setTimeZone(float timezoneOffsetHours) {
    timezoneOffset = timezoneOffsetHours;
}

float MCP7940Scheduler::getTimeZone() {
    return timezoneOffset;
}

bool MCP7940Scheduler::updateTimeFromNTP() {
    timeClient.begin();
    if (timeClient.forceUpdate()) {
        time_t ntpTime = timeClient.getEpochTime();
        if (timezoneOffset > 0) {
          rtc.adjust(DateTime(ntpTime + uint16_t(timezoneOffset * 3600)));
        }
        return true;
    }
    return false;
}

bool MCP7940Scheduler::setWateringSchedule( WateringSchedule* ws) {
    const uint8_t ramAddress = 0x00;  // Example starting address for RAM
    return rtc.writeRAM(ramAddress, *ws);
}

// Get a watering schedule
bool MCP7940Scheduler::getWateringSchedule(WateringSchedule* ws) {
    const uint8_t ramAddress = 0; // Starting address for RAM (same as used in set_schedule)
    if (rtc.readRAM(ramAddress, *ws) > 0) {
        return true; // Successfully read schedule
    }
    return false; // Failed to read schedule
}

bool MCP7940Scheduler::alarmTriggered(ALARM alarm) {
    switch (alarm) {
        case ONTRIGGER:
            return rtc.isAlarm(0);  // Check if Alarm 0 is triggered
        case OFFTRIGGER:
            return rtc.isAlarm(1);  // Check if Alarm 1 is triggered
        default:
            return false;  // Invalid alarm
    }
}


String MCP7940Scheduler::getCurrentTimestamp() {
    DateTime now = rtc.now();
    char buffer[20];
    snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d",
             now.year(), now.month(), now.day(),
             now.hour(), now.minute(), now.second());
    return String(buffer);
}

bool MCP7940Scheduler::setNextAlarm(bool autoNextInetrval) {
    DateTime now = rtc.now();  // Get the current time
    WateringSchedule currentSchedule;
    getWateringSchedule(&currentSchedule);
    DateTime nextAlarmTime;

    uint16_t startSeconds = currentSchedule.hour * 3600 + currentSchedule.minute * 60;
    uint16_t nowSeconds = now.hour() * 3600 + now.minute() * 60 + now.second();

    if (autoNextInetrval) {
        // Calculate the next alarm based on the interval
        uint16_t intervalSeconds = currentSchedule.interval_minute * 60;
        uint16_t elapsedSinceStart = nowSeconds - startSeconds;

        if (elapsedSinceStart < intervalSeconds) {
            // Schedule the next alarm today within the interval
            nextAlarmTime = now + TimeSpan(0, 0, (intervalSeconds - elapsedSinceStart) / 60,
                                           (intervalSeconds - elapsedSinceStart) % 60);
        } else {
            // Schedule the next alarm at the interval boundary
            uint16_t remainingSeconds = intervalSeconds - (elapsedSinceStart % intervalSeconds);
            nextAlarmTime = now + TimeSpan(0, 0, remainingSeconds / 60, remainingSeconds % 60);
        }
    } else {
        // Calculate the initial scheduled time
        if (nowSeconds < startSeconds) {
            // Schedule for later today
            nextAlarmTime = DateTime(now.year(), now.month(), now.day(),
                                      currentSchedule.hour, currentSchedule.minute, 0);
        } else {
            // Schedule for tomorrow
            nextAlarmTime = DateTime(now + TimeSpan(1, 0, 0, 0));  // Add 1 day
            nextAlarmTime = DateTime(nextAlarmTime.year(), nextAlarmTime.month(), nextAlarmTime.day(),
                                      currentSchedule.hour, currentSchedule.minute, 0);
        }
    }

    rtc.clearAlarm(ALARM::ONTRIGGER);         // Clear any existing alarms
    rtc.clearAlarm(ALARM::OFFTRIGGER);        // Clear any existing alarms

    // Set Alarm 0 for the calculated next watering time
    if (!rtc.setAlarm(ALARM::ONTRIGGER, ALARM_TYPE, nextAlarmTime, true)) {
        Serial.println("Failed to set Alarm 0.");
        return false;
    }

    // Set Alarm 1 for the duration after Alarm 0
    DateTime secondAlarmTime = nextAlarmTime + TimeSpan(0, 0, currentSchedule.duration_sec / 60,
                                                        currentSchedule.duration_sec % 60);
    if (!rtc.setAlarm(ALARM::OFFTRIGGER, ALARM_TYPE, secondAlarmTime, true)) {
        Serial.println("Failed to set Alarm 1.");
        return false;
    }

    return true;
}


void MCP7940Scheduler::getAlarms(DateTime &onAlarm, DateTime &offAlarm) {
    onAlarm = rtc.getAlarm(0,ALARM_TYPE);  // Retrieve Alarm 0 time
    offAlarm = rtc.getAlarm(1,ALARM_TYPE);  // Retrieve Alarm 1 time
}


