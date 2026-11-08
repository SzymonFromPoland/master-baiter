#pragma once

#include <Arduino.h>
#include <FS.h>

class DataLogger
{
public:
    using ValueCallback = float (*)(const void *context);

    static constexpr size_t MAX_SIGNALS = 24;

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
    // Applies to each started run; zero means no duration limit.
    void setMaxDurationMs(uint32_t durationMs);

    bool begin();
    // Samples and elapsed time advance only while started; existing logs resume from their saved time.
    void update(bool started, uint32_t nowMs);
    bool clear();

    bool hasSignal(const char *name) const;
    File openRead() const;
    const String &status() const;

private:
    struct Signal
    {
        String name;
        ValueCallback callback = nullptr;
        const void *context = nullptr;
    };

    static constexpr size_t BUFFER_SIZE = 131072;
    static constexpr uint32_t FLUSH_INTERVAL_MS = 1000;
    static const char FILE_PATH[];

    Signal _signals[MAX_SIGNALS];
    size_t _signalCount;

    char _pendingBuffer[BUFFER_SIZE];
    size_t _pendingLength;
    uint32_t _sampleIntervalMs;
    uint32_t _maxDurationMs;
    uint32_t _runStartedAt;
    uint32_t _elapsedRunStartedAt;
    uint64_t _elapsedMs;
    uint32_t _lastSampleAt;
    uint32_t _lastFlushAt;

    bool _mounted;
    bool _schemaReady;
    bool _recordingFailed;
    bool _wasStarted;
    bool _recordingActive;
    bool _durationReached;

    String _status;

    String header() const;
    bool restoreElapsedMs();
    bool writeHeader(File &file);
    bool appendSample(uint64_t elapsedMs);
    bool flushBuffer();
    void setError(const char *message);
};
