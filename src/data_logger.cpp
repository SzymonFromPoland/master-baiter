#include "data_logger.h"

#include <esp_system.h>
#include <stdio.h>
#include <string.h>

DataLogger::DataLogger()
    : _signalCount(0),
      _streamCallback(nullptr),
      _streamContext(nullptr),
      _sampleIntervalMs(50),
      _maxDurationMs(0),
      _runStartedAt(0),
      _elapsedRunStartedAt(0),
      _elapsedMs(0),
      _lastSampleAt(0),
      _sampleSequence(0),
      _droppedSamples(0),
      _schemaReady(false),
      _recordingFailed(false),
      _wasStarted(false),
      _recordingActive(false),
      _durationReached(false),
      _sessionWasLogged(false),
      _sessionId{},
      _status("Not initialized.")
{
}

bool DataLogger::add(const char *name, ValueCallback callback, const void *context)
{
    if (_schemaReady)
    {
        Serial.println("[DataLogger] Signals must be registered before begin().");
        return false;
    }

    if (!name || !name[0] || !callback)
    {
        Serial.println("[DataLogger] Invalid signal name or callback.");
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
            Serial.printf("[DataLogger] Invalid signal name '%s'; use letters, digits, '_' or '-'.\n", name);
            return false;
        }
    }

    if (_signalCount >= MAX_SIGNALS)
    {
        Serial.printf("[DataLogger] Signal limit reached (%u).\n", static_cast<unsigned int>(MAX_SIGNALS));
        return false;
    }

    if (hasSignal(name))
    {
        Serial.printf("[DataLogger] Duplicate signal '%s'.\n", name);
        return false;
    }

    Signal &signal = _signals[_signalCount++];
    signal.name = name;
    signal.callback = callback;
    signal.context = context;
    return true;
}

bool DataLogger::setSampleIntervalMs(uint32_t intervalMs)
{
    if (intervalMs == 0)
    {
        Serial.println("[DataLogger] Sample interval must be greater than zero.");
        _status = "Sample interval must be greater than zero.";
        return false;
    }

    _sampleIntervalMs = intervalMs;
    return true;
}

void DataLogger::setMaxDurationMs(uint32_t durationMs)
{
    _maxDurationMs = durationMs;
}

void DataLogger::setStreamCallback(StreamCallback callback, void *context)
{
    _streamCallback = callback;
    _streamContext = context;
}

bool DataLogger::begin()
{
    if (_signalCount == 0)
    {
        setError("No signals are configured.");
        return false;
    }

    _schemaReady = true;
    _recordingFailed = false;
    _status = "Ready; waiting for started and a phone connection.";
    return true;
}

void DataLogger::update(bool started, bool enabled, uint32_t nowMs)
{
    if (!started)
    {
        if (_wasStarted)
        {
            _elapsedMs += static_cast<uint32_t>(nowMs - _elapsedRunStartedAt);
            _elapsedRunStartedAt = nowMs;
            _wasStarted = false;
            _recordingActive = false;
            if (_sessionWasLogged)
                emitSessionEvent(StreamFrame::Type::SessionStop);
            _sessionWasLogged = false;
            _durationReached = false;
            if (!_recordingFailed)
                _status = "Ready; waiting for started and a phone connection.";
        }
        return;
    }

    if (!_wasStarted)
    {
        _wasStarted = true;
        _runStartedAt = nowMs;
        _elapsedRunStartedAt = nowMs;
        _elapsedMs = 0;
        _lastSampleAt = nowMs - _sampleIntervalMs;
        _sampleSequence = 0;
        _droppedSamples = 0;
        _durationReached = false;
        _recordingActive = false;
        _sessionWasLogged = false;
        beginSession(nowMs);
    }
    else
    {
        _elapsedMs += static_cast<uint32_t>(nowMs - _elapsedRunStartedAt);
        _elapsedRunStartedAt = nowMs;
    }

    if (!enabled)
    {
        _recordingActive = false;
        return;
    }

    if (!_recordingActive && !_recordingFailed && !_durationReached && _schemaReady)
    {
        _recordingActive = true;
        if (!_sessionWasLogged)
        {
            emitSessionEvent(StreamFrame::Type::SessionStart);
            _sessionWasLogged = true;
        }
        _status = "Streaming to phone.";
    }

    if (!_recordingActive || _recordingFailed)
        return;

    if (_maxDurationMs > 0 && nowMs - _runStartedAt >= _maxDurationMs)
    {
        _recordingActive = false;
        _durationReached = true;
        _status = "Maximum recording duration reached for this run.";
        return;
    }

    if (nowMs - _lastSampleAt < _sampleIntervalMs)
        return;

    _lastSampleAt = nowMs;
    appendSample(_elapsedMs);
}

bool DataLogger::hasSignal(const char *name) const
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

String DataLogger::header() const
{
    String result = "elapsed_ms";
    for (size_t i = 0; i < _signalCount; ++i)
    {
        result += ',';
        result += _signals[i].name;
    }
    return result;
}

const String &DataLogger::status() const
{
    return _status;
}

void DataLogger::beginSession(uint32_t nowMs)
{
    snprintf(
        _sessionId,
        sizeof(_sessionId),
        "%08lx%08lx",
        static_cast<unsigned long>(esp_random()),
        static_cast<unsigned long>(nowMs));
}

void DataLogger::emitSessionEvent(StreamFrame::Type type)
{
    if (!_streamCallback)
        return;

    StreamFrame frame;
    frame.type = type;
    memcpy(frame.sessionId, _sessionId, sizeof(frame.sessionId));
    frame.sequence = _sampleSequence;
    frame.droppedSamples = _droppedSamples;
    frame.elapsedMs = _elapsedMs;
    _streamCallback(frame, _streamContext);
}

bool DataLogger::appendSample(uint64_t elapsedMs)
{
    StreamFrame frame;
    frame.type = StreamFrame::Type::Sample;
    memcpy(frame.sessionId, _sessionId, sizeof(frame.sessionId));
    frame.sequence = ++_sampleSequence;
    frame.droppedSamples = _droppedSamples;
    frame.elapsedMs = elapsedMs;
    frame.signalCount = static_cast<uint8_t>(_signalCount);

    for (size_t i = 0; i < _signalCount; ++i)
        frame.values[i] = _signals[i].callback(_signals[i].context);

    if (!_streamCallback || !_streamCallback(frame, _streamContext))
        ++_droppedSamples;
    return true;
}

void DataLogger::setError(const char *message)
{
    _status = message ? message : "Unknown logging error.";
    _recordingFailed = true;
    _recordingActive = false;
    Serial.printf("[DataLogger] %s\n", _status.c_str());
}
