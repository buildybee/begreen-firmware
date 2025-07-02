#include "MCP7940_Scheduler.h"
#include <NTPClient.h>
#include <WiFiUdp.h>

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, NTP_SERVER);

MCP7940Scheduler::MCP7940Scheduler() : timezoneOffset(5.5) , _nextDueAlarm(0){}

void MCP7940Scheduler::begin() {
    rtc.begin();
    while (!rtc.deviceStatus()) {
        Serial.println(F("Oscillator is off, turning it on."));
        if (!rtc.deviceStart()) {
            Serial.println(F("Oscillator did not start, trying again."));
            delay(1000);
        }
    }
    if (updateTimeFromNTP()) {
        Serial.println("Time updated from NTP.");
    } else {
        Serial.println("Failed to update time from NTP.");
    }
}

void MCP7940Scheduler::setTimeZone(float tzOffset) {
    timezoneOffset = tzOffset;
}

float MCP7940Scheduler::getTimeZone() { return timezoneOffset; }

bool MCP7940Scheduler::updateTimeFromNTP() {
    timeClient.begin();
    if (timeClient.forceUpdate()) {
        time_t ntpTime = timeClient.getEpochTime() + static_cast<uint32_t>(timezoneOffset * 3600);
        rtc.adjust(DateTime(ntpTime));
        return true;
    }
    return false;
}

String MCP7940Scheduler::getCurrentTimestamp() {
    DateTime now = rtc.now();
    char buffer[20];
    snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d",
             now.year(), now.month(), now.day(),
             now.hour(), now.minute(), now.second());
    return String(buffer);
}


bool MCP7940Scheduler::setSchedules(const WateringSchedules& schedules) {
    return rtc.writeRAM(0, schedules);
}

bool MCP7940Scheduler::getSchedules(WateringSchedules& schedules) {
    return rtc.readRAM(0, schedules) > 0;
}

bool MCP7940Scheduler::setManualStopTime(uint16_t duration_sec) {
    // This function implements the "Hardware Override" for a manual run.
    // It directly programs the OFFTRIGGER without touching saved schedules.
    DateTime now = rtc.now();
    DateTime offAlarmTime = now + TimeSpan(duration_sec);

    // ALARM_TYPE 4 matches on H, M, S for a precise one-time stop
    if (!rtc.setAlarm(ALARM::OFFTRIGGER, 4, offAlarmTime, true)) {
        Serial.println("Failed to set manual OFFTRIGGER alarm.");
        return false;
    }
    Serial.println("Manual stop time set.");
    return true;
}

bool MCP7940Scheduler::setNextAlarm() {
    DateTime now = rtc.now();
    WateringSchedules allSchedules;
    if (!getSchedules(allSchedules)) {
        Serial.println("Could not read schedules from RTC RAM. Initializing blank schedule set.");
        memset(&allSchedules, 0, sizeof(allSchedules));
    }

    DateTime earliestNextAlarm(0);
    uint16_t durationForNextAlarm = 0;

    for (int i = 0; i < MAX_SCHEDULES; ++i) {
        const auto& schedule = allSchedules.items[i];
        if (!schedule.enabled || schedule.daysOfWeek == 0) {
            continue;
        }

        for (int dayOffset = 0; dayOffset < 7; ++dayOffset) {
            DateTime checkDate = now + TimeSpan(dayOffset, 0, 0, 0);
            uint8_t dayOfWeek = checkDate.dayOfTheWeek();
            
            if ((schedule.daysOfWeek >> (dayOfWeek - 1)) & 1) {
                DateTime potentialAlarmTime(checkDate.year(), checkDate.month(), checkDate.day(),
                                            schedule.hour, schedule.minute, 0);

                if (potentialAlarmTime.unixtime() > now.unixtime()) {
                    if (earliestNextAlarm.unixtime() == 0 || potentialAlarmTime.unixtime() < earliestNextAlarm.unixtime()) {
                        earliestNextAlarm = potentialAlarmTime;
                        durationForNextAlarm = schedule.duration_sec;
                    }
                    break; 
                }
            }
        }
    }

    rtc.clearAlarm(ALARM::ONTRIGGER);
    rtc.clearAlarm(ALARM::OFFTRIGGER);
    rtc.setAlarmState(ALARM::ONTRIGGER, false);
    rtc.setAlarmState(ALARM::OFFTRIGGER, false);

    _nextDueAlarm = earliestNextAlarm; // Store the result

    if (earliestNextAlarm.unixtime() > 0) {
        Serial.printf("Next alarm set for: %04d-%02d-%02d %02d:%02d\n",
             earliestNextAlarm.year(), earliestNextAlarm.month(), earliestNextAlarm.day(),
             earliestNextAlarm.hour(), earliestNextAlarm.minute());

        if (!rtc.setAlarm(ALARM::ONTRIGGER, ALARM_TYPE, earliestNextAlarm, true)) {
            Serial.println("Failed to set ONTRIGGER alarm.");
            return false;
        }
        
        DateTime offAlarmTime = earliestNextAlarm + TimeSpan(durationForNextAlarm);
        if (!rtc.setAlarm(ALARM::OFFTRIGGER, ALARM_TYPE, offAlarmTime, true)) {
            Serial.println("Failed to set OFFTRIGGER alarm.");
            rtc.setAlarmState(ALARM::ONTRIGGER, false);
            return false;
        }
        return true;
    } 
    
    Serial.println("No future alarms to set.");
    return false;
}

bool MCP7940Scheduler::alarmTriggered(ALARM alarm) {
    return rtc.isAlarm(static_cast<uint8_t>(alarm));
}

void MCP7940Scheduler::getAlarms(DateTime &onAlarm, DateTime &offAlarm) {
    uint8_t alarmType;
    onAlarm = rtc.getAlarm(0, alarmType);
    offAlarm = rtc.getAlarm(1, alarmType);
}

DateTime MCP7940Scheduler::getNextDueAlarm() const {
    return _nextDueAlarm;
}