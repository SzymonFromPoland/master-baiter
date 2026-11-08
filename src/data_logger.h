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
    // Samples are buffered while started, flushed when full, and flushed again after stopping.
    void update(bool started, bool enabled, uint32_t nowMs);
    bool clear();

    bool hasSignal(const char *name) const;
    File openRead() const;
    const String &status() const;
    // Returns the persisted JSON summary for the most recently completed run.
    const String &lastSessionStatsJson() const;

private:
    struct Signal
    {
        String name;
        ValueCallback callback = nullptr;
        const void *context = nullptr;
    };

    struct SignalRange
    {
        float minimum = 0.0f;
        float maximum = 0.0f;
        bool hasSamples = false;
    };

    static constexpr size_t BUFFER_SIZE = 128 * 1024;
    static const char FILE_PATH[];
    static const char SESSION_STATS_FILE_PATH[];

    Signal _signals[MAX_SIGNALS];
    SignalRange _activeSessionRanges[MAX_SIGNALS];
    size_t _signalCount;

    char _pendingBuffer[BUFFER_SIZE];
    size_t _pendingLength;
    uint32_t _sampleIntervalMs;
    uint32_t _maxDurationMs;
    uint32_t _runStartedAt;
    uint32_t _elapsedRunStartedAt;
    uint64_t _elapsedMs;
    uint32_t _lastSampleAt;
    uint64_t _activeSessionBytesSaved;
    uint32_t _activeSessionFlushCount;

    bool _mounted;
    bool _schemaReady;
    bool _recordingFailed;
    bool _wasStarted;
    bool _recordingActive;
    bool _durationReached;

    String _lastSessionStatsJson;
    String _status;

    String header() const;
    bool restoreElapsedMs();
    bool restoreLastSessionStats();
    bool saveLastSessionStats() const;
    String buildSessionStatsJson() const;
    void resetActiveSessionStats();
    bool writeHeader(File &file);
    bool appendSample(uint64_t elapsedMs);
    bool flushBuffer();
    void setError(const char *message);
};
