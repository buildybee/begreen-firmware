#include "MCP7940_Scheduler.h"
#include <NTPClient.h>
#include <WiFiUdp.h>


// NTP client setup
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, NTP_SERVER);

MCP7940Scheduler::MCP7940Scheduler() 
    : missedThreshold(30), timezoneOffset(5.5) {}

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
        } else {
          rtc.adjust(DateTime(ntpTime - uint16_t(timezoneOffset * 3600)));
        }
        return true;
    }
    return false;
}

bool MCP7940Scheduler::setWateringSchedule(const WateringSchedule& schedule) {
    currentSchedule = schedule;

    // Set the initial alarm
    DateTime now = rtc.now();
    DateTime alarmTime(now.year(), now.month(), now.day(),
                       schedule.hour, schedule.minute, 0);

    if (alarmTime.unixtime() <= now.unixtime()) {
        alarmTime = alarmTime + TimeSpan(1, 0, 0, 0);  // Move to the next day
    }
    return setAlarm0(alarmTime);
}

bool MCP7940Scheduler::alarmTriggered(ALARM alarm) {

    return false;
}

void MCP7940Scheduler::handleAlarm() {

}

void MCP7940Scheduler::setMissedThreshold(uint16_t threshold) {
    missedThreshold = threshold;
}

uint16_t MCP7940Scheduler::getMissedThreshold() {
    return missedThreshold;
}

bool MCP7940Scheduler::saveConfiguration() {

    return false;
}

bool MCP7940Scheduler::loadConfiguration() {

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

uint32_t MCP7940Scheduler::calculateMissedTime() {
    
    return 0;
}

bool MCP7940Scheduler::setAlarm0(const DateTime& alarmTime) {

    return false;
}

bool MCP7940Scheduler::setAlarm1(const DateTime& alarmTime) {

    return false;
}

void MCP7940Scheduler::writeToRAM(uint8_t address, const uint8_t* data, uint8_t length) {

}

void MCP7940Scheduler::readFromRAM(uint8_t address, uint8_t* data, uint8_t length) {

}
