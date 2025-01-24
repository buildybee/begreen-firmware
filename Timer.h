#include <Ticker.h>

class Timer {
public:
    // Timer type: Scheduler (repeated) or One-shot (single execution)
    enum TimerType { SCHEDULER, ONESHOT };

    Timer() : running(false), timerType(SCHEDULER), intervalMs(1000), func(nullptr), isrCallback(nullptr) {}

    // Sets the function and interval using function pointers
    void create(void (*function)(), unsigned long interval) {
        func = function;
        intervalMs = interval;
    }

    // Updates the interval
    void setInterval(unsigned long interval) {
        intervalMs = interval;
        if (running) {
            restart(); // Restart with updated interval
        }
    }

    // Sets the timer type
    void setType(TimerType type) {
        timerType = type;
    }

    // Gets the current timer type
    TimerType getType() const {
        return timerType;
    }

    // Sets an interrupt service routine (ISR) callback
    void setInterrupt(void (*isr)()) {
        isrCallback = isr;
    }

    // Clears the ISR callback
    void clearInterrupt() {
        isrCallback = nullptr;
    }

    // Starts the timer
    void start() {
        if (running) {
            Serial.println("Timer is already running.");
            return;
        }
        if (!func) {
            Serial.println("No function set. Use create() to set a function first.");
            return;
        }

        if (timerType == SCHEDULER) {
            ticker.attach_ms(intervalMs, [this]() {
                func();
                if (isrCallback) isrCallback();
            });
        } else if (timerType == ONESHOT) {
            ticker.once_ms(intervalMs, [this]() {
                func();
                if (isrCallback) isrCallback();
                stop(); // Stop automatically after one execution
            });
        }

        running = true;
    }

    // Stops the timer
    void stop() {
        if (!running) {
            Serial.println("Timer is already stopped.");
            return;
        }
        ticker.detach();
        running = false;
    }

    // Checks if the timer is running
    bool isRunning() const {
        return running;
    }

private:
    void restart() {
        stop();
        start(); // Restart with the currently set function
    }

    Ticker ticker;
    bool running;
    TimerType timerType;
    unsigned long intervalMs;
    void (*func)();        // Pointer to the main function
    void (*isrCallback)(); // Pointer to the ISR
};
