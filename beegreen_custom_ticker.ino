#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiManager.h>        // https://github.com/tzapu/WiFiManager
#include <PubSubClient.h>       // https://pubsubclient.knolleary.net/api
#include <INA219.h>             // https://github.com/RobTillaart/INA219
#include <Adafruit_NeoPixel.h>  // https://github.com/adafruit/Adafruit_NeoPixel
#include <Button2.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <FS.h>                  
#include <LittleFS.h>            // Use LittleFS instead of SPIFFS
#include <ArduinoJson.h> 

//custom header
#include "Timer.h"
#include "objects.h"
#include "MCP7940_Scheduler.h"
#include "helper.h"

WiFiManager wm;

BearSSL::WiFiClientSecure espClient;
PubSubClient mqttClient(espClient);
MCP7940Scheduler rtc;
INA219 ina219(INA219_ADDR);
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


char mqtt_server[60] = "";
uint16_t mqtt_port;
char mqtt_user[32] = "";
char mqtt_password[32] = "";

// Flag for saving data
bool shouldSaveConfig = false;

// Callback notifying us of the need to save config
void saveConfigCallback() {
  Serial.println("Should save config");
  shouldSaveConfig = true;
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
      LittleFS.remove("/config.json");
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


void setupWiFi() {
  WiFi.mode(WIFI_STA);  // explicitly set mode, esp defaults to STA+AP
  wm.setWiFiAutoReconnect(true);
  wm.setConfigPortalBlocking(false);
  
  //automatically connect using saved credentials if they exist
  //If connection fails it starts an access point with the specified name
  if (wm.autoConnect("AutoConnectAP")) {
    Serial.println("connected...yeey :)");
    deviceState.radioStatus = ConnectivityStatus::LOCALCONNECTED;
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
          Serial.printf("OTA Update Failed. Error code: %d\n", ret);
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
      if (mqttClient.connect("beegreen", mqtt_user, mqtt_password)) {
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

Timer heartBeat(60000,Timer::SCHEDULER,[]() {
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



void setup() {
  firmwareUpdate = false;
  mqttloop = true;
  firmwareUpdateOngoing = false;
  Serial.begin(115200);
  
  Serial.println();

  // Mount LittleFS
  Serial.println("Mounting FS...");
  if (!LittleFS.begin()) {
    Serial.println("Failed to mount FS");
  } else {
    Serial.println("Mounted file system");                          
    if (LittleFS.exists("/config.json")) {  
      Serial.println("Reading config file");
      File configFile = LittleFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("Opened config file");
        
        // Parse JSON
        DynamicJsonDocument json(256);
        DeserializationError error = deserializeJson(json, configFile);
        if (!error) {
          Serial.println("Parsed JSON");

          // Assign values from JSON to variables
          String mqttServer = json["s"].as<String>();
          String mqttPort = json["p"].as<String>();
          String mqttUser = json["u"].as<String>();
          String mqttPassword = json["pw"].as<String>();

          // Convert String to char[]
          mqttServer.toCharArray(mqtt_server, sizeof(mqtt_server));
          mqtt_port = static_cast<uint16_t>(atoi(mqttPort.c_str()));
          mqttUser.toCharArray(mqtt_user, sizeof(mqtt_user));
          mqttPassword.toCharArray(mqtt_password, sizeof(mqtt_password));

          Serial.println("mqtt_server from JSON: " + mqttServer);
          Serial.println("mqtt_port from JSON: " + mqttPort);
          Serial.println("mqtt_user from JSON: " + mqttUser);
          Serial.println("mqtt_password from JSON: " + mqttPassword);
        } else {
          Serial.println("Failed to parse JSON config");
        }
        configFile.close();
      } else {
        Serial.println("Failed to open config file");
      }
    }
  }

  // Set up WiFiManager parameters
  WiFiManagerParameter custom_mqtt_server("mqtt_server", "MQTT Server", "", 60);
  WiFiManagerParameter custom_mqtt_port("mqtt_port", "MQTT Port", "", 4);
  WiFiManagerParameter custom_mqtt_username("username", "Username", "", 32);
  WiFiManagerParameter custom_mqtt_password("password", "Password", "", 32);

  wm.setSaveConfigCallback(saveConfigCallback);

  wm.addParameter(&custom_mqtt_server);
  wm.addParameter(&custom_mqtt_port);
  wm.addParameter(&custom_mqtt_username);
  wm.addParameter(&custom_mqtt_password);

  if (!wm.autoConnect("AutoConnectAP")) {
    Serial.println("Failed to connect, restarting...");
    delay(3000);
    ESP.restart();
  }

  Serial.println("Connected to WiFi!");

  // Save configuration to LittleFS if needed
  if (shouldSaveConfig) {
    Serial.println("Saving config...");
    DynamicJsonDocument json(256);
    json["s"] = custom_mqtt_server.getValue();
    json["p"] = custom_mqtt_port.getValue();
    json["u"] = custom_mqtt_username.getValue();
    json["pw"] = custom_mqtt_password.getValue();

    File configFile = LittleFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("Failed to open config file for writing");
    } else {
      serializeJson(json, configFile);
      configFile.close();
      Serial.println("Config saved");
      ESP.restart();
    }
  }

  Serial.println("Local IP:");
  Serial.println(WiFi.localIP());

  Wire.begin(SDA_PIN, SCL_PIN);

  pinMode(LED_PIN, OUTPUT);
  led.begin();
  led.clear();
  
  pinMode(MOSFET_PIN, OUTPUT);
  digitalWrite(MOSFET_PIN, LOW); // Turn off mosfet
  
  button.begin(BUTTON_PIN, INPUT, false);
  button.setDebounceTime(DEBOUNCE_DELAY);
  button.setLongClickTime(LONG_CLICK_WINDOW);
  button.setLongClickDetectedHandler(buttonHandler);  // this will only be called upon detection

  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), buttonISR, FALLING); 

  espClient.setInsecure();
  setupWiFi();
  delay(100);
  Serial.println("port value that wioll be entered");
  Serial.print(mqtt_port);
  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setCallback(mqttCallback);
  delay(100);

  rtc.begin();
  heartBeat.start();
  setLedColor.start();
  alarmHandler.start();
  loopMqtt.start();
}

void loop() {
  wm.process();
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