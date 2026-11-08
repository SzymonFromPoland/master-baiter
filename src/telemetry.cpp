#include "telemetry.h"

#include <string.h>

static const char NO_SESSION_STATS_JSON[] =
    "{\"hasSession\":false,\"bytesSaved\":0,\"flushCount\":0,\"signals\":{}}";

Telemetry::Telemetry()
    : _signalCount(0),
      _loopsPerSample(10),
      _loopsSinceSample(0),
      _runStartedAt(0),
      _wasStarted(false),
      _recordingActive(false),
      _status("Not initialized."),
      _server(PORT),
      _queue(nullptr),
      _taskHandle(nullptr),
      _clientConnected(false),
      _droppedSamples(0),
      _nextSequence(0),
      _recordingState(0),
      _runState(0),
      _started(false)
{
}

bool Telemetry::add(const char *name, ValueCallback callback, const void *context)
{
    if (_started)
    {
        Serial.println("[Telemetry] Signals must be registered before begin().");
        return false;
    }

    if (!name || !name[0] || !callback)
    {
        Serial.println("[Telemetry] Invalid signal name or callback.");
        return false;
    }

    for (const char *character = name; *character; ++character)
    {
        const bool valid =
            (*character >= 'a' && *character <= 'z') ||
            (*character >= 'A' && *character <= 'Z') ||
            (*character >= '0' && *character <= '9') ||
            *character == '_' || *character == '-';
        if (!valid)
        {
            Serial.printf("[Telemetry] Invalid signal name '%s'; use letters, digits, '_' or '-'.\n", name);
            return false;
        }
    }

    if (_signalCount >= MAX_SIGNALS)
    {
        Serial.printf("[Telemetry] Signal limit reached (%u).\n", static_cast<unsigned int>(MAX_SIGNALS));
        return false;
    }

    if (hasSignal(name))
    {
        Serial.printf("[Telemetry] Duplicate signal '%s'.\n", name);
        return false;
    }

    Signal &signal = _signals[_signalCount++];
    signal.name = name;
    signal.callback = callback;
    signal.context = context;
    return true;
}

bool Telemetry::setSampleIntervalLoops(uint32_t loopsPerSample)
{
    if (loopsPerSample == 0)
    {
        Serial.println("[Telemetry] Samples-per-interval loop count must be greater than zero.");
        _status = "Samples-per-interval loop count must be greater than zero.";
        return false;
    }

    _loopsPerSample = loopsPerSample;
    _loopsSinceSample = 0;
    return true;
}

bool Telemetry::begin()
{
    if (_started)
        return true;

    if (_signalCount == 0)
    {
        setError("No signals are configured.");
        return false;
    }

    _queue = xQueueCreate(QUEUE_LENGTH, sizeof(Sample));
    if (!_queue)
    {
        setError("Could not allocate the sample queue.");
        return false;
    }

    _server.begin();
    BaseType_t taskCreated = xTaskCreatePinnedToCore(
        taskEntry,
        "telemetry",
        4096,
        this,
        1,
        &_taskHandle,
        0);

    if (taskCreated != pdPASS)
    {
        vQueueDelete(_queue);
        _queue = nullptr;
        setError("Could not start the network task.");
        return false;
    }

    _started = true;
    _status = "Ready; waiting for started.";
    Serial.printf("[Telemetry] Listening on TCP port %u.\n", static_cast<unsigned int>(PORT));
    return true;
}

void Telemetry::update(bool started, bool enabled, uint32_t nowMs)
{
    if (!_started)
        return;

    if (started && !_wasStarted)
        _runStartedAt = nowMs;
    _wasStarted = started;

    if (!started || !enabled)
    {
        _recordingActive = false;
        _loopsSinceSample = 0;
        if (!started)
            _status = "Ready; waiting for started.";
        return;
    }

    if (!_recordingActive)
    {
        _recordingActive = true;
        _loopsSinceSample = 0;
        _status = "Streaming samples to the phone.";
    }

    if (++_loopsSinceSample < _loopsPerSample)
        return;

    _loopsSinceSample = 0;
    float sampleValues[MAX_SIGNALS];
    for (size_t i = 0; i < _signalCount; ++i)
        sampleValues[i] = _signals[i].callback(_signals[i].context);

    enqueue(
        static_cast<uint32_t>(nowMs - _runStartedAt),
        sampleValues,
        _signalCount);
}

bool Telemetry::connected() const
{
    return _clientConnected;
}

void Telemetry::setRecordingState(bool active)
{
    const uint32_t previous = _recordingState;
    if (active && !(previous & 1U))
        _recordingState = (previous + 2U) | 1U;
    else if (!active && (previous & 1U))
        _recordingState = previous & ~1U;
}

void Telemetry::setRunState(bool active)
{
    const uint32_t previous = _runState;
    if (active && !(previous & 1U))
        _runState = (previous + 2U) | 1U;
    else if (!active && (previous & 1U))
        _runState = previous & ~1U;
}

bool Telemetry::enqueue(uint32_t elapsedMs, const float *values, size_t count)
{
    if (!_started || !_clientConnected || !values || count == 0 || count > MAX_SIGNALS)
        return false;

    Sample sample;
    sample.sequence = _nextSequence++;
    sample.elapsedMs = elapsedMs;
    sample.recordingState = _recordingState;
    sample.runState = _runState;
    sample.count = static_cast<uint8_t>(count);
    memcpy(sample.values, values, count * sizeof(float));

    if (xQueueSend(_queue, &sample, 0) != pdTRUE)
    {
        ++_droppedSamples;
        return false;
    }

    return true;
}

bool Telemetry::clear()
{
    _status = "Log files are stored on the phone.";
    return false;
}

bool Telemetry::hasSignal(const char *name) const
{
    if (!name)
        return false;

    for (size_t i = 0; i < _signalCount; ++i)
    {
        if (_signals[i].name == name)
            return true;
    }

    return false;
}

size_t Telemetry::signalCount() const
{
    return _signalCount;
}

const String &Telemetry::signalName(size_t index) const
{
    return _signals[index].name;
}

File Telemetry::openRead() const
{
    return File();
}

const String &Telemetry::status() const
{
    return _status;
}

const String &Telemetry::lastSessionStatsJson() const
{
    static const String stats(NO_SESSION_STATS_JSON);
    return stats;
}

void Telemetry::setError(const char *message)
{
    _status = message ? message : "Unknown telemetry error.";
    Serial.printf("[Telemetry] %s\n", _status.c_str());
}

void Telemetry::taskEntry(void *context)
{
    static_cast<Telemetry *>(context)->run();
}

void Telemetry::run()
{
    for (;;)
    {
        WiFiClient client = _server.available();
        if (!client)
        {
            _clientConnected = false;
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        Sample staleSample;
        while (xQueueReceive(_queue, &staleSample, 0) == pdTRUE)
        {
        }

        client.setNoDelay(true);
        _clientConnected = true;
        uint32_t lastSentState = UINT32_MAX;
        uint32_t lastSentRunState = UINT32_MAX;

        while (client.connected())
        {
            Sample samples[MAX_BATCH_SAMPLES];
            if (xQueueReceive(_queue, &samples[0], pdMS_TO_TICKS(BATCH_WINDOW_MS)) != pdTRUE)
            {
                const uint32_t state = _recordingState;
                const uint32_t runState = _runState;
                if (state != lastSentState || runState != lastSentRunState)
                {
                    uint8_t header[FRAME_HEADER_SIZE] = {'M', 'B', 'T', 4, 0, 0, 0, 0};
                    const uint32_t dropped = _droppedSamples;
                    memcpy(header + 8, &dropped, sizeof(dropped));
                    memcpy(header + 12, &state, sizeof(state));
                    memcpy(header + 16, &runState, sizeof(runState));
                    if (client.write(header, sizeof(header)) != sizeof(header))
                        break;
                    lastSentState = state;
                    lastSentRunState = runState;
                }
                continue;
            }

            size_t sampleCount = 1;
            const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(BATCH_WINDOW_MS);
            while (sampleCount < MAX_BATCH_SAMPLES)
            {
                const TickType_t now = xTaskGetTickCount();
                if (static_cast<int32_t>(deadline - now) <= 0)
                    break;

                if (xQueueReceive(_queue, &samples[sampleCount], deadline - now) != pdTRUE)
                    break;
                ++sampleCount;
            }

            uint8_t frame[FRAME_HEADER_SIZE + MAX_BATCH_SAMPLES * (16 + MAX_SIGNALS * sizeof(float))];
            frame[0] = 'M';
            frame[1] = 'B';
            frame[2] = 'T';
            frame[3] = 4;
            frame[4] = static_cast<uint8_t>(sampleCount);
            frame[5] = samples[0].count;
            frame[6] = 0;
            frame[7] = 0;

            const uint32_t droppedSamples = _droppedSamples;
            memcpy(frame + 8, &droppedSamples, sizeof(droppedSamples));
            const uint32_t state = _recordingState;
            memcpy(frame + 12, &state, sizeof(state));
            const uint32_t runState = _runState;
            memcpy(frame + 16, &runState, sizeof(runState));
            size_t frameLength = FRAME_HEADER_SIZE;
            for (size_t i = 0; i < sampleCount; ++i)
            {
                if (samples[i].count != samples[0].count)
                {
                    Serial.println("[Telemetry] Signal count changed within a batch.");
                    client.stop();
                    break;
                }
                memcpy(frame + frameLength, &samples[i].sequence, sizeof(uint32_t));
                frameLength += sizeof(uint32_t);
                memcpy(frame + frameLength, &samples[i].elapsedMs, sizeof(uint32_t));
                frameLength += sizeof(uint32_t);
                memcpy(frame + frameLength, &samples[i].recordingState, sizeof(uint32_t));
                frameLength += sizeof(uint32_t);
                memcpy(frame + frameLength, &samples[i].runState, sizeof(uint32_t));
                frameLength += sizeof(uint32_t);
                const size_t valuesSize = samples[i].count * sizeof(float);
                memcpy(frame + frameLength, samples[i].values, valuesSize);
                frameLength += valuesSize;
            }
            if (!client.connected())
                break;
            if (client.write(frame, frameLength) != frameLength)
                break;
        }

        _clientConnected = false;
        client.stop();
    }
}
