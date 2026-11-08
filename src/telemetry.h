#pragma once

#include <Arduino.h>
#include <FS.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

class Telemetry
{
public:
    using ValueCallback = float (*)(const void *context);

    static constexpr uint16_t PORT = 4210;
    static constexpr size_t MAX_SIGNALS = 24;

    Telemetry();

    bool add(const char *name, ValueCallback callback, const void *context = nullptr);

    template <typename T>
    bool add(const char *name, const T *value)
    {
        if (!value)
            return add(name, static_cast<ValueCallback>(nullptr), nullptr);

        return add(
            name,
            [](const void *context) -> float
            {
                return static_cast<float>(*static_cast<const T *>(context));
            },
            value);
    }

    // Samples every loopsPerSample calls to update(): 1 = every call, 2 = every other call.
    bool setSampleIntervalLoops(uint32_t loopsPerSample);
    bool begin();
    void update(bool started, bool enabled, uint32_t nowMs);

    bool connected() const;
    void setRecordingState(bool active);
    void setRunState(bool active);

    bool clear();
    bool hasSignal(const char *name) const;
    size_t signalCount() const;
    const String &signalName(size_t index) const;
    File openRead() const;
    const String &status() const;
    const String &lastSessionStatsJson() const;

private:
    struct Signal
    {
        String name;
        ValueCallback callback = nullptr;
        const void *context = nullptr;
    };

    struct Sample
    {
        uint32_t sequence;
        uint32_t elapsedMs;
        uint32_t recordingState;
        uint32_t runState;
        uint8_t count;
        float values[MAX_SIGNALS];
    };

    static constexpr size_t QUEUE_LENGTH = 32;
    static constexpr size_t MAX_BATCH_SAMPLES = 10;
    static constexpr size_t FRAME_HEADER_SIZE = 20;
    static constexpr uint32_t BATCH_WINDOW_MS = 50;

    static void taskEntry(void *context);
    void run();
    bool enqueue(uint32_t elapsedMs, const float *values, size_t count);
    void setError(const char *message);

    Signal _signals[MAX_SIGNALS];
    size_t _signalCount;
    uint32_t _loopsPerSample;
    uint32_t _loopsSinceSample;
    uint32_t _runStartedAt;
    bool _wasStarted;
    bool _recordingActive;
    String _status;

    WiFiServer _server;
    QueueHandle_t _queue;
    TaskHandle_t _taskHandle;
    volatile bool _clientConnected;
    volatile uint32_t _droppedSamples;
    uint32_t _nextSequence;
    volatile uint32_t _recordingState;
    volatile uint32_t _runState;
    bool _started;
};
