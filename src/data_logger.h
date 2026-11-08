#pragma once

#include <Arduino.h>

class DataLogger
{
public:
    static constexpr size_t MAX_SIGNALS = 24;

    struct StreamFrame
    {
        enum class Type : uint8_t
        {
            SessionStart,
            Sample,
            SessionStop
        };

        Type type = Type::Sample;
        char sessionId[17] = {};
        uint32_t sequence = 0;
        uint32_t droppedSamples = 0;
        uint64_t elapsedMs = 0;
        uint8_t signalCount = 0;
        float values[MAX_SIGNALS] = {};
    };

    using ValueCallback = float (*)(const void *context);
    using StreamCallback = bool (*)(const StreamFrame &frame, void *context);

    DataLogger();

    // Register signals before begin(); pointers and callback contexts must remain valid.
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

    bool setSampleIntervalMs(uint32_t intervalMs);
    void setMaxDurationMs(uint32_t durationMs);
    void setStreamCallback(StreamCallback callback, void *context);

    bool begin();
    // Samples are streamed while started; the phone is responsible for persistent storage.
    void update(bool started, bool enabled, uint32_t nowMs);

    bool hasSignal(const char *name) const;
    String header() const;
    const String &status() const;

private:
    struct Signal
    {
        String name;
        ValueCallback callback = nullptr;
        const void *context = nullptr;
    };

    Signal _signals[MAX_SIGNALS];
    size_t _signalCount;
    StreamCallback _streamCallback;
    void *_streamContext;
    uint32_t _sampleIntervalMs;
    uint32_t _maxDurationMs;
    uint32_t _runStartedAt;
    uint32_t _elapsedRunStartedAt;
    uint64_t _elapsedMs;
    uint32_t _lastSampleAt;
    uint32_t _sampleSequence;
    uint32_t _droppedSamples;
    bool _schemaReady;
    bool _recordingFailed;
    bool _wasStarted;
    bool _recordingActive;
    bool _durationReached;
    bool _sessionWasLogged;
    char _sessionId[17];
    String _status;

    void beginSession(uint32_t nowMs);
    void emitSessionEvent(StreamFrame::Type type);
    bool appendSample(uint64_t elapsedMs);
    void setError(const char *message);
};
