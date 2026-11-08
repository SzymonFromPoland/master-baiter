#include "data_logger.h"

#include <LittleFS.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

constexpr size_t DataLogger::MAX_SIGNALS;
constexpr size_t DataLogger::BUFFER_SIZE;
constexpr size_t DataLogger::QUEUE_LENGTH;
constexpr size_t DataLogger::STATUS_BUFFER_SIZE;
constexpr uint32_t DataLogger::FLUSH_INTERVAL_MS;
constexpr uint32_t DataLogger::WORKER_POLL_INTERVAL_MS;
const char DataLogger::FILE_PATH[] = "/robot.csv";

namespace
{
constexpr uint32_t FLUSH_NOTIFICATION = 1U;
}

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
      _writerQueue(nullptr),
      _writerTask(nullptr),
      _storageMutex(nullptr),
      _commandDone(nullptr),
      _commandSucceeded(false),
      _commandFormatted(false),
      _commandError{},
      _mounted(false),
      _schemaReady(false),
      _recordingFailed(false),
      _wasStarted(false),
      _recordingActive(false),
      _durationReached(false),
      _droppedSamples(0),
      _queueHighWatermark(0),
      _metricsMux(portMUX_INITIALIZER_UNLOCKED),
      _writerFailed(false),
      _writerErrorPending(false),
      _writerError{},
      _lastFlushDurationUs(0),
      _maxFlushDurationUs(0),
      _flushCount(0),
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
    if (_writerTask)
        return true;

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

    _writerQueue = xQueueCreate(QUEUE_LENGTH, sizeof(QueueItem));
    _storageMutex = xSemaphoreCreateMutex();
    _commandDone = xSemaphoreCreateBinary();
    if (!_writerQueue || !_storageMutex || !_commandDone)
    {
        if (_writerQueue)
            vQueueDelete(_writerQueue);
        if (_storageMutex)
            vSemaphoreDelete(_storageMutex);
        if (_commandDone)
            vSemaphoreDelete(_commandDone);
        _writerQueue = nullptr;
        _storageMutex = nullptr;
        _commandDone = nullptr;
        setError("Could not allocate asynchronous logger resources.");
        return false;
    }

    if (xTaskCreatePinnedToCore(
            writerTaskEntry,
            "logWriter",
            4096,
            this,
            tskIDLE_PRIORITY + 1,
            &_writerTask,
            0) != pdPASS)
    {
        vQueueDelete(_writerQueue);
        vSemaphoreDelete(_storageMutex);
        vSemaphoreDelete(_commandDone);
        _writerQueue = nullptr;
        _storageMutex = nullptr;
        _commandDone = nullptr;
        _writerTask = nullptr;
        setError("Could not start the asynchronous logger task.");
        return false;
    }

    _status = "Ready; waiting for started.";
    return true;
}

void DataLogger::update(bool started, uint32_t nowMs)
{
    char writerError[STATUS_BUFFER_SIZE];
    if (consumeWriterError(writerError, sizeof(writerError)))
        setError(writerError);

    if (!started)
    {
        if (_wasStarted)
        {
            _elapsedMs += static_cast<uint32_t>(nowMs - _elapsedRunStartedAt);
            _elapsedRunStartedAt = nowMs;
            _wasStarted = false;
            _recordingActive = false;
            if (_writerTask)
                xTaskNotify(_writerTask, FLUSH_NOTIFICATION, eSetBits);
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
        if (_writerTask)
            xTaskNotify(_writerTask, FLUSH_NOTIFICATION, eSetBits);
        _status = "Maximum recording duration reached for this run.";
        return;
    }

    if (nowMs - _lastSampleAt < _sampleIntervalMs)
        return;

    _lastSampleAt = nowMs;
    enqueueSample(_elapsedMs);
}

bool DataLogger::clear()
{
    bool formatted = false;
    char error[STATUS_BUFFER_SIZE] = {};
    if (_writerTask)
    {
        if (!requestWorkerCommand(QueueItemType::Clear, formatted))
        {
            if (_commandError[0])
                setError(_commandError);
            return false;
        }
    }
    else
    {
        const bool needsStorageLock = _storageMutex != nullptr;
        if (needsStorageLock && xSemaphoreTake(_storageMutex, portMAX_DELAY) != pdTRUE)
        {
            setError("Could not lock LittleFS to clear the log.");
            return false;
        }
        if (!clearStorage(formatted, error, sizeof(error)))
        {
            if (needsStorageLock)
                xSemaphoreGive(_storageMutex);
            setError(error);
            return false;
        }
        if (needsStorageLock)
            xSemaphoreGive(_storageMutex);
        _lastFlushAt = millis();
    }

    _schemaReady = true;
    _recordingFailed = false;
    _status = formatted ? "LittleFS formatted and logs cleared." : "Logs cleared.";
    _recordingActive = _wasStarted && !_durationReached;
    _elapsedMs = 0;
    _elapsedRunStartedAt = millis();
    _droppedSamples = 0;
    _queueHighWatermark = 0;
    clearWriterError();
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

File DataLogger::openRead()
{
    if (!_mounted)
        return File();

    bool formatted = false;
    if (_writerTask && !requestWorkerCommand(QueueItemType::Flush, formatted))
    {
        if (_commandError[0])
            setError(_commandError);
        return File();
    }

    if (_storageMutex && xSemaphoreTake(_storageMutex, portMAX_DELAY) != pdTRUE)
        return File();

    if (!LittleFS.exists(FILE_PATH))
    {
        if (_storageMutex)
            xSemaphoreGive(_storageMutex);
        return File();
    }

    File file = LittleFS.open(FILE_PATH, FILE_READ);
    if (!file && _storageMutex)
        xSemaphoreGive(_storageMutex);
    return file;
}

void DataLogger::closeRead(File &file)
{
    file.close();
    if (_storageMutex)
        xSemaphoreGive(_storageMutex);
}

String DataLogger::status() const
{
    char writerError[STATUS_BUFFER_SIZE] = {};
    bool hasWriterError;
    portENTER_CRITICAL(&_metricsMux);
    hasWriterError = _writerFailed;
    if (hasWriterError)
        strncpy(writerError, _writerError, sizeof(writerError) - 1);
    portEXIT_CRITICAL(&_metricsMux);

    String result = hasWriterError && writerError[0] ? String(writerError) : _status;
    if (_droppedSamples)
    {
        result += " Queue overflow: ";
        result += String(_droppedSamples);
        result += " samples dropped.";
    }
    return result;
}

uint32_t DataLogger::queueDepth() const
{
    return _writerQueue ? uxQueueMessagesWaiting(_writerQueue) : 0;
}

uint32_t DataLogger::queueHighWatermark() const
{
    return _queueHighWatermark;
}

uint32_t DataLogger::droppedSamples() const
{
    return _droppedSamples;
}

uint32_t DataLogger::lastFlushDurationUs() const
{
    portENTER_CRITICAL(&_metricsMux);
    const uint32_t duration = _lastFlushDurationUs;
    portEXIT_CRITICAL(&_metricsMux);
    return duration;
}

uint32_t DataLogger::maxFlushDurationUs() const
{
    portENTER_CRITICAL(&_metricsMux);
    const uint32_t duration = _maxFlushDurationUs;
    portEXIT_CRITICAL(&_metricsMux);
    return duration;
}

uint32_t DataLogger::flushCount() const
{
    portENTER_CRITICAL(&_metricsMux);
    const uint32_t count = _flushCount;
    portEXIT_CRITICAL(&_metricsMux);
    return count;
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

bool DataLogger::appendSample(const QueueItem &sample)
{
    char row[512];
    int length = snprintf(row, sizeof(row), "%llu", static_cast<unsigned long long>(sample.elapsedMs));
    if (length < 0 || static_cast<size_t>(length) >= sizeof(row))
    {
        publishWriterError("Could not format a log sample.");
        return false;
    }

    for (size_t i = 0; i < _signalCount; ++i)
    {
        const int remaining = static_cast<int>(sizeof(row) - static_cast<size_t>(length));
        const int added = snprintf(
            row + length,
            static_cast<size_t>(remaining),
            ",%.3f",
            sample.values[i]);
        if (added < 0 || added >= remaining)
        {
            publishWriterError("A log sample exceeds the row buffer.");
            return false;
        }
        length += added;
    }

    if (static_cast<size_t>(length) + 1 >= sizeof(row))
    {
        publishWriterError("A log sample exceeds the row buffer.");
        return false;
    }
    row[length++] = '\n';

    if (_pendingLength + static_cast<size_t>(length) > BUFFER_SIZE &&
        !flushBuffer())
        return false;

    if (static_cast<size_t>(length) > BUFFER_SIZE)
    {
        publishWriterError("A log sample exceeds the flash write buffer.");
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
    if (writerFailed())
        return false;

    const uint32_t lockStartedAt = micros();
    const bool storageLocked = _storageMutex &&
                               xSemaphoreTake(_storageMutex, portMAX_DELAY) == pdTRUE;
    if (!storageLocked)
    {
        recordFlushDuration(micros() - lockStartedAt);
        publishWriterError("Could not lock LittleFS for a log write.");
        _pendingLength = 0;
        return false;
    }

    const uint32_t flushStartedAt = micros();
    File file = LittleFS.open(FILE_PATH, FILE_APPEND);
    if (!file)
    {
        xSemaphoreGive(_storageMutex);
        recordFlushDuration(micros() - flushStartedAt);
        _pendingLength = 0;
        publishWriterError("Could not append to the log file; flash may be full.");
        return false;
    }

    const size_t expectedLength = _pendingLength;
    const size_t written = file.write(
        reinterpret_cast<const uint8_t *>(_pendingBuffer),
        expectedLength);
    file.flush();
    file.close();
    xSemaphoreGive(_storageMutex);

    recordFlushDuration(micros() - flushStartedAt);
    _pendingLength = 0;

    if (written != expectedLength)
    {
        publishWriterError("Flash write failed; the latest buffered samples may be lost.");
        return false;
    }

    _lastFlushAt = millis();
    return true;
}

bool DataLogger::clearStorage(bool &formatted, char *error, size_t errorSize)
{
    formatted = false;
    if (error && errorSize)
        error[0] = '\0';

    if (!_mounted)
    {
        if (!LittleFS.begin(false))
        {
            Serial.println("[DataLogger] Clear requested; retrying LittleFS with format-on-failure.");
            if (!LittleFS.begin(true))
            {
                snprintf(error, errorSize, "LittleFS could not be formatted; logs were not cleared.");
                return false;
            }
            formatted = true;
        }
        _mounted = true;
    }

    _pendingLength = 0;
    if (LittleFS.exists(FILE_PATH) && !LittleFS.remove(FILE_PATH))
    {
        snprintf(error, errorSize, "Could not remove the log file; logs were not cleared.");
        return false;
    }

    File file = LittleFS.open(FILE_PATH, FILE_WRITE);
    if (!file || !writeHeader(file))
    {
        if (file)
            file.close();
        snprintf(error, errorSize, "Could not create a new log file after clearing.");
        return false;
    }
    file.close();
    return true;
}

bool DataLogger::enqueueSample(uint64_t elapsedMs)
{
    if (!_writerQueue || !_writerTask)
    {
        setError("The asynchronous logger is unavailable.");
        return false;
    }

    QueueItem item = {};
    item.type = QueueItemType::Sample;
    item.elapsedMs = elapsedMs;
    for (size_t i = 0; i < _signalCount; ++i)
        item.values[i] = _signals[i].callback(_signals[i].context);

    if (xQueueSend(_writerQueue, &item, 0) != pdTRUE)
    {
        if (_droppedSamples < UINT32_MAX)
            ++_droppedSamples;
        return false;
    }

    const uint32_t queued = uxQueueMessagesWaiting(_writerQueue);
    if (queued > _queueHighWatermark)
        _queueHighWatermark = queued;
    return true;
}

bool DataLogger::requestWorkerCommand(QueueItemType type, bool &formatted)
{
    if (!_writerQueue || !_writerTask || !_commandDone)
    {
        snprintf(_commandError, sizeof(_commandError), "The asynchronous logger is unavailable.");
        return false;
    }

    xSemaphoreTake(_commandDone, 0);
    QueueItem item = {};
    item.type = type;
    item.waitForCompletion = true;

    if (xQueueSend(_writerQueue, &item, portMAX_DELAY) != pdTRUE)
    {
        snprintf(_commandError, sizeof(_commandError), "Could not queue a logger command.");
        return false;
    }
    if (xSemaphoreTake(_commandDone, portMAX_DELAY) != pdTRUE)
    {
        snprintf(_commandError, sizeof(_commandError), "Could not wait for the logger task.");
        return false;
    }

    formatted = _commandFormatted;
    if (!_commandSucceeded)
    {
        if (!_commandError[0])
            snprintf(_commandError, sizeof(_commandError), "Logger command failed.");
        char ignoredError[STATUS_BUFFER_SIZE];
        consumeWriterError(ignoredError, sizeof(ignoredError));
        return false;
    }
    return true;
}

void DataLogger::writerTaskEntry(void *context)
{
    static_cast<DataLogger *>(context)->writerTask();
}

void DataLogger::writerTask()
{
    QueueItem item;
    for (;;)
    {
        bool flushRequested = false;
        if (xQueueReceive(
                _writerQueue,
                &item,
                pdMS_TO_TICKS(WORKER_POLL_INTERVAL_MS)) == pdTRUE)
        {
            processQueueItem(item);
            while (xQueueReceive(_writerQueue, &item, 0) == pdTRUE)
                processQueueItem(item);
        }

        uint32_t notifications = 0;
        if (xTaskNotifyWait(0, FLUSH_NOTIFICATION, &notifications, 0) == pdTRUE &&
            (notifications & FLUSH_NOTIFICATION))
            flushRequested = true;

        if (_pendingLength > 0 &&
            !writerFailed() &&
            (flushRequested ||
             static_cast<uint32_t>(millis() - _lastFlushAt) >= FLUSH_INTERVAL_MS))
            flushBuffer();
    }
}

void DataLogger::processQueueItem(const QueueItem &item)
{
    if (item.type == QueueItemType::Sample)
    {
        if (!writerFailed() && appendSample(item) && _pendingLength >= BUFFER_SIZE / 2)
            flushBuffer();
        return;
    }

    if (item.type == QueueItemType::Flush)
    {
        const bool succeeded = flushBuffer();
        if (item.waitForCompletion)
        {
            _commandSucceeded = succeeded;
            _commandFormatted = false;
            _commandError[0] = '\0';
            if (!succeeded)
            {
                portENTER_CRITICAL(&_metricsMux);
                strncpy(_commandError, _writerError, sizeof(_commandError) - 1);
                _commandError[sizeof(_commandError) - 1] = '\0';
                portEXIT_CRITICAL(&_metricsMux);
            }
            xSemaphoreGive(_commandDone);
        }
        return;
    }

    bool formatted = false;
    char error[STATUS_BUFFER_SIZE] = {};
    bool succeeded = false;
    if (_storageMutex && xSemaphoreTake(_storageMutex, portMAX_DELAY) == pdTRUE)
    {
        succeeded = clearStorage(formatted, error, sizeof(error));
        _lastFlushAt = millis();
        xSemaphoreGive(_storageMutex);
    }
    else
    {
        snprintf(error, sizeof(error), "Could not lock LittleFS to clear the log.");
    }

    if (succeeded)
        clearWriterError();
    else
        publishWriterError(error);

    _commandSucceeded = succeeded;
    _commandFormatted = formatted;
    strncpy(_commandError, error, sizeof(_commandError) - 1);
    _commandError[sizeof(_commandError) - 1] = '\0';
    xSemaphoreGive(_commandDone);
}

bool DataLogger::consumeWriterError(char *error, size_t errorSize)
{
    portENTER_CRITICAL(&_metricsMux);
    const bool pending = _writerErrorPending;
    if (pending && error && errorSize)
    {
        strncpy(error, _writerError, errorSize - 1);
        error[errorSize - 1] = '\0';
    }
    if (pending)
        _writerErrorPending = false;
    portEXIT_CRITICAL(&_metricsMux);
    return pending;
}

bool DataLogger::writerFailed() const
{
    portENTER_CRITICAL(&_metricsMux);
    const bool failed = _writerFailed;
    portEXIT_CRITICAL(&_metricsMux);
    return failed;
}

void DataLogger::publishWriterError(const char *error)
{
    portENTER_CRITICAL(&_metricsMux);
    _writerFailed = true;
    _writerErrorPending = true;
    strncpy(_writerError, error ? error : "Unknown asynchronous logger error.", sizeof(_writerError) - 1);
    _writerError[sizeof(_writerError) - 1] = '\0';
    portEXIT_CRITICAL(&_metricsMux);
}

void DataLogger::clearWriterError()
{
    portENTER_CRITICAL(&_metricsMux);
    _writerFailed = false;
    _writerErrorPending = false;
    _writerError[0] = '\0';
    portEXIT_CRITICAL(&_metricsMux);
}

void DataLogger::recordFlushDuration(uint32_t durationUs)
{
    portENTER_CRITICAL(&_metricsMux);
    _lastFlushDurationUs = durationUs;
    if (durationUs > _maxFlushDurationUs)
        _maxFlushDurationUs = durationUs;
    if (_flushCount < UINT32_MAX)
        ++_flushCount;
    portEXIT_CRITICAL(&_metricsMux);
}

void DataLogger::setError(const char *message)
{
    _status = message ? message : "Unknown logging error.";
    _recordingFailed = true;
    _recordingActive = false;
    Serial.printf("[DataLogger] %s\n", _status.c_str());
}
