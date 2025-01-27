#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiManager.h>        // https://github.com/tzapu/WiFiManager
#include <PubSubClient.h>       // https://pubsubclient.knolleary.net/api
#include <INA219.h>             // https://github.com/RobTillaart/INA219
#include <Adafruit_NeoPixel.h>  // https://github.com/adafruit/Adafruit_NeoPixel
#include <Button2.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>

//custom header
#include "Timer.h"
#include "objects.h"
#include "MCP7940_Scheduler.h"

WiFiManager wm;
WiFiManagerParameter custom_mqtt_server("server", "mqtt server", "fdafe7462f45431cbfe28e5fde0e41e0.s1.eu.hivemq.cloud", 60);
WiFiManagerParameter custom_mqtt_port("port", "mqtt port", "8883", 6);
WiFiManagerParameter custom_mqtt_user("mqtt_user", "Username", "buildybee", 32);
WiFiManagerParameter custom_mqtt_pass("mqtt_user", "Password", "Buildybee123", 32);

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


void gracefullShutownprep(){
  mqttClient.disconnect();
  wm.disconnect();
  pumpStop();
  if (pendingAlarmUpdate){
      rtc.setNextAlarm(false);
      pendingAlarmUpdate = !pendingAlarmUpdate;
  }
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
      // wm.resetSettings();
      Serial.println("Reset mecahnism witheld for testing purpose, change before deploying");
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
  wm.addParameter(&custom_mqtt_server);
  wm.addParameter(&custom_mqtt_port);
  wm.addParameter(&custom_mqtt_user);
  wm.addParameter(&custom_mqtt_pass);
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
    gracefullShutownprep();
    Serial.println("Reset trigger disengaged: Restarting device");
    delay(1000);
    ESP.restart();
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
  if (!digitalRead(MOSFET_PIN)) {
    Serial.println("Starting pump");
    digitalWrite(MOSFET_PIN, HIGH);
    deviceState.pumpRunning = true;
    publishMsg(PUMP_STATUS_TOPIC, "on");
    return;
  } 
  Serial.println("Pump already in running state");
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

// Define the server and paths for OTA
const char* updateCheckURL = "http://192.168.1.12:5000/update?version=1.0.0";
const char* firmwareDownloadURL = "http://192.168.1.12:5000/download";

void checkForOTAUpdate() {
  WiFiClient wifiClient; // Use WiFiClient or WiFiClientSecure for HTTPS
  HTTPClient http;
  mqttClient.disconnect();

  Serial.printf("Free heap before OTA: %d bytes\n", ESP.getFreeHeap());
  Serial.println("Checking for OTA updates...");

  if (http.begin(wifiClient, updateCheckURL)) { 
    Serial.println("Connected to update server...");
    http.setTimeout(5000); // Set 5-second timeout

    int httpCode = http.GET();
    Serial.printf("HTTP GET Response Code: %d\n", httpCode);

    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      Serial.println("Update check payload: " + payload);

      if (payload.indexOf("\"update\":true") >= 0) {
        Serial.println("Update available. Starting OTA...");
        t_httpUpdate_return ret = ESPhttpUpdate.update(wifiClient, firmwareDownloadURL);

        if (ret == HTTP_UPDATE_OK) {
          Serial.println("OTA Update Successful");
        } else {
          Serial.printf("OTA Update Failed. Error code: %d\n", ret);
        }
      } else {
        Serial.println("No update available.");
      }
    } else {
      Serial.printf("Failed to connect to server. HTTP code: %d\n", httpCode);
    }

    http.end();
  } else {
    Serial.println("Unable to connect to OTA update server.");
  }

  Serial.printf("Free heap after OTA: %d bytes\n", ESP.getFreeHeap());
}

Timer heartBeat(60000,Timer::SCHEDULER,[]() {
    if (mqttClient.connected()) {
    mqttClient.publish(HEARBEAT_TOPIC, "1");
  }
});

Timer setLedColor(500,Timer::SCHEDULER,[](){
  if (deviceState.pumpRunning){
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




void setup() {
    Serial.begin(115200);
    Wire.begin(SDA_PIN, SCL_PIN);

    pinMode(LED_PIN, OUTPUT);
    led.begin();
    led.clear();
    
    pinMode(MOSFET_PIN, OUTPUT);
    digitalWrite(MOSFET_PIN, LOW); // Turn off mosfet
    
    button.begin(BUTTON_PIN,INPUT,false);
    button.setDebounceTime(DEBOUNCE_DELAY);
    button.setLongClickTime(LONG_CLICK_WINDOW);
    button.setLongClickDetectedHandler(buttonHandler);  // this will only be called upon detection

    attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), buttonISR, FALLING); 

    espClient.setInsecure();
    setupWiFi();
    delay(100);
    mqttClient.setServer(custom_mqtt_server.getValue(), 8883);
    mqttClient.setCallback(mqttCallback);
    delay(100);

    rtc.begin();
    heartBeat.start();
    setLedColor.start();
    alarmHandler.start();
}

void loop() {
  wm.process();
  button.loop();
  if (buttonPressed) {
    handleButtonPress();
  }

  if (WiFi.status() == WL_CONNECTED){
    deviceState.radioStatus = ConnectivityStatus::LOCALCONNECTED;
    if (!mqttClient.connected()) {
      if (mqttClient.connect("beegreen", custom_mqtt_user.getValue(), custom_mqtt_pass.getValue())) {
        mqttClient.subscribe(PUMP_CONTROL_TOPIC);
        mqttClient.subscribe(SET_SCHEDULE);
        mqttClient.subscribe(REQUEST_NEXT_SCHEDULE);
        deviceState.radioStatus = ConnectivityStatus::SERVERCONNECTED;
      } else { 
        deviceState.radioStatus = ConnectivityStatus::SERVERNOTCONNECTED;
      }
    } else {
      deviceState.radioStatus = ConnectivityStatus::SERVERCONNECTED;
      mqttClient.loop();
    }
  } else {deviceState.radioStatus = ConnectivityStatus::LOCALNOTCONNECTED;}

  checkForOTAUpdate();
}
