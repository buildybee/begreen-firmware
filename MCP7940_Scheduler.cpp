#include "MCP7940_Scheduler.h"
#include <NTPClient.h>
#include <WiFiUdp.h>

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, NTP_SERVER);

MCP7940Scheduler::MCP7940Scheduler() : timezoneOffset(5.5) {}

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
        time_t ntpTime = timeClient.getEpochTime() + static_cast<uint16_t>(timezoneOffset * 3600);
        rtc.adjust(DateTime(ntpTime));
        return true;
    }
    return false;
}

bool MCP7940Scheduler::setWateringSchedule(WateringSchedule* ws) {
    deleteSchedule();
    return rtc.writeRAM(0x00, *ws);
}

bool MCP7940Scheduler::getWateringSchedule(WateringSchedule* ws) {
    return rtc.readRAM(0x00, *ws) > 0;
}

bool MCP7940Scheduler::alarmTriggered(ALARM alarm) {
    return rtc.isAlarm(static_cast<uint8_t>(alarm));
}

String MCP7940Scheduler::getCurrentTimestamp() {
    DateTime now = rtc.now();
    char buffer[20];
    snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d",
             now.year(), now.month(), now.day(),
             now.hour(), now.minute(), now.second());
    return String(buffer);
}


bool MCP7940Scheduler::setNextAlarm(bool autoNextInterval) {
    DateTime now = rtc.now();
    WateringSchedule currentSchedule;
    getWateringSchedule(&currentSchedule);
    if (currentSchedule.duration_sec == 0) {
        return false;
    }

    DateTime nextAlarmTime;
    uint16_t startSeconds = currentSchedule.hour * 3600 + currentSchedule.minute * 60;
    uint16_t nowSeconds = now.hour() * 3600 + now.minute() * 60 + now.second();

    if (autoNextInterval) {
        uint16_t intervalSeconds = currentSchedule.interval_minute * 60;
        uint16_t elapsedSinceStart = nowSeconds - startSeconds;
        uint16_t remainingSeconds = intervalSeconds - (elapsedSinceStart % intervalSeconds);
        nextAlarmTime = now + TimeSpan(0, 0, remainingSeconds / 60, remainingSeconds % 60);
    } else {
        nextAlarmTime = (nowSeconds < startSeconds)
                            ? DateTime(now.year(), now.month(), now.day(), currentSchedule.hour, currentSchedule.minute, 0)
                            : DateTime(now + TimeSpan(1, 0, 0, 0));
    }

    rtc.clearAlarm(ALARM::ONTRIGGER);
    rtc.clearAlarm(ALARM::OFFTRIGGER);

    if (!rtc.setAlarm(ALARM::ONTRIGGER, ALARM_TYPE, nextAlarmTime, true)) {
        Serial.println("Failed to set Alarm 0.");
        return false;
    }
    DateTime secondAlarmTime = nextAlarmTime + TimeSpan(0, 0, currentSchedule.duration_sec / 60, currentSchedule.duration_sec % 60);
    return rtc.setAlarm(ALARM::OFFTRIGGER, ALARM_TYPE, secondAlarmTime, true);
}

void MCP7940Scheduler::getAlarms(DateTime &onAlarm, DateTime &offAlarm) {
    onAlarm = rtc.getAlarm(0, ALARM_TYPE);
    offAlarm = rtc.getAlarm(1, ALARM_TYPE);
}


bool MCP7940Scheduler::deleteAlarm(){
    rtc.setAlarmState(ONTRIGGER,false);
    rtc.setAlarmState(OFFTRIGGER,false);
    return rtc.clearAlarm(ONTRIGGER) && rtc.clearAlarm(OFFTRIGGER);
}

void MCP7940Scheduler::deleteSchedule(){
    struct WateringSchedule *ws = {0};
    setWateringSchedule(ws);
}
