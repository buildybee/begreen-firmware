#ifndef Objects_h
#define Objects_h

// mqtt topics
#define LED_TOPIC "beegreen/status"
#define SENSOR_TOPIC "beegreen/sensor"
#define PUMP_CONTROL_TOPIC "beegreen/pump_trigger"
#define HEARBEAT_TOPIC "beegreen/heartbeat"
#define SET_SCHEDULE "beegreen/set_schedule"
#define PUMP_STATUS_TOPIC "beegreen/pump_status"
// I2C Pins
#define SDA_PIN 5
#define SCL_PIN 4

// I/O constants
#define BUTTON_PIN 14
#define MOSFET_PIN 12  // Drives the pump
#define LED_PIN 13     // prod will be 13
#define NUM_LEDS 1

// opertational constants
#define LED_BRIGHTNESS 100
#define POWER_CONSUMPTION_THRESHOLD 50
#define PING_INTERVAL 6000
#define INA219_ADDR 0x40

#define DEBOUNCE_DELAY 20        // Debounce delay in milliseconds
#define DOUBLE_CLICK_WINDOW 500  // Maximum time between clicks for a double-click in milliseconds
#define LONG_CLICK_WINDOW 2500

enum ConnectivityStatus {
  LOCALCONNECTED,
  LOCALNOTCONNECTED,
  SERVERCONNECTED,
  SERVERNOTCONNECTED,
};

// Enum for RGB LED colors
enum LedColor {
    RED = 0xFF0000,         // Red
    GREEN = 0x00FF00, // Green
    YELLOW = 0xFFFF00, // Yellow
    BLUE = 0x0000FF, // Blue
    OFF = 0x000000, // Off
};


struct  State {
  float temp;
  float humidity;
  float currentConsumption = 500.00; //ref value on for testing 
  bool autoMode = 0;
  ConnectivityStatus radioStatus = ConnectivityStatus::LOCALNOTCONNECTED;
  bool pumpRunning = false;
  bool waterTankEmpty = false;
};

struct Hardconfig {
  uint heartbeat = 60;
  uint motorCutoffThreshold = 200;
  uint aht20ReadInterval= 60;
};



#endif