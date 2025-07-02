# BeeGreen Watering System: MQTT API Documentation

This document outlines the MQTT topics and data formats required to communicate with the BeeGreen watering device.

### MQTT Broker Details
* **Server:** `[Your MQTT Server Address]`
* **Port:** `[Your MQTT Port]`
* **Username:** `[Your MQTT Username]`
* **Password:** `[Your MQTT Password]`

---
## Device Subscriptions (App → Device)
These are the topics the app must **publish** to in order to control the device.

### 1. Manual Pump Control
* **Topic:** `beegreen/pump_trigger`
* **Action:** Manually starts or stops the pump.
* **Payload Format:** A plain string containing an integer.
    * **`0`**: Stops the pump immediately.
    * **`> 0`**: Starts the pump and sets a timer to automatically stop it after the specified number of seconds.
* **Field Data Type:**
    * `duration`: `integer`
* **Examples:**
    * To start the pump for 5 minutes (300 seconds), send payload: `300`
    * To stop the pump, send payload: `0`

### 2. Set or Update a Schedule
* **Topic:** `beegreen/set_schedule`
* **Action:** Creates or modifies one of the 10 available schedule slots (indexed 0-9).
* **Payload Format:** A colon-delimited string: `index:hour:minute:duration:daysOfWeek:enabled`
* **Field Data Types:**
    * `index`: `integer` (Range: 0-9)
    * `hour`: `integer` (Range: 0-23)
    * `minute`: `integer` (Range: 0-59)
    * `duration`: `integer` (Watering time in seconds)
    * `daysOfWeek`: `integer` (Range: 0-127, see Appendix for calculation)
    * `enabled`: `integer` (Use `1` for enabled, `0` for disabled)
* **Example:** To set schedule #1 to run at 8:30 PM for 90 seconds, every day: `"1:20:30:90:127:1"`

### 3. Request All Schedules
* **Topic:** `beegreen/request_schedules`
* **Action:** Asks the device to publish its complete list of all 10 configured schedules.
* **Payload Format:** Can be empty. The device only acts on receiving a message on this topic.

### 4. Request Firmware Update Check
* **Topic:** `beegreen/firmware_upgrade`
* **Action:** Tells the device to check for a new firmware version.
* **Payload Format:** A plain string containing the integer `1`.

---
## Device Publications (Device → App)
These are the topics the app should **subscribe** to in order to receive status and data from the device.

### 1. Pump Status
* **Topic:** `beegreen/pump_status`
* **Action:** Published whenever the pump's state changes (on/off).
* **Payload Format:** JSON object.
* **Field Data Types:**
    * `payload`: `string` (Value is either "on" or "off")
    * `timestamp`: `string` (Format: "YYYY-MM-DD HH:MM:SS")
* **Example:** `{"payload":"on", "timestamp":"2025-07-02 21:14:03"}`

### 2. Next Due Schedule (Retained)
* **Topic:** `beegreen/next_schedule_due`
* **Action:** A **retained** message that always shows the timestamp of the next scheduled watering event. An empty payload means no schedules are set.
* **Payload Format:** A plain string timestamp.
* **Field Data Type:** `string` (Format: "YYYY-MM-DD HH:MM:SS")
* **Example:** `"2025-07-02 21:30:00"`

### 3. List of All Schedules
* **Topic:** `beegreen/get_schedules_response`
* **Action:** The device's response after a request is made to `beegreen/request_schedules`.
* **Payload Format:** A JSON array of 10 schedule objects.
* **Object Field Data Types:**
    * `index`: `integer`
    * `hour`: `integer`
    * `min`: `integer`
    * `dur`: `integer` (seconds)
    * `dow`: `integer` (bitmask value)
    * `en`: `boolean` (true/false)
* **Example:**
    ```json
    [
      {"index":0,"hour":8,"min":30,"dur":60,"dow":127,"en":true},
      {"index":1,"hour":20,"min":30,"dur":90,"dow":31,"en":false},
      ...
      {"index":9,"hour":0,"min":0,"dur":0,"dow":0,"en":false}
    ]
    ```

### 4. Device Heartbeat
* **Topic:** `beegreen/heartbeat`
* **Action:** A periodic message indicating the device is online and its current firmware version.
* **Payload Format:** Plain string.
* **Field Data Type:** `string`
* **Example:** `"1.2.4"`

---
## Appendix: Days of Week Bitmask
The `daysOfWeek` value is an integer calculated by adding the values of the days you want the schedule to run on.

| Day       | Value |
| :-------- | :---- |
| Sunday    | 1     |
| Monday    | 2     |
| Tuesday   | 4     |
| Wednesday | 8     |
| Thursday  | 16    |
| Friday    | 32    |
| Saturday  | 64    |

**Examples:**
* **Weekends (Sat + Sun):** 64 + 1 = `65`
* **Weekdays (Mon-Fri):** 2 + 4 + 8 + 16 + 32 = `62`
* **Every Day:** 1 + 2 + 4 + 8 + 16 + 32 + 64 = `127`

### Advanced Example: Excluding a Day
To exclude a specific day, simply subtract its value from the "Every Day" value of **127**.

For instance, to set a schedule that runs every day **except Tuesday**:
1.  Start with the value for Every Day: `127`
2.  Subtract the value for Tuesday: `4`
3.  The final `daysOfWeek` value is `123`.

**Example Payload:** To set schedule #3 to run at 7:00 AM for 2 minutes (120 seconds) every day except Tuesday, you would publish:
`"3:07:00:120:123:1"`