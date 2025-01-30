#include <Ticker.h>

class Timer {
public:
    enum TimerType { SCHEDULER, ONESHOT };

    Timer(unsigned long interval, TimerType type, void (*task)()) 
        : intervalMs(interval), timerType(type), callback(task), running(false) {}

    void start() {
        if (running || !callback) return;

        if (timerType == SCHEDULER) {
            ticker.attach_ms(intervalMs, callback);
        } else {
            ticker.once_ms(intervalMs, [this]() {
                if (callback) callback();
                stop();
            });
        }
        running = true;
    }

    void stop() {
        if (!running) return;
        ticker.detach();
        running = false;
    }

    void restart() {
        stop();
        start();
    }

    void setType(TimerType type) {
        timerType = type;
        if (running) restart();
    }

    TimerType getType() const { return timerType; }

    void setInterval(unsigned long interval) {
        intervalMs = interval;
        if (running) restart();
    }

    unsigned long getInterval() const { return intervalMs; }

    bool isRunning() const { return running; }

private:
    Ticker ticker;
    unsigned long intervalMs;
    TimerType timerType;
    void (*callback)();
    bool running;
};
