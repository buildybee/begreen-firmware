#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiManager.h>        // https://github.com/tzapu/WiFiManager
#include <PubSubClient.h>       // https://pubsubclient.knolleary.net/api 
#include <Adafruit_NeoPixel.h>  // https://github.com/adafruit/Adafruit_NeoPixel
#include <Button2.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>               
#include <EEPROM.h>            // Use LittleFS instead of SPIFFS
#include <ArduinoJson.h>

//custom header
#include "Timer.h"
#include "objects.h"
#include "MCP7940_Scheduler.h"
#include "helper.h"

BearSSL::WiFiClientSecure espClient;
PubSubClient mqttClient(espClient);
MCP7940Scheduler rtc;
Button2 button;
Adafruit_NeoPixel led(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);
LedColor ledColorPicker[2] = {LedColor::RED,LedColor::OFF};
bool picker = false;
State deviceState;
bool resetTrigger = false;
bool pendingAlarmUpdate= false;
volatile bool buttonPressed = false;
volatile unsigned long lastPressTime = 0; // Timestamp of the last button press
volatile bool doubleClickDetected = false;
bool mqttloop,firmwareUpdate,firmwareUpdateOngoing;



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

void gracefullShutownprep(){
  mqttClient.disconnect();
  wm.disconnect();
  pumpStop();
  if (pendingAlarmUpdate){
      rtc.setNextAlarm(false);
      pendingAlarmUpdate = !pendingAlarmUpdate;
  }
  led.setPixelColor(0,LedColor::OFF);
  led.show();
}

// ISR for button press
void IRAM_ATTR buttonISR() {
  static unsigned long lastISRTime = 0;
  unsigned long currentISRTime = millis();

  // Debounce logic
  if ((currentISRTime - lastISRTime) > DEBOUNCE_DELAY) {
    if ((currentISRTime - lastPressTime) <= DOUBLE_CLICK_WINDOW) {
      doubleClickDetected = true;  // Double click detected
    } else {
      doubleClickDetected = false; // Reset double click detection
    }
    lastPressTime = currentISRTime;
    buttonPressed = true;
  }
  lastISRTime = currentISRTime;
}

// Function to process button events
void handleButtonPress() {
  if (doubleClickDetected) {
    Serial.println("Double-click detected");
    if (resetTrigger){
      gracefullShutownprep();
      wm.resetSettings();
      Serial.println("Reset done");
      ESP.restart();
    }
    if (!digitalRead(MOSFET_PIN)){
      pumpStart();
    } else{
      pumpStop();
    }
    doubleClickDetected = false;
  }
  buttonPressed = false;
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
  wm.setConfigPortalTimeout(180); // 3 minutes timeout for config portal
  wm.setWiFiAutoReconnect(true);
  wm.setConfigPortalBlocking(false);
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
  // Create a null-terminated string from the payload
  char payloadStr[length + 1];
  strncpy(payloadStr, (char *)payload, length);
  payloadStr[length] = '\0';
  Serial.println(payloadStr);

  if (strcmp(topic, PUMP_CONTROL_TOPIC) == 0) {
    if (atoi(payloadStr)==1){
      pumpStart();
    } else {
      pumpStop();
    }
  } else if (strcmp(topic, SET_SCHEDULE) == 0) {
    onSetScheduleCallback(payloadStr);
    if (digitalRead(MOSFET_PIN)){
      pendingAlarmUpdate = true;
    } else {rtc.setNextAlarm(false);}
  } else if (strcmp(topic, REQUEST_NEXT_SCHEDULE) == 0) {
    DateTime onAlarm,offAlarm;
    WateringSchedule ws;
    rtc.getWateringSchedule(&ws);
    rtc.getAlarms(onAlarm,offAlarm);
    char buffer[40];
    snprintf(buffer,40,"%02d/%02d %02d:%02d,%d:%d",
             onAlarm.day(), onAlarm.month(), onAlarm.hour(), onAlarm.minute(), 
             ws.duration_sec,ws.interval_minute);
    mqttClient.publish(GET_NEXT_SCHEDULE,buffer);
  } else if (strcmp(topic, GET_UPDATE_REQUEST) == 0) {
    if (atoi(payloadStr)==1){
      firmwareUpdate = true;
    }
  } else {
    Serial.print("Topic action not found");
  }
}

// Callback for SET_SCHEDULE
void  onSetScheduleCallback(const char* payload) {
  WateringSchedule ws;
  if (parseSchedulePayload(payload, ws)) {
      if (rtc.setWateringSchedule(&ws)) {
          Serial.println("Schedule saved successfully.");
      } else {
          Serial.println("Failed to save schedule to RAM.");
      }
  } else {
      Serial.println("Invalid schedule format. Expected HH:MM:duration_sec:interval_min");
  }
}

// Function to parse payload in the format "HH:MM:duration_sec:interval_sec" into a WateringSchedule struct
bool parseSchedulePayload(const char* payload, WateringSchedule& ws) {
  int parsed = sscanf(payload, "%2hhu:%2hhu:%4hu:%5hu", &ws.hour, &ws.minute, &ws.duration_sec, &ws.interval_minute);
  if (parsed == 4) {
      return true;
  }
  return false;
}

void buttonHandler(Button2& btn) {
  Serial.print("In Button handler Instance: ");
  if (btn.getType() == long_click)
  Serial.println("Long click");
  resetTrigger = !resetTrigger;
  if (resetTrigger){
    Serial.println("Reset trigger engaged: Double click to reset");
  } else {
    Serial.println("Reset trigger disengaged");
  }
}

void publishMsg(const char *topic, const char *payload){
if (mqttClient.connected()) {
    String jsonPayload = "{";
    jsonPayload += "\"payload\":\"";
    jsonPayload += payload;
    jsonPayload += "\",";
    jsonPayload += "\"timestamp\":\"";
    jsonPayload += rtc.getCurrentTimestamp();
    jsonPayload += "\"";
    jsonPayload += "}";

    mqttClient.publish(topic, jsonPayload.c_str()); // Publish the JSON payload
  }
}

void pumpStart(){
  if (!digitalRead(MOSFET_PIN) && (!firmwareUpdate)) {
    Serial.println("Starting pump");
    digitalWrite(MOSFET_PIN, HIGH);
    deviceState.pumpRunning = true;
    publishMsg(PUMP_STATUS_TOPIC, "on");
    return;
  } 
  Serial.println("Pump already in running state or upgrade in prgress");
}

void pumpStop(){
  if (digitalRead(MOSFET_PIN)) {
    Serial.println("Stopping pump");
    digitalWrite(MOSFET_PIN, LOW);
    deviceState.pumpRunning = false;
    publishMsg(PUMP_STATUS_TOPIC, "off");
    if (pendingAlarmUpdate){
      rtc.setNextAlarm(false);
      pendingAlarmUpdate = !pendingAlarmUpdate;
    }
    return;
  }
  Serial.println("Pump already in idle state");
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

void mqtt() {
  if (WiFi.status() == WL_CONNECTED){
    if (!mqttClient.connected()) {
      if (mqttClient.connect("beegreen", mqttDetails.mqtt_user, mqttDetails.mqtt_password)) {
        mqttClient.subscribe(PUMP_CONTROL_TOPIC);
        mqttClient.subscribe(SET_SCHEDULE);
        mqttClient.subscribe(REQUEST_NEXT_SCHEDULE);
        mqttClient.subscribe(GET_UPDATE_REQUEST);
        deviceState.radioStatus = ConnectivityStatus::SERVERCONNECTED;
      } else { 
        deviceState.radioStatus = ConnectivityStatus::SERVERNOTCONNECTED;
      }
    } else {
      deviceState.radioStatus = ConnectivityStatus::SERVERCONNECTED;
    }
  } else { 
    deviceState.radioStatus = ConnectivityStatus::LOCALNOTCONNECTED; 
  }

}

Timer heartBeat(HEARTBEAT_TIMER,Timer::SCHEDULER,[]() {
    if (mqttClient.connected()) {
    mqttClient.publish(HEARBEAT_TOPIC, FIRMWARE_VERSION);
  }
});

Timer setLedColor(500,Timer::SCHEDULER,[](){
  if (resetTrigger || firmwareUpdateOngoing){
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
    pumpStop();
    rtc.setNextAlarm();
  }
});

Timer loopMqtt(5000,Timer::SCHEDULER,[]() {
  mqttloop = true;
});

void eeprom_read() {
  EEPROM.begin(sizeof(mqttDetails) + 10);
  EEPROM.get(10, mqttDetails);
  EEPROM.end();
}

void eeprom_saveconfig() {
  EEPROM.begin(sizeof(mqttDetails) + 10);
  EEPROM.put(10, mqttDetails);
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

void setup() {

  firmwareUpdate = false;
  mqttloop = true;
  firmwareUpdateOngoing = false;
  Serial.begin(115200);
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
  
  button.begin(BUTTON_PIN, INPUT, false);
  button.setDebounceTime(DEBOUNCE_DELAY);
  button.setLongClickTime(LONG_CLICK_WINDOW);
  button.setLongClickDetectedHandler(buttonHandler);  // this will only be called upon detection

  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), buttonISR, FALLING);

  rtc.begin();
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

  button.loop();
  mqttClient.loop();
  if (buttonPressed) {
    handleButtonPress();
  }

  if(mqttloop) {
    mqtt();
    mqttloop = false;
  }

  if ((firmwareUpdate) && (!digitalRead(MOSFET_PIN))) {
    checkForOTAUpdate();
    firmwareUpdate = false;
  }

}