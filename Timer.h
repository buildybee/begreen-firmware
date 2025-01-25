#include <Ticker.h>

class Timer {
public:
    enum TimerType { SCHEDULER, ONESHOT };

    Timer(unsigned long interval, TimerType type, void (*task)()) 
        : intervalMs(interval), timerType(type), callback(task), running(false) {}

    // Starts the timer
    void start() {
        if (running || !callback) return;

        if (timerType == SCHEDULER) {
            ticker.attach_ms(intervalMs, callback);
        } else {
            ticker.once_ms(intervalMs, [this]() {
                if (callback) callback();
                stop(); // Stop automatically after execution
            });
        }
        running = true;
    }

    // Stops the timer
    void stop() {
        if (!running) return;
        ticker.detach();
        running = false;
    }

    // Restarts the timer with the same configuration
    void restart() {
        stop();
        start();
    }

    // Sets the timer type
    void setType(TimerType type) {
        timerType = type;
        if (running) restart(); // Apply the new type immediately
    }

    // Gets the current timer type
    TimerType getType() const {
        return timerType;
    }

    // Updates the timer interval
    void setInterval(unsigned long interval) {
        intervalMs = interval;
        if (running) restart(); // Apply the new interval immediately
    }

    // Gets the current timer interval
    unsigned long getInterval() const {
        return intervalMs;
    }

    // Checks if the timer is running
    bool isRunning() const {
        return running;
    }

private:
    Ticker ticker;
    unsigned long intervalMs;
    TimerType timerType;
    void (*callback)();
    bool running;
};
