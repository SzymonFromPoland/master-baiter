#include "data_logger.h"

#include <LittleFS.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

constexpr size_t DataLogger::MAX_SIGNALS;
constexpr size_t DataLogger::BUFFER_SIZE;
constexpr uint32_t DataLogger::FLUSH_INTERVAL_MS;
const char DataLogger::FILE_PATH[] = "/robot.csv";

DataLogger::DataLogger()
    : _signalCount(0),
      _pendingLength(0),
      _sampleIntervalMs(50),
      _maxDurationMs(0),
      _runStartedAt(0),
      _elapsedRunStartedAt(0),
      _elapsedMs(0),
      _lastSampleAt(0),
      _lastFlushAt(0),
      _mounted(false),
      _schemaReady(false),
      _recordingFailed(false),
      _wasStarted(false),
      _recordingActive(false),
      _durationReached(false),
      _status("Not initialized.")
{
}

bool DataLogger::add(const char *name, ValueCallback callback, const void *context)
{
    if (_mounted)
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
        Serial.printf("[DataLogger] Signal limit reached (%u).\n", (unsigned int)MAX_SIGNALS);
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

bool DataLogger::begin()
{
    if (_signalCount == 0)
    {
        setError("No signals are configured.");
        return false;
    }

    if (!_mounted)
    {
        if (!LittleFS.begin(false))
        {
            setError("LittleFS mount failed; logging is disabled.");
            return false;
        }
        _mounted = true;
    }

    String expectedHeader = header();
    if (expectedHeader.endsWith("\n"))
        expectedHeader.remove(expectedHeader.length() - 1);
    String legacyExpectedHeader = "uptime_ms";
    const int firstColumnEnd = expectedHeader.indexOf(',');
    if (firstColumnEnd >= 0)
        legacyExpectedHeader += expectedHeader.substring(firstColumnEnd);

    if (LittleFS.exists(FILE_PATH))
    {
        File file = LittleFS.open(FILE_PATH, FILE_READ);
        if (!file)
        {
            setError("Could not open the existing log file.");
            return false;
        }

        String savedHeader = file.readStringUntil('\n');
        file.close();
        if (savedHeader.endsWith("\r"))
            savedHeader.remove(savedHeader.length() - 1);

        if (savedHeader.length() == 0)
        {
            File emptyFile = LittleFS.open(FILE_PATH, FILE_APPEND);
            if (!emptyFile || !writeHeader(emptyFile))
            {
                if (emptyFile)
                    emptyFile.close();
                setError("Could not initialize the empty log file.");
                return false;
            }
            emptyFile.close();
        }
        else if (savedHeader != expectedHeader && savedHeader != legacyExpectedHeader)
        {
            _schemaReady = false;
            setError("Stored log columns differ from the configured signals; parse or clear logs before recording.");
            return false;
        }
    }
    else
    {
        File file = LittleFS.open(FILE_PATH, FILE_WRITE);
        if (!file || !writeHeader(file))
        {
            if (file)
                file.close();
            setError("Could not create the log file.");
            return false;
        }
        file.close();
    }

    if (!restoreElapsedMs())
        return false;

    _schemaReady = true;
    _recordingFailed = false;
    _pendingLength = 0;
    _lastFlushAt = millis();
    _status = "Ready; waiting for started.";
    return true;
}

void DataLogger::update(bool started, uint32_t nowMs)
{
    if (!started)
    {
        if (_wasStarted)
        {
            _elapsedMs += static_cast<uint32_t>(nowMs - _elapsedRunStartedAt);
            _elapsedRunStartedAt = nowMs;
            _wasStarted = false;
            _recordingActive = false;
            if (!_recordingFailed && !flushBuffer())
                return;
            _durationReached = false;
            if (!_recordingFailed)
                _status = "Ready; waiting for started.";
        }
        return;
    }

    if (!_wasStarted)
    {
        _wasStarted = true;
        _runStartedAt = nowMs;
        _elapsedRunStartedAt = nowMs;
        _lastSampleAt = nowMs - _sampleIntervalMs;
        _durationReached = false;
        _recordingActive = _mounted && _schemaReady && !_recordingFailed;
        if (_recordingActive)
            _status = "Recording.";
    }
    else
    {
        _elapsedMs += static_cast<uint32_t>(nowMs - _elapsedRunStartedAt);
        _elapsedRunStartedAt = nowMs;
    }

    if (!_recordingActive || _recordingFailed)
        return;

    if (_maxDurationMs > 0 && nowMs - _runStartedAt >= _maxDurationMs)
    {
        _recordingActive = false;
        _durationReached = true;
        if (!flushBuffer())
            return;
        _status = "Maximum recording duration reached for this run.";
        return;
    }

    if (nowMs - _lastSampleAt < _sampleIntervalMs)
        return;

    _lastSampleAt = nowMs;
    if (!appendSample(_elapsedMs))
        return;

    if (_pendingLength >= BUFFER_SIZE / 2 ||
        nowMs - _lastFlushAt >= FLUSH_INTERVAL_MS)
        flushBuffer();
}

bool DataLogger::clear()
{
    bool formatted = false;
    if (!_mounted)
    {
        if (!LittleFS.begin(false))
        {
            Serial.println("[DataLogger] Clear requested; retrying LittleFS with format-on-failure.");
            if (!LittleFS.begin(true))
            {
                setError("LittleFS could not be formatted; logs were not cleared.");
                return false;
            }
            formatted = true;
        }
        _mounted = true;
    }

    const uint32_t nowMs = millis();
    _pendingLength = 0;
    if (LittleFS.exists(FILE_PATH) && !LittleFS.remove(FILE_PATH))
    {
        setError("Could not remove the log file; logs were not cleared.");
        return false;
    }

    File file = LittleFS.open(FILE_PATH, FILE_WRITE);
    if (!file || !writeHeader(file))
    {
        if (file)
            file.close();
        setError("Could not create a new log file after clearing.");
        return false;
    }
    file.close();

    _schemaReady = true;
    _recordingFailed = false;
    _status = formatted ? "LittleFS formatted and logs cleared." : "Logs cleared.";
    _recordingActive = _wasStarted && !_durationReached;
    _elapsedMs = 0;
    _elapsedRunStartedAt = nowMs;
    _lastFlushAt = nowMs;
    return true;
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

File DataLogger::openRead() const
{
    if (!_mounted || !LittleFS.exists(FILE_PATH))
        return File();
    return LittleFS.open(FILE_PATH, FILE_READ);
}

const String &DataLogger::status() const
{
    return _status;
}

String DataLogger::header() const
{
    String result = "elapsed_ms";
    for (size_t i = 0; i < _signalCount; ++i)
    {
        result += ',';
        result += _signals[i].name;
    }
    result += '\n';
    return result;
}

bool DataLogger::restoreElapsedMs()
{
    File file = LittleFS.open(FILE_PATH, FILE_READ);
    if (!file)
    {
        setError("Could not read the log file to restore elapsed time.");
        return false;
    }

    file.readStringUntil('\n');
    uint64_t elapsedMs = 0;
    bool foundSample = false;
    while (file.available())
    {
        String row = file.readStringUntil('\n');
        if (row.length() == 0)
            continue;

        const char *start = row.c_str();
        if (start[0] < '0' || start[0] > '9')
            continue;

        char *end = nullptr;
        errno = 0;
        const unsigned long long timestamp = strtoull(start, &end, 10);
        if (end == start || *end != ',' || errno == ERANGE)
            continue;

        size_t fieldCount = 1;
        for (size_t i = 0; i < row.length(); ++i)
        {
            if (row[i] == ',')
                ++fieldCount;
        }
        if (fieldCount != _signalCount + 1)
            continue;

        if (!foundSample || timestamp > elapsedMs)
        {
            elapsedMs = static_cast<uint64_t>(timestamp);
            foundSample = true;
        }
    }

    file.close();
    _elapsedMs = foundSample ? elapsedMs : 0;
    return true;
}

bool DataLogger::writeHeader(File &file)
{
    const String contents = header();
    const size_t written = file.write(
        reinterpret_cast<const uint8_t *>(contents.c_str()),
        contents.length());
    file.flush();
    return written == contents.length();
}

bool DataLogger::appendSample(uint64_t elapsedMs)
{
    char row[512];
    int length = snprintf(row, sizeof(row), "%llu", static_cast<unsigned long long>(elapsedMs));
    if (length < 0 || static_cast<size_t>(length) >= sizeof(row))
    {
        setError("Could not format a log sample.");
        return false;
    }

    for (size_t i = 0; i < _signalCount; ++i)
    {
        const int remaining = static_cast<int>(sizeof(row) - static_cast<size_t>(length));
        const int added = snprintf(
            row + length,
            static_cast<size_t>(remaining),
            ",%.3f",
            _signals[i].callback(_signals[i].context));
        if (added < 0 || added >= remaining)
        {
            setError("A log sample exceeds the row buffer.");
            return false;
        }
        length += added;
    }

    if (static_cast<size_t>(length) + 1 >= sizeof(row))
    {
        setError("A log sample exceeds the row buffer.");
        return false;
    }
    row[length++] = '\n';

    if (_pendingLength + static_cast<size_t>(length) > BUFFER_SIZE &&
        !flushBuffer())
        return false;

    if (static_cast<size_t>(length) > BUFFER_SIZE)
    {
        setError("A log sample exceeds the flash write buffer.");
        return false;
    }

    memcpy(_pendingBuffer + _pendingLength, row, static_cast<size_t>(length));
    _pendingLength += static_cast<size_t>(length);
    return true;
}

bool DataLogger::flushBuffer()
{
    if (_pendingLength == 0)
        return true;

    if (!_mounted || !_schemaReady || _recordingFailed)
        return false;

    File file = LittleFS.open(FILE_PATH, FILE_APPEND);
    if (!file)
    {
        _pendingLength = 0;
        setError("Could not append to the log file; flash may be full.");
        return false;
    }

    const size_t written = file.write(
        reinterpret_cast<const uint8_t *>(_pendingBuffer),
        _pendingLength);
    file.flush();
    file.close();

    if (written != _pendingLength)
    {
        _pendingLength = 0;
        setError("Flash write failed; the latest buffered samples may be lost.");
        return false;
    }

    _pendingLength = 0;
    _lastFlushAt = millis();
    return true;
}

void DataLogger::setError(const char *message)
{
    _status = message ? message : "Unknown logging error.";
    _recordingFailed = true;
    _recordingActive = false;
    Serial.printf("[DataLogger] %s\n", _status.c_str());
}
