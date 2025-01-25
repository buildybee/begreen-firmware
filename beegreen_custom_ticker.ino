#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiManager.h>        // https://github.com/tzapu/WiFiManager
#include <PubSubClient.h>       // https://pubsubclient.knolleary.net/api
#include <INA219.h>             // https://github.com/RobTillaart/INA219
#include <Adafruit_NeoPixel.h>  // https://github.com/adafruit/Adafruit_NeoPixel
#include <Button2.h>

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
bool pumpOnTrigger = false;
bool pumpOffTrigger = false;
volatile bool buttonPressed = false;
volatile unsigned long lastPressTime = 0; // Timestamp of the last button press
volatile bool doubleClickDetected = false;

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
    pumpOffTrigger = true;
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
  for (int i = 0; i < length; i++) {
    Serial.println((char)payload[i]);
  }
  if (strcmp(topic, PUMP_CONTROL_TOPIC) == 0) {
   if (atoi((char *)payload)==1){
    pumpOnTrigger = true;
   } else {pumpOffTrigger = true;}
  } else {
    Serial.print("Topic action not found");
  }
}

void buttonHandler(Button2& btn) {
    Serial.print("In Button handler Instance: ");
    switch (btn.getType()) {
      case double_click:
        Serial.println("Double click");
        pumpOffTrigger = true;
        break;
      case long_click:
        Serial.println("Long click");
        pumpOnTrigger = !deviceState.pumpRunning;
        break;
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
  pumpOnTrigger = false;
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
  pumpOnTrigger = false;
  pumpOffTrigger = false;
  if (digitalRead(MOSFET_PIN)) {
    Serial.println("Stopping pump");
    digitalWrite(MOSFET_PIN, LOW);
    deviceState.pumpRunning = false;
    publishMsg(PUMP_STATUS_TOPIC, "off");
    return;
  }
  Serial.println("Pump already in idle state");
}



Timer heartBeat(5000,Timer::SCHEDULER,[]() {
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

Timer watering(500,Timer::SCHEDULER,[](){
  if (pumpOffTrigger) {
    pumpStop();
  } else if (pumpOnTrigger) {
    pumpStart();
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
    watering.start();
}

void loop() {
  wm.process();
  button.loop();
  if (buttonPressed) {
    handleButtonPress();
  }
  if (WiFi.status() == WL_CONNECTED){
    deviceState.radioStatus = ConnectivityStatus::LOCALCONNECTED;
    if (mqttClient.connected()) {
      deviceState.radioStatus = ConnectivityStatus::SERVERCONNECTED;
      mqttClient.loop();
      mqttClient.subscribe(PUMP_CONTROL_TOPIC);
    } else {
      Serial.println("reconnect mqtt");
      if (!mqttClient.connect("beegreen", custom_mqtt_user.getValue(), custom_mqtt_pass.getValue())) {
        deviceState.radioStatus = ConnectivityStatus::SERVERNOTCONNECTED;
      }
    }
  } else {deviceState.radioStatus = ConnectivityStatus::LOCALNOTCONNECTED;}
}
