#include "rtc_pump_schedule.h"

RTCPumpScheduler::RTCPumpScheduler() {
    // Initialize any required variables here.
}

bool RTCPumpScheduler::start() {
    return rtc.begin();
}

bool RTCPumpScheduler::setSchedule(uint8_t day, WateringSchedule * schedule) {
  uint8_t readStartAddress = (day-1)*8;
  return rtc.writeRAM(readStartAddress,schedule->morning_hour) && \
  rtc.writeRAM(readStartAddress+1,schedule->morning_minute) && \
  rtc.writeRAM(readStartAddress+2,schedule->morning_duration_minutes) && \
  rtc.writeRAM(readStartAddress+3,schedule->morning_duration_seconds) && \
  rtc.writeRAM(readStartAddress+4,schedule->evening_hour) && \
  rtc.writeRAM(readStartAddress+5,schedule->evening_minute) && \
  rtc.writeRAM(readStartAddress+6,schedule->evening_duration_minutes) && \
  rtc.writeRAM(readStartAddress+7,schedule->evening_duration_seconds);
}

void RTCPumpScheduler::getSchedule(uint8_t day, WateringSchedule * schedule) {
  uint8_t dataBuffer[8];
  uint8_t x = rtc.readRAM((day-1)*8, dataBuffer);
  schedule->morning_hour = dataBuffer[0];
  schedule->morning_minute = dataBuffer[1];
  schedule->morning_duration_minutes = dataBuffer[2];
  schedule->morning_duration_seconds = dataBuffer[3];
  schedule->evening_hour = dataBuffer[4];
  schedule->evening_minute = dataBuffer[5];
  schedule->evening_duration_minutes = dataBuffer[6];
  schedule->evening_duration_seconds = dataBuffer[7];
}

bool RTCPumpScheduler::setNextAlarm(bool nextDay = false) {
    DateTime currentTime = rtc.now(); // Get the current time
    uint8_t dow = currentTime.dayOfTheWeek();
    // Adjust dow if invalid or earlier than the current day
    if (nextDay) {
      // If no valid alarm found for today, move to the next day
      dow = (dow == 7) ? 1 : dow + 1;
    }

    WateringSchedule * alarmSchedule;
    RTCPumpScheduler::getSchedule(dow-1, alarmSchedule);

    // Create DateTime objects for morning and evening alarms
    DateTime morningAlarmDateTime = DateTime(
        currentTime.year(),
        currentTime.month(),
        currentTime.day(),
        alarmSchedule->morning_hour,
        alarmSchedule->morning_minute,
        0
    );

    DateTime eveningAlarmDateTime = DateTime(
        currentTime.year(),
        currentTime.month(),
        currentTime.day(),
        alarmSchedule->evening_hour,
        alarmSchedule->evening_minute,
        0
    );

    // Check if morning alarm is in the future
    if ((morningAlarmDateTime - currentTime).totalseconds() > 0) {
        TimeSpan morningAlarmDuration = TimeSpan(0,0,alarmSchedule->morning_duration_minutes,alarmSchedule->morning_duration_seconds);
        DateTime morningAlarmEnd = morningAlarmDateTime + morningAlarmDuration; // Add TimeSpan to DateTime
        if (RTCPumpScheduler::resetAlarm(START_ALARM) && RTCPumpScheduler::resetAlarm(STOP_ALARM)){
          return false;
        }
        return rtc.setAlarm(START_ALARM, ALARM_TYPE, morningAlarmDateTime) && rtc.setAlarm(STOP_ALARM, ALARM_TYPE, morningAlarmEnd);
    }

    // Check if evening alarm is in the future
    if ((eveningAlarmDateTime - currentTime).totalseconds() > 0) {
        TimeSpan eveningAlarmDuration = TimeSpan(0,0,alarmSchedule->evening_duration_minutes,alarmSchedule->evening_duration_seconds);
        DateTime eveningAlarmEnd = eveningAlarmDateTime + eveningAlarmDuration; // Add TimeSpan to DateTime
        if (RTCPumpScheduler::resetAlarm(START_ALARM) && RTCPumpScheduler::resetAlarm(STOP_ALARM)){
          return false;
        }
        return rtc.setAlarm(START_ALARM, ALARM_TYPE,eveningAlarmDateTime) && rtc.setAlarm(STOP_ALARM, ALARM_TYPE, eveningAlarmEnd);
    }
  return RTCPumpScheduler::setNextAlarm(true); // Recursive call for the next day
}

bool RTCPumpScheduler::handlePowerFailure(uint8_t thresholdHours = 2) {
  TimeSpan someDelay = 2;
  DateTime powerDown = rtc.getPowerDown();
  DateTime powerUp = rtc.getPowerUp();

  if (!rtc.getAlarmState(0) and !rtc.getAlarmState(1)){
    Serial.println("Water schedule not missed");
    return false;
  }
  TimeSpan pumpStarted = rtc.getAlarm(START_ALARM, ALARM_TYPE) - powerDown;   //  pumpStarted>0 means didnt even start , pumpStarted<0 powerloss after pump started.

  if (rtc.getAlarmState(0) and !rtc.getAlarmState(1)){  //when power restores before timer stop : 1. pump didnt even start 2. powerloss after pump started
    
    TimeSpan diff = powerUp - powerDown;  //default considering power loss in between pump watering
    if (pumpStarted.totalseconds() > 0) {  // pumpStarted>0 means didnt even start
      diff =  powerUp - rtc.getAlarm(START_ALARM, ALARM_TYPE); //compoensate from start time to power up 
    }
    diff = diff+ someDelay;
    DateTime endAlarmTime = rtc.getAlarm(STOP_ALARM, ALARM_TYPE)+ diff; //stop alarm get extended;
    if (RTCPumpScheduler::resetAlarm(START_ALARM) && RTCPumpScheduler::resetAlarm(STOP_ALARM)){
      return false;
    }
    return rtc.setAlarm(START_ALARM,ALARM_TYPE,rtc.now()+someDelay) && rtc.setAlarm(STOP_ALARM,ALARM_TYPE,endAlarmTime);
  } else if (rtc.getAlarmState(0) and rtc.getAlarmState(1)) { //case  1. power loss during the entire watering period 2. powerloss after pump started and power restores past end time
     TimeSpan diff = rtc.getAlarm(STOP_ALARM, ALARM_TYPE) - rtc.getAlarm(START_ALARM, ALARM_TYPE); //consider entire period is missed;
     if (pumpStarted.totalseconds() < 0) {  // pumpStarted<0 powerloss after pump started.
      diff =  rtc.getAlarm(STOP_ALARM, ALARM_TYPE) - powerDown; //compoensate from start time to power up 
    }
    diff = diff+ someDelay;

    // if powerUp - STOP_ALARM  > threshold: do not correct or add to next schedule
    
    if (RTCPumpScheduler::resetAlarm(START_ALARM) && RTCPumpScheduler::resetAlarm(STOP_ALARM)){
      return false;
    }
      return rtc.setAlarm(START_ALARM,ALARM_TYPE,rtc.now()+someDelay) && rtc.setAlarm(STOP_ALARM,ALARM_TYPE,rtc.now()+diff);
  }
  return false;
}

bool RTCPumpScheduler::startWatering() {
  return rtc.isAlarm(0) && !rtc.isAlarm(1);
}

bool RTCPumpScheduler::stopWatering() {
  return rtc.isAlarm(0) && rtc.isAlarm(1);
}

bool RTCPumpScheduler::resetAlarm(uint8_t alarm) {
  return rtc.clearAlarm (alarm) && 
  rtc.setAlarmState(alarm,false);
}
