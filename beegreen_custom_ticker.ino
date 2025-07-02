#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiManager.h>        // https://github.com/tzapu/WiFiManager
#include <PubSubClient.h>       // https://pubsubclient.knolleary.net/api 
#include <Adafruit_NeoPixel.h>  // https://github.com/adafruit/Adafruit_NeoPixel
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>               
#include <EEPROM.h>            // Use LittleFS instead of SPIFFS
#include <ArduinoJson.h>
#include <INA219.h>
#include <DoubleResetDetect.h>

//custom header
#include "Timer.h"
#include "objects.h"
#include "MCP7940_Scheduler.h"
#include "helper.h"

BearSSL::WiFiClientSecure espClient;
PubSubClient mqttClient(espClient);
MCP7940Scheduler rtc;
Adafruit_NeoPixel led(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);
LedColor ledColorPicker[2] = {LedColor::RED,LedColor::OFF};
State deviceState;
INA219 INA(INA219_I2C_ADDR);
DoubleResetDetect drd(DRD_TIMEOUT, DRD_ADDRESS);

bool picker = false;
bool resetTrigger = false;
bool pendingAlarmUpdate= false;
bool mqttloop,firmwareUpdate,firmwareUpdateOngoing;
float current  = 0;
volatile unsigned long lastClickTime = 0;
volatile uint8_t clickCount = 0;
unsigned long lastButtonCheckTime = 0;


WiFiManager wm;
MqttCredentials mqttDetails;
// Set up WiFiManager parameters
WiFiManagerParameter custom_mqtt_server("mqtt_server", "MQTT Server", "", 60);
WiFiManagerParameter custom_mqtt_port("mqtt_port", "MQTT Port", "", 4);
WiFiManagerParameter custom_mqtt_username("username", "Username", "", 32);
WiFiManagerParameter custom_mqtt_password("password", "Password", "", 32);


// Flag for saving data
bool shouldSaveConfig = false;

// Callback notifying us of the need to save config
void saveConfigCallback() {
  Serial.println("Should save config");

  strcpy(mqttDetails.mqtt_server,custom_mqtt_server.getValue());
  strcpy(mqttDetails.mqtt_user,custom_mqtt_username.getValue());
  strcpy(mqttDetails.mqtt_password,custom_mqtt_password.getValue());
  mqttDetails.mqtt_port = static_cast<uint16_t>(atoi(custom_mqtt_port.getValue()));

  Serial.println("Value of mqttDetails param that wiil be saved");
  Serial.println(mqttDetails.mqtt_server);
  Serial.println(mqttDetails.mqtt_user);
  Serial.println(mqttDetails.mqtt_password);
  Serial.println(mqttDetails.mqtt_port);
  Serial.println("****");

  Serial.println("Saving config...");
  eeprom_saveconfig();
  ESP.restart();
}

void configPotrtalTimeoutCalback() {
  if (!digitalRead(MOSFET_PIN)) {
    wm.reboot();
  }
}

void gracefullShutownprep(){
  mqttClient.disconnect();
  wm.disconnect();
  pumpStop();
  if (pendingAlarmUpdate){
      rtc.setNextAlarm();
      pendingAlarmUpdate = !pendingAlarmUpdate;
  }
  led.setPixelColor(0,LedColor::OFF);
  led.show();
}

// ISR for button press

// Interrupt handler (IRAM_ATTR for ESP8266)
void IRAM_ATTR buttonISR() {
  static unsigned long lastDebounceTime = 0;
  unsigned long now = millis();

  // Debounce check
  if (now - lastDebounceTime < DEBOUNCE_DELAY) return;
  lastDebounceTime = now;

  // Register click
  if (now - lastClickTime < DOUBLE_CLICK_WINDOW) {
    clickCount++; // Increment if within double-click window
  } else {
    clickCount = 1; // Reset if too slow
  }
  lastClickTime = now;
}

// Method to generate the string in the format "prefix_chipID_last4mac"
String generateDeviceID() {
  // Get the chip ID
  String chipID = String(WIFI_getChipId(),HEX);
  chipID.toUpperCase();
  // Get the MAC address
  String macAddress = WiFi.macAddress();
  macAddress.replace(":", ""); // Remove colons from the MAC address
  // Extract the last 4 characters of the MAC address
  String last4Mac = macAddress.substring(macAddress.length() - 4);
  // Combine the prefix, chip ID, and last 4 MAC characters
  String deviceID = String("BeeGreen") + "_" + chipID + "_" + last4Mac;
  return deviceID;
}

void setupWiFi() {
  // WiFi.mode(WIFI_STA);  // explicitly set mode, esp defaults to STA+AP
  wm.setConfigPortalBlocking(false);
  wm.setConnectTimeout(20);
  wm.setWiFiAutoReconnect(true);
  wm.setConfigPortalTimeout(120);
  wm.setConfigPortalTimeoutCallback(configPotrtalTimeoutCalback);
  wm.addParameter(&custom_mqtt_server);
  wm.addParameter(&custom_mqtt_port);
  wm.addParameter(&custom_mqtt_username);
  wm.addParameter(&custom_mqtt_password);
  wm.setSaveConfigCallback(saveConfigCallback);
  
  //automatically connect using saved credentials if they exist
  //If connection fails it starts an access point with the specified name

  if (wm.autoConnect(generateDeviceID().c_str())) {
    Serial.println("WiFi connected...yeey :)");
    deviceState.radioStatus = ConnectivityStatus::LOCALCONNECTED;
    checkForOTAUpdate();
    
  } else {
    Serial.println("Configportal running");
    deviceState.radioStatus = ConnectivityStatus::LOCALNOTCONNECTED;
  }

}

void mqttCallback(char *topic, byte *payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");

  char payloadStr[length + 1];
  strncpy(payloadStr, (char *)payload, length);
  payloadStr[length] = '\0';
  Serial.println(payloadStr);

  if (strcmp(topic, PUMP_CONTROL_TOPIC) == 0) {
    int duration = atoi(payloadStr);

    if (duration == 0) {
      // If payload is "0", stop the pump.
      pumpStop();
    } else if (duration > 0) {
      // If payload is a positive number, start the pump with that duration.
      pumpStart();
      rtc.setManualStopTime(duration);
    }
  } else if (strcmp(topic, SET_SCHEDULE) == 0) {
    onSetScheduleCallback(payloadStr);
    // Only apply the changes immediately if the pump is not running.
    if (!digitalRead(MOSFET_PIN)) {
      updateAndPublishNextAlarm();
    }
  } else if (strcmp(topic, REQUEST_ALL_SCHEDULES) == 0) {
    WateringSchedules allSchedules;
    rtc.getSchedules(allSchedules);

    DynamicJsonDocument doc(1024); // Sufficient size for 10 schedules
    JsonArray scheduleArray = doc.to<JsonArray>();

    for (int i = 0; i < MAX_SCHEDULES; i++) {
      JsonObject scheduleObj = scheduleArray.createNestedObject();
      scheduleObj["index"] = i;
      scheduleObj["hour"] = allSchedules.items[i].hour;
      scheduleObj["min"] = allSchedules.items[i].minute;
      scheduleObj["dur"] = allSchedules.items[i].duration_sec;
      scheduleObj["dow"] = allSchedules.items[i].daysOfWeek;
      scheduleObj["en"] = allSchedules.items[i].enabled;
    }

    char buffer[1024];
    serializeJson(doc, buffer);
    mqttClient.publish(GET_ALL_SCHEDULES, buffer);

  } else if (strcmp(topic, GET_UPDATE_REQUEST) == 0) {
    if (atoi(payloadStr) == 1) {
      firmwareUpdate = true;
    }
  } else if (strcmp(topic, RESTART) == 0) {
    ESP.restart();
  }
   else {
    Serial.print("Topic action not found");
  }
}

// Callback for SET_SCHEDULE
void onSetScheduleCallback(const char* payload) {
    int index;
    ScheduleItem newItem;
    
    if (parseSchedulePayload(payload, index, newItem)) {
        WateringSchedules allSchedules;
        
        if (!rtc.getSchedules(allSchedules)) {
            Serial.println("Could not read schedules, initializing new set.");
            memset(&allSchedules, 0, sizeof(allSchedules));
        }

        allSchedules.items[index] = newItem;

        if (rtc.setSchedules(allSchedules)) {
            Serial.printf("Schedule at index %d saved successfully.\n", index);
        } else {
            Serial.println("Failed to save schedules to RTC RAM.");
        }
    } else {
        Serial.println("Invalid schedule format. Expected index:HH:MM:duration:daysOfWeek:enabled");
    }
}

// It parses the payload for setting a schedule: "index:HH:MM:duration:daysOfWeek:enabled"
bool parseSchedulePayload(const char* payload, int& index, ScheduleItem& item) {
    int enabled_int;
    int parsed = sscanf(payload, "%d:%2hhu:%2hhu:%hu:%hhu:%d", 
                        &index, &item.hour, &item.minute, 
                        &item.duration_sec, &item.daysOfWeek, &enabled_int);
    
    if (parsed == 6 && index >= 0 && index < MAX_SCHEDULES) {
        item.enabled = (enabled_int == 1);
        return true;
    }
    return false;
}

void publishMsg(const char *topic, const char *payload,bool retained){
  if (mqttClient.connected()) {
      String jsonPayload = "{";
      jsonPayload += "\"payload\":\"";
      jsonPayload += payload;
      jsonPayload += "\",";
      jsonPayload += "\"timestamp\":\"";
      jsonPayload += rtc.getCurrentTimestamp();
      jsonPayload += "\"";
      jsonPayload += "}";

      mqttClient.publish(topic, jsonPayload.c_str(),retained); // Publish the JSON payload
    }
}


void pumpStart(){
  if (!digitalRead(MOSFET_PIN) && (!firmwareUpdate)) {
    Serial.println("Starting pump");
    digitalWrite(MOSFET_PIN, HIGH);
    deviceState.pumpRunning = true;
    if (mqttClient.connected()) {
      publishMsg(PUMP_STATUS_TOPIC, "on",true);
    }
    return;
  } 
  Serial.println("Pump already in running state or upgrade in progress");
}

void pumpStop() {
  if (digitalRead(MOSFET_PIN)) {
    Serial.println("Stopping pump");
    digitalWrite(MOSFET_PIN, LOW);
    deviceState.pumpRunning = false;
    publishMsg(PUMP_STATUS_TOPIC, "off");

    // Always recalculate the next alarm when the pump stops.
    // This correctly resumes the schedule after both manual and automatic stops,
    // and it will apply any schedule updates that were received mid-run.
    // Recalculate and publish the next due time.
    updateAndPublishNextAlarm();
  } else {
    Serial.println("Pump already in idle state");
  }
}

void checkForOTAUpdate() {
  HTTPClient http;
  Serial.println("Checking for OTA updates...");

  String updateURL = String(UPDATEURL) + "?nocache=" + String(millis()); // Force fresh request
  if (http.begin(espClient, updateURL)) { 
    Serial.println("Connected to update server...");
    http.setTimeout(5000); // Set 5-second timeout

    int httpCode = http.GET(); // Perform GET request to fetch the version file
    if (httpCode == HTTP_CODE_OK) {
      String fetchedFirmwareVersionString = http.getString(); // Store the String object
      fetchedFirmwareVersionString.trim(); // Trim whitespace

      Serial.println("Fetched Firmware Version: " + fetchedFirmwareVersionString);
      Serial.println("System Firmware Version: " + String(FIRMWARE_VERSION));
      

      // Compare fetched version with current firmware version
      if (v1GreaterThanV2(fetchedFirmwareVersionString.c_str(),FIRMWARE_VERSION)) {
        firmwareUpdateOngoing = true;
        Serial.println("Update available. Starting OTA...");
        mqttClient.disconnect(); // Ensure MQTT is disconnected during OTA
        String firmwareURL = String(FIRMWAREDOWNLOAD)+ fetchedFirmwareVersionString + ".bin";
        t_httpUpdate_return ret = ESPhttpUpdate.update(espClient, firmwareURL);

        if (ret == HTTP_UPDATE_OK) {
          Serial.println("OTA Update Successful");
          gracefullShutownprep();
          ESP.restart();
        } else {
            Serial.printf("HTTP_UPDATE_FAILED Error (%d): %s\n", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
          firmwareUpdateOngoing = false;
        }
      } else {
        Serial.println("No update available.");
      }
    } else {
      Serial.printf("Failed to fetch update file. HTTP code: %d\n", httpCode);
    }

    http.end(); // End the HTTP connection
  } else {
    Serial.println("Unable to connect to OTA update server.");
  }
  http.end();
}

void connectNetworkStack() {
  if (WiFi.status() == WL_CONNECTED && mqttClient.connected()) {
    deviceState.radioStatus = ConnectivityStatus::SERVERCONNECTED;
    return;
  }

  if (WiFi.status() == WL_CONNECTED && !mqttClient.connected()) {
    if (mqttClient.connect("beegreen", mqttDetails.mqtt_user, mqttDetails.mqtt_password)) {
      mqttClient.subscribe(PUMP_CONTROL_TOPIC);
      mqttClient.subscribe(SET_SCHEDULE);
      mqttClient.subscribe(REQUEST_ALL_SCHEDULES);
      mqttClient.subscribe(GET_UPDATE_REQUEST);
      deviceState.radioStatus = ConnectivityStatus::SERVERCONNECTED;
      return;
    }
    deviceState.radioStatus = ConnectivityStatus::SERVERNOTCONNECTED;
    return;
  }

  if (WiFi.status() != WL_CONNECTED && wm.getConfigPortalActive()) {
     deviceState.radioStatus = ConnectivityStatus::LOCALNOTCONNECTED;
     return;
  }

  if (WiFi.status() != WL_CONNECTED && !wm.getConfigPortalActive() && !digitalRead(MOSFET_PIN)) {
     wm.reboot();
  }
}

void updateAndPublishNextAlarm() {
  // Set the next hardware alarm based on the schedule
  bool alarmWasSet = rtc.setNextAlarm();
  
  if (alarmWasSet) {
    // Get the time that was just set
    DateTime nextAlarm = rtc.getNextDueAlarm();
    
    char buffer[20];
    snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d",
             nextAlarm.year(), nextAlarm.month(), nextAlarm.day(),
             nextAlarm.hour(), nextAlarm.minute(), nextAlarm.second());
             
    // Publish the message with the retain flag set to true
    mqttClient.publish(NEXT_SCHEDULE, buffer, true);
  } else {
    // If no alarms are set, publish an empty string to clear the retained message
    mqttClient.publish(NEXT_SCHEDULE, "", true);
  }
}

Timer heartBeat(HEARTBEAT_TIMER,Timer::SCHEDULER,[]() {
    if (mqttClient.connected()) {
    mqttClient.publish(HEARBEAT_TOPIC, FIRMWARE_VERSION);
  }
});

Timer setLedColor(500,Timer::SCHEDULER,[](){
  if (firmwareUpdateOngoing){
    ledColorPicker[0] = LedColor::MAGENTA;
    ledColorPicker[1] = LedColor::OFF;
  } else if (deviceState.pumpRunning){
    ledColorPicker[0] = LedColor::BLUE;
    ledColorPicker[1] = LedColor::BLUE;
  } else {
    switch (deviceState.radioStatus) {
      case ConnectivityStatus::SERVERCONNECTED:
        ledColorPicker[0] = LedColor::GREEN;
        ledColorPicker[1] = LedColor::GREEN;
        break;
      case (ConnectivityStatus::LOCALNOTCONNECTED):
        ledColorPicker[0] = LedColor::RED;
        ledColorPicker[1] = LedColor::OFF;
        break;
      default:
        ledColorPicker[0] = LedColor::RED;
        ledColorPicker[1] = LedColor::RED;
        break;
    }
  }
  if (deviceState.waterTankEmpty) {
    ledColorPicker[1] = LedColor::BLUE;
  }

  picker = !picker;
  led.setPixelColor(0,ledColorPicker[int(picker)]);
  led.show();
});

Timer alarmHandler(1000, Timer::SCHEDULER, []() {
  if (rtc.alarmTriggered(ALARM::ONTRIGGER) && !digitalRead(MOSFET_PIN)) {
    Serial.println("onAlarm triggered: ");
    pumpStart();
  }

  if (rtc.alarmTriggered(ALARM::OFFTRIGGER) && digitalRead(MOSFET_PIN)) {
    Serial.println("offAlarm triggered: ");
    // pumpStop now correctly handles stopping the pump AND setting the next alarm.
    pumpStop();
  }
});

Timer loopMqtt(5000,Timer::SCHEDULER,[]() {
  mqttloop = true;
});

void eeprom_read() {
  EEPROM.begin(sizeof(mqttDetails) + 10);
  EEPROM.get(EEPROM_START_ADDR, mqttDetails);
  EEPROM.end();
}

void eeprom_saveconfig() {
  EEPROM.begin(sizeof(mqttDetails) + 10);
  EEPROM.put(EEPROM_START_ADDR, mqttDetails);
  if (EEPROM.commit()){
  EEPROM.end();
  } else { Serial.println ("SAving failed"); }
}

void stopServices() {
  mqttClient.disconnect();  // Disconnect MQTT
  // WiFi.disconnect();        // Disconnect WiFi
  // Stop any running timers or tasks
  heartBeat.stop();
  setLedColor.stop();
  alarmHandler.stop();
  loopMqtt.stop();
  // Ensure the pump is stopped
  pumpStop();
}


#ifdef INA219_I2C_ADDR
  Timer currentConsumption(30000, Timer::SCHEDULER,[]() {
    if (INA.isConnected() && digitalRead(MOSFET_PIN)) {
      current  = INA.getCurrent_mA();
      Serial.println(current);
      if (current !=0){
         publishMsg(CURRENT_CONSUMPTION, String(current).c_str(),false);
      }
    }
  });
#endif


void setup() {
  Serial.begin(115200);
  if (drd.detect()) {
    Serial.println("Entering config mode via double reset");
    wm.resetSettings();
    Serial.println("Reset done");
    delay(1000);
    ESP.restart();
  }

  firmwareUpdate = false;
  mqttloop = true;
  firmwareUpdateOngoing = false;
  pinMode(MOSFET_PIN, OUTPUT);
  digitalWrite(MOSFET_PIN, LOW);

  pinMode(LED_PIN, OUTPUT);
  led.begin();
  led.clear();

  Wire.begin(SDA_PIN, SCL_PIN);

  espClient.setInsecure();
  setupWiFi();
  eeprom_read();
  Serial.print("Local IP: ");
  Serial.println(WiFi.localIP());
  Serial.println("Mqtt Details:");
  Serial.printf("- Server: %s\n",mqttDetails.mqtt_server);
  Serial.printf("- Port: %d\n",mqttDetails.mqtt_port);
  Serial.printf("- User: %s\n",mqttDetails.mqtt_user);
  mqttClient.setServer(mqttDetails.mqtt_server, mqttDetails.mqtt_port);
  mqttClient.setCallback(mqttCallback);

  pinMode(LED_PIN, OUTPUT);
  led.begin();
  led.clear();
  
   // Turn off mosfet
  
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), buttonISR, FALLING);

  rtc.begin();

  #ifdef INA219_I2C_ADDR
    if(INA.begin()) {
        INA.setMaxCurrentShunt(MAX_CURRENT, SHUNT);
        currentConsumption.start();
    } else { Serial.println("INA219: Could not connect. Fix and Reboot"); }
  #endif

  heartBeat.start();
  setLedColor.start();
  alarmHandler.start();
  loopMqtt.start();
}

void loop() {
  wm.process();
  // Check WiFi status and attempt reconnection if needed
  
  if (WiFi.status() != WL_CONNECTED && !wm.getConfigPortalActive()) {
    static unsigned long lastWifiAttempt = 0;
    if (millis() - lastWifiAttempt > 30000) { // Every 30 seconds
      lastWifiAttempt = millis();
      WiFi.disconnect();
      WiFi.begin();
    }
  }
  mqttClient.loop();
  
   // Reset clickCount if no follow-up click occurs within CLICK_RESET_TIMEOUT
  if (clickCount > 0 && millis() - lastClickTime >= DOUBLE_CLICK_WINDOW+50) {
    clickCount = 0; // Force reset
  }

  // Handle double-click (only if 2+ clicks within DOUBLE_CLICK_GAP)
  if (clickCount >= 2) {
    clickCount = 0; // Reset after action
    if (!digitalRead(MOSFET_PIN)) pumpStart();
    else pumpStop();
  }

  if(mqttloop) {
    connectNetworkStack();
    mqttloop = false;
  }

  if ((firmwareUpdate) && (!digitalRead(MOSFET_PIN))) {
    checkForOTAUpdate();
    firmwareUpdate = false;
  }

}