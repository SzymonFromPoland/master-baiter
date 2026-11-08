#pragma once

#include <Arduino.h>
#include <FS.h>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

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
    // Holds the storage lock until closeRead() is called.
    File openRead();
    void closeRead(File &file);
    String status() const;
    uint32_t queueDepth() const;
    uint32_t queueHighWatermark() const;
    uint32_t droppedSamples() const;
    uint32_t lastFlushDurationUs() const;
    uint32_t maxFlushDurationUs() const;
    uint32_t flushCount() const;

private:
    struct Signal
    {
        String name;
        ValueCallback callback = nullptr;
        const void *context = nullptr;
    };

    enum class QueueItemType : uint8_t
    {
        Sample,
        Flush,
        Clear
    };

    struct QueueItem
    {
        QueueItemType type;
        bool waitForCompletion;
        uint64_t elapsedMs;
        float values[MAX_SIGNALS];
    };

    static constexpr size_t BUFFER_SIZE = 8192;
    static constexpr size_t QUEUE_LENGTH = 64;
    static constexpr size_t STATUS_BUFFER_SIZE = 128;
    static constexpr uint32_t FLUSH_INTERVAL_MS = 1000;
    static constexpr uint32_t WORKER_POLL_INTERVAL_MS = 50;
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

    QueueHandle_t _writerQueue;
    TaskHandle_t _writerTask;
    SemaphoreHandle_t _storageMutex;
    SemaphoreHandle_t _commandDone;
    bool _commandSucceeded;
    bool _commandFormatted;
    char _commandError[STATUS_BUFFER_SIZE];

    bool _mounted;
    bool _schemaReady;
    bool _recordingFailed;
    bool _wasStarted;
    bool _recordingActive;
    bool _durationReached;

    uint32_t _droppedSamples;
    uint32_t _queueHighWatermark;
    mutable portMUX_TYPE _metricsMux;
    bool _writerFailed;
    bool _writerErrorPending;
    char _writerError[STATUS_BUFFER_SIZE];
    uint32_t _lastFlushDurationUs;
    uint32_t _maxFlushDurationUs;
    uint32_t _flushCount;

    String _status;

    String header() const;
    bool restoreElapsedMs();
    bool writeHeader(File &file);
    bool appendSample(const QueueItem &sample);
    bool flushBuffer();
    bool clearStorage(bool &formatted, char *error, size_t errorSize);
    bool enqueueSample(uint64_t elapsedMs);
    bool requestWorkerCommand(QueueItemType type, bool &formatted);
    void writerTask();
    static void writerTaskEntry(void *context);
    void processQueueItem(const QueueItem &item);
    bool consumeWriterError(char *error, size_t errorSize);
    bool writerFailed() const;
    void publishWriterError(const char *error);
    void clearWriterError();
    void recordFlushDuration(uint32_t durationUs);
    void setError(const char *message);
};
