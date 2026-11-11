#include "tuner.h"

#include "telemetry.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

namespace
{
constexpr unsigned long HOME_WIFI_CONNECT_TIMEOUT_MS = 15000;

void appendJsonString(String &json, const String &value)
{
    json += '"';
    for (size_t i = 0; i < value.length(); ++i)
    {
        const char character = value[i];
        switch (character)
        {
        case '"':
            json += "\\\"";
            break;
        case '\\':
            json += "\\\\";
            break;
        case '\b':
            json += "\\b";
            break;
        case '\f':
            json += "\\f";
            break;
        case '\n':
            json += "\\n";
            break;
        case '\r':
            json += "\\r";
            break;
        case '\t':
            json += "\\t";
            break;
        default:
            if (static_cast<uint8_t>(character) < 0x20)
            {
                char escaped[7];
                snprintf(escaped, sizeof(escaped), "\\u%04x", static_cast<unsigned char>(character));
                json += escaped;
            }
            else
            {
                json += character;
            }
            break;
        }
    }
    json += '"';
}
}

WiFiTuner::WiFiTuner()
    : _server(80),
      _parameterCount(0),
      _currentTab("Main"),
      _logSource(nullptr),
      _hasDisplay(false),
      _notificationStartedUs(0),
      _notificationDurationUs(0),
      _notificationActive(false),
      _stationMode(false)
{
}

WiFiTuner::~WiFiTuner()
{
    for (int i = 0; i < _parameterCount; i++)
    {
        free(_parameters[i].optionValues);
        _parameters[i].optionValues = nullptr;
    }
}

void WiFiTuner::configureServer()
{
    _server.on("/", HTTP_GET, [this]()
               { handleRoot(); });

    _server.on("/api/schema", HTTP_GET, [this]()
               { handleSchema(); });

    _server.on("/update", HTTP_GET, [this]()
               { handleUpdate(); });

    _server.on("/button", HTTP_GET, [this]()
               { handleButton(); });

    _server.on("/display", HTTP_GET, [this]()
               { handleDisplay(); });

    _server.on("/notification", HTTP_GET, [this]()
               { handleNotification(); });

    _server.on("/notification/cancel", HTTP_GET, [this]()
               {
                   cancelNotification();
                   _server.send(200, "text/plain", "OK");
               });

    _server.begin();
}

void WiFiTuner::begin(const char *ssid)
{
    _stationMode = false;
    WiFi.mode(WIFI_AP);
    if (!WiFi.softAP(ssid, "0123456789"))
        Serial.println("[Tuner] Failed to start Wi-Fi access point");

    configureServer();

    Serial.println();
    Serial.println("Robot control API started");
    Serial.print("SSID: ");
    Serial.println(ssid);
    Serial.print("IP: ");
    Serial.println(WiFi.softAPIP());
}

void WiFiTuner::begin(const char *ssid, const char *password)
{
    _stationMode = false;

    if (!ssid || !ssid[0])
    {
        Serial.println("[Tuner] Home Wi-Fi SSID is empty; starting access point instead");
        begin();
        return;
    }

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(ssid, password ? password : "");

    Serial.print("[Tuner] Connecting to Wi-Fi: ");
    Serial.println(ssid);

    const unsigned long startedAt = millis();
    while (WiFi.status() != WL_CONNECTED &&
           (unsigned long)(millis() - startedAt) < HOME_WIFI_CONNECT_TIMEOUT_MS)
    {
        delay(100);
    }

    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("[Tuner] Could not connect to home Wi-Fi; starting access point instead");
        WiFi.disconnect(true);
        begin();
        return;
    }

    _stationMode = true;
    configureServer();

    Serial.println();
    Serial.println("Robot control API started");
    Serial.print("Connected to SSID: ");
    Serial.println(ssid);
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
}

void WiFiTuner::handle()
{
    notificationActive();

    if (_stationMode || WiFi.softAPgetStationNum() > 0)
        _server.handleClient();
}

void WiFiTuner::notify(const char *message, unsigned long durationUs)
{
    if (!message || message[0] == '\0')
    {
        cancelNotification();
        return;
    }

    _notificationMessage = message;
    _notificationStartedUs = micros();
    _notificationDurationUs = durationUs;
    _notificationActive = true;
}

void WiFiTuner::cancelNotification()
{
    _notificationMessage = "";
    _notificationActive = false;
}

bool WiFiTuner::notificationActive()
{
    if (_notificationActive && _notificationDurationUs > 0 &&
        (unsigned long)(micros() - _notificationStartedUs) >= _notificationDurationUs)
    {
        cancelNotification();
    }

    return _notificationActive;
}

size_t WiFiTuner::printf(const char *format, ...)
{
    char buffer[DISPLAY_PRINTF_BUFFER_SIZE];

    va_list args;
    va_start(args, format);
    int length = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    if (length <= 0)
        return 0;

    size_t written = (size_t)length;
    size_t stored = min(written, sizeof(buffer) - 1);
    buffer[stored] = '\0';
    appendDisplayText(buffer);

    return written;
}

void WiFiTuner::clearDisplay()
{
    _displayText = "";
}

void WiFiTuner::addTab(const char *name)
{
    if (!name || !name[0])
    {
        Serial.println("[Tuner] Tab name cannot be empty.");
        return;
    }

    _currentTab = name;
    _currentCategory = "";
}

void WiFiTuner::addCategory(const char *name)
{
    _currentCategory = name;
}

void WiFiTuner::addAngle(
    const char *name,
    float *value,
    FloatCallback onChange)
{
    if (!name || !name[0] || !value)
    {
        Serial.println("[Tuner] Angle control requires a name and value.");
        return;
    }

    if (_parameterCount >= MAX_PARAMETERS)
    {
        Serial.println("[Tuner] Parameter limit reached; angle control was not added.");
        return;
    }

    if (!isfinite(*value))
    {
        Serial.printf("[Tuner] Angle '%s' must have a finite initial value.\n", name);
        return;
    }

    const float boundedValue = constrain(*value, -180.0f, 180.0f);
    if (boundedValue != *value)
    {
        Serial.printf("[Tuner] Angle '%s' was clamped to [-180, 180].\n", name);
        *value = boundedValue;
        if (onChange)
            onChange(boundedValue);
    }

    Parameter &p = _parameters[_parameterCount++];
    p.name = name;
    p.tab = _currentTab;
    p.category = _currentCategory;
    p.type = ANGLE;
    p.value = value;
    p.minFloat = -180.0f;
    p.maxFloat = 180.0f;
    p.stepFloat = 0.1f;
    p.floatCallback = onChange;
    p.boolCallback = nullptr;
    p.intCallback = nullptr;
    p.uint8Callback = nullptr;
    p.uint16Callback = nullptr;
    p.uintCallback = nullptr;
    p.ulongCallback = nullptr;
    p.buttonCallback = nullptr;
}

void WiFiTuner::setTelemetry(Telemetry &telemetry)
{
    _logSource = &telemetry;
}

bool WiFiTuner::addRaw(
    const char *title,
    Telemetry &telemetry,
    std::initializer_list<const char *> signals)
{
    return addLogView(title, telemetry, signals, LOG_RAW);
}

bool WiFiTuner::addChart(
    const char *title,
    Telemetry &telemetry,
    std::initializer_list<const char *> signals)
{
    return addLogView(title, telemetry, signals, LOG_CHART);
}

bool WiFiTuner::addChart(
    const char *title,
    Telemetry &telemetry,
    std::initializer_list<const char *> signals,
    float minimum,
    float maximum)
{
    return addLogView(title, telemetry, signals, LOG_CHART, true, minimum, maximum);
}

bool WiFiTuner::addLogView(
    const char *title,
    Telemetry &telemetry,
    std::initializer_list<const char *> signals,
    ParameterType type,
    bool hasChartBounds,
    float chartMinimum,
    float chartMaximum)
{
    if (!title || !title[0] || signals.size() == 0)
    {
        Serial.println("[Tuner] Log view title and at least one signal are required.");
        return false;
    }

    if (hasChartBounds &&
        (type != LOG_CHART || !isfinite(chartMinimum) || !isfinite(chartMaximum) ||
         chartMinimum >= chartMaximum))
    {
        Serial.printf("[Tuner] Chart '%s' requires finite bounds with minimum less than maximum.\n", title);
        return false;
    }

    if (_logSource && _logSource != &telemetry)
    {
        Serial.println("[Tuner] Only one telemetry source can be displayed.");
        return false;
    }

    if (_parameterCount >= MAX_PARAMETERS)
    {
        Serial.println("[Tuner] Parameter limit reached; log view was not added.");
        return false;
    }

    String series;
    for (auto current = signals.begin(); current != signals.end(); ++current)
    {
        if (!*current || !telemetry.hasSignal(*current))
        {
            Serial.printf("[Tuner] Log view '%s' references an unconfigured telemetry signal.\n", title);
            return false;
        }

        for (auto previous = signals.begin(); previous != current; ++previous)
        {
            if (strcmp(*previous, *current) == 0)
            {
                Serial.printf("[Tuner] Log view '%s' contains duplicate signal '%s'.\n", title, *current);
                return false;
            }
        }

        if (series.length())
            series += ',';
        series += *current;
    }

    Parameter &p = _parameters[_parameterCount++];
    p.name = title;
    p.tab = _currentTab;
    p.category = _currentCategory;
    p.type = type;
    p.logSeries = series;
    p.hasChartBounds = hasChartBounds;
    p.chartMinimum = chartMinimum;
    p.chartMaximum = chartMaximum;
    p.value = nullptr;
    p.floatCallback = nullptr;
    p.boolCallback = nullptr;
    p.intCallback = nullptr;
    p.uint8Callback = nullptr;
    p.uint16Callback = nullptr;
    p.uintCallback = nullptr;
    p.ulongCallback = nullptr;
    p.buttonCallback = nullptr;

    _logSource = &telemetry;
    return true;
}

void WiFiTuner::addSlider(
    const char *name,
    float *value,
    float min,
    float max,
    float step,
    FloatCallback onChange)
{
    if (_parameterCount >= MAX_PARAMETERS)
        return;

    Parameter &p = _parameters[_parameterCount++];

    p.name = name;
    p.tab = _currentTab;
    p.category = _currentCategory;
    p.type = SLIDER;
    p.value = value;
    p.minFloat = min;
    p.maxFloat = max;
    p.stepFloat = step;
    p.floatCallback = onChange;
    p.boolCallback = nullptr;
    p.intCallback = nullptr;
    p.uint16Callback = nullptr;
    p.uintCallback = nullptr;
    p.ulongCallback = nullptr;
    p.buttonCallback = nullptr;
}

void WiFiTuner::addSlider(
    const char *name,
    int *value,
    int min,
    int max,
    int step,
    IntCallback onChange)
{
    if (_parameterCount >= MAX_PARAMETERS)
        return;

    Parameter &p = _parameters[_parameterCount++];

    p.name = name;
    p.tab = _currentTab;
    p.category = _currentCategory;
    p.type = INT_SLIDER;
    p.value = value;
    p.minInt = min;
    p.maxInt = max;
    p.stepInt = step;
    p.floatCallback = nullptr;
    p.boolCallback = nullptr;
    p.intCallback = onChange;
    p.uint16Callback = nullptr;
    p.uintCallback = nullptr;
    p.ulongCallback = nullptr;
    p.buttonCallback = nullptr;
}

void WiFiTuner::addSlider(
    const char *name,
    uint16_t *value,
    uint32_t min,
    uint32_t max,
    uint32_t step,
    UInt16Callback onChange)
{
    if (_parameterCount >= MAX_PARAMETERS)
        return;

    Parameter &p = _parameters[_parameterCount++];

    p.name = name;
    p.tab = _currentTab;
    p.category = _currentCategory;
    p.type = UINT16_SLIDER;
    p.value = value;
    p.minUInt16 = (min > UINT16_MAX) ? UINT16_MAX : (uint16_t)min;
    p.maxUInt16 = (max > UINT16_MAX) ? UINT16_MAX : (uint16_t)max;
    p.stepUInt16 = (step > UINT16_MAX) ? UINT16_MAX : (uint16_t)step;
    p.floatCallback = nullptr;
    p.boolCallback = nullptr;
    p.intCallback = nullptr;
    p.uint16Callback = onChange;
    p.uintCallback = nullptr;
    p.ulongCallback = nullptr;
    p.buttonCallback = nullptr;
}

void WiFiTuner::addSlider(
    const char *name,
    unsigned int *value,
    unsigned int min,
    unsigned int max,
    unsigned int step,
    UIntCallback onChange)
{
    if (_parameterCount >= MAX_PARAMETERS)
        return;

    Parameter &p = _parameters[_parameterCount++];

    p.name = name;
    p.tab = _currentTab;
    p.category = _currentCategory;
    p.type = UINT_SLIDER;
    p.value = value;
    p.minUInt = min;
    p.maxUInt = max;
    p.stepUInt = step;
    p.floatCallback = nullptr;
    p.boolCallback = nullptr;
    p.intCallback = nullptr;
    p.uint16Callback = nullptr;
    p.uintCallback = onChange;
    p.ulongCallback = nullptr;
    p.buttonCallback = nullptr;
}

void WiFiTuner::addSlider(
    const char *name,
    unsigned long *value,
    unsigned long min,
    unsigned long max,
    unsigned long step,
    ULongCallback onChange)
{
    if (_parameterCount >= MAX_PARAMETERS)
        return;

    Parameter &p = _parameters[_parameterCount++];

    p.name = name;
    p.tab = _currentTab;
    p.category = _currentCategory;
    p.type = ULONG_SLIDER;
    p.value = value;
    p.minULong = min;
    p.maxULong = max;
    p.stepULong = step;
    p.floatCallback = nullptr;
    p.boolCallback = nullptr;
    p.intCallback = nullptr;
    p.uint16Callback = nullptr;
    p.uintCallback = nullptr;
    p.ulongCallback = onChange;
    p.buttonCallback = nullptr;
}

void WiFiTuner::addSlider(
    const char *name,
    uint8_t *value,
    uint32_t min,
    uint32_t max,
    uint32_t step,
    UInt8Callback onChange)
{
    if (_parameterCount >= MAX_PARAMETERS)
        return;

    Parameter &p = _parameters[_parameterCount++];

    p.name = name;
    p.tab = _currentTab;
    p.category = _currentCategory;
    p.type = UINT8_SLIDER;
    p.value = value;
    p.minUInt8 = (min > UINT8_MAX) ? UINT8_MAX : (uint8_t)min;
    p.maxUInt8 = (max > UINT8_MAX) ? UINT8_MAX : (uint8_t)max;
    p.stepUInt8 = (step > UINT8_MAX) ? UINT8_MAX : (uint8_t)step;
    p.floatCallback = nullptr;
    p.boolCallback = nullptr;
    p.intCallback = nullptr;
    p.uint8Callback = onChange;
    p.uint16Callback = nullptr;
    p.uintCallback = nullptr;
    p.ulongCallback = nullptr;
    p.buttonCallback = nullptr;
}

template <typename T>
WiFiTuner::Parameter *WiFiTuner::makeDropdown(
    const char *name,
    T *value,
    DropdownKind kind,
    std::initializer_list<const char *> labels,
    std::initializer_list<T> values)
{
    if (_parameterCount >= MAX_PARAMETERS || !value)
        return nullptr;

    size_t count = min(labels.size(), values.size());

    if (count == 0)
        return nullptr;

    double *optionValues = (double *)malloc(count * sizeof(double));

    if (!optionValues)
        return nullptr;

    Parameter &p = _parameters[_parameterCount++];

    p.name = name;
    p.tab = _currentTab;
    p.category = _currentCategory;
    p.type = DROPDOWN;
    p.value = value;
    p.dropdownKind = kind;
    p.optionLabels = "";
    p.optionValues = optionValues;
    p.optionCount = (int)count;

    size_t i = 0;

    for (const char *label : labels)
    {
        if (i >= count)
            break;

        if (i > 0)
            p.optionLabels += '\n';

        p.optionLabels += label;
        i++;
    }

    i = 0;

    for (T v : values)
    {
        if (i >= count)
            break;

        p.optionValues[i++] = (double)v;
    }

    p.floatCallback = nullptr;
    p.boolCallback = nullptr;
    p.intCallback = nullptr;
    p.uint16Callback = nullptr;
    p.uintCallback = nullptr;
    p.ulongCallback = nullptr;
    p.buttonCallback = nullptr;

    return &p;
}

void WiFiTuner::addDropdown(
    const char *name,
    float *value,
    std::initializer_list<const char *> labels,
    std::initializer_list<float> values,
    FloatCallback onChange)
{
    Parameter *p = makeDropdown<float>(name, value, DD_FLOAT, labels, values);

    if (p)
        p->floatCallback = onChange;
}

void WiFiTuner::addDropdown(
    const char *name,
    bool *value,
    std::initializer_list<const char *> labels,
    std::initializer_list<bool> values,
    BoolCallback onChange)
{
    Parameter *p = makeDropdown<bool>(name, value, DD_BOOL, labels, values);

    if (p)
        p->boolCallback = onChange;
}

void WiFiTuner::addDropdown(
    const char *name,
    int *value,
    std::initializer_list<const char *> labels,
    std::initializer_list<int> values,
    IntCallback onChange)
{
    Parameter *p = makeDropdown<int>(name, value, DD_INT, labels, values);

    if (p)
        p->intCallback = onChange;
}

void WiFiTuner::addDropdown(
    const char *name,
    uint16_t *value,
    std::initializer_list<const char *> labels,
    std::initializer_list<uint16_t> values,
    UInt16Callback onChange)
{
    Parameter *p = makeDropdown<uint16_t>(name, value, DD_UINT16, labels, values);

    if (p)
        p->uint16Callback = onChange;
}

void WiFiTuner::addDropdown(
    const char *name,
    unsigned int *value,
    std::initializer_list<const char *> labels,
    std::initializer_list<unsigned int> values,
    UIntCallback onChange)
{
    Parameter *p = makeDropdown<unsigned int>(name, value, DD_UINT, labels, values);

    if (p)
        p->uintCallback = onChange;
}

void WiFiTuner::addDropdown(
    const char *name,
    unsigned long *value,
    std::initializer_list<const char *> labels,
    std::initializer_list<unsigned long> values,
    ULongCallback onChange)
{
    Parameter *p = makeDropdown<unsigned long>(name, value, DD_ULONG, labels, values);

    if (p)
        p->ulongCallback = onChange;
}

void WiFiTuner::addDropdown(
    const char *name,
    uint8_t *value,
    std::initializer_list<const char *> labels,
    std::initializer_list<uint8_t> values,
    UInt8Callback onChange)
{
    Parameter *p = makeDropdown<uint8_t>(name, value, DD_UINT8, labels, values);

    if (p)
        p->uint8Callback = onChange;
}

void WiFiTuner::addButton(
    const char *name,
    ButtonCallback onPress)
{
    if (_parameterCount >= MAX_PARAMETERS)
        return;

    Parameter &p = _parameters[_parameterCount++];

    p.name = name;
    p.tab = _currentTab;
    p.category = _currentCategory;
    p.type = BUTTON;
    p.value = nullptr;
    p.minFloat = 0;
    p.maxFloat = 0;
    p.stepFloat = 0;
    p.minInt = 0;
    p.maxInt = 0;
    p.floatCallback = nullptr;
    p.boolCallback = nullptr;
    p.intCallback = nullptr;
    p.uint16Callback = nullptr;
    p.uintCallback = nullptr;
    p.ulongCallback = nullptr;
    p.buttonCallback = onPress;
}

void WiFiTuner::addDisplay(const char *name)
{
    if (_hasDisplay || _parameterCount >= MAX_PARAMETERS)
        return;

    Parameter &p = _parameters[_parameterCount++];

    p.name = name;
    p.tab = _currentTab;
    p.category = _currentCategory;
    p.type = DISPLAY_TEXT;
    p.value = nullptr;
    p.minFloat = 0;
    p.maxFloat = 0;
    p.stepFloat = 0;
    p.minInt = 0;
    p.maxInt = 0;
    p.stepInt = 0;
    p.minUInt = 0;
    p.maxUInt = 0;
    p.stepUInt = 0;
    p.minULong = 0;
    p.maxULong = 0;
    p.stepULong = 0;
    p.floatCallback = nullptr;
    p.boolCallback = nullptr;
    p.intCallback = nullptr;
    p.uint16Callback = nullptr;
    p.uintCallback = nullptr;
    p.ulongCallback = nullptr;
    p.buttonCallback = nullptr;

    _hasDisplay = true;
}

WiFiTuner::Parameter *WiFiTuner::findParameter(int index)
{
    if (index < 0 || index >= _parameterCount)
        return nullptr;

    return &_parameters[index];
}

bool WiFiTuner::getParameterValue(const Parameter &p, double &value) const
{
    switch (p.type)
    {
    case SLIDER:
    case ANGLE:
        value = *(float *)p.value;
        break;
    case INT_SLIDER:
        value = *(int *)p.value;
        break;
    case UINT8_SLIDER:
        value = *(uint8_t *)p.value;
        break;
    case UINT16_SLIDER:
        value = *(uint16_t *)p.value;
        break;
    case UINT_SLIDER:
        value = *(unsigned int *)p.value;
        break;
    case ULONG_SLIDER:
        value = *(unsigned long *)p.value;
        break;
    case DROPDOWN:
    {
        const double currentValue = dropdownCurrentValue(p);
        for (int i = 0; i < p.optionCount; ++i)
        {
            if (p.optionValues[i] == currentValue)
            {
                value = i;
                return true;
            }
        }
        return false;
    }
    default:
        return false;
    }

    return isfinite(value);
}

double WiFiTuner::dropdownCurrentValue(const Parameter &p) const
{
    switch (p.dropdownKind)
    {
    case DD_FLOAT:
        return (double)*(float *)p.value;
    case DD_BOOL:
        return *(bool *)p.value ? 1.0 : 0.0;
    case DD_INT:
        return (double)*(int *)p.value;
    case DD_UINT8:
        return (double)*(uint8_t *)p.value;
    case DD_UINT16:
        return (double)*(uint16_t *)p.value;
    case DD_UINT:
        return (double)*(unsigned int *)p.value;
    case DD_ULONG:
        return (double)*(unsigned long *)p.value;
    }

    return 0.0;
}

bool WiFiTuner::setDropdownOption(Parameter &p, int optionIndex)
{
    if (p.type != DROPDOWN || optionIndex < 0 || optionIndex >= p.optionCount)
        return false;

    double selected = p.optionValues[optionIndex];
    if (selected == dropdownCurrentValue(p))
        return true;

    switch (p.dropdownKind)
    {
    case DD_FLOAT:
    {
        float newValue = (float)selected;
        *(float *)p.value = newValue;
        if (p.floatCallback)
            p.floatCallback(newValue);
        break;
    }
    case DD_BOOL:
    {
        bool newValue = selected != 0.0;
        *(bool *)p.value = newValue;
        if (p.boolCallback)
            p.boolCallback(newValue);
        break;
    }
    case DD_INT:
    {
        int newValue = (int)selected;
        *(int *)p.value = newValue;
        if (p.intCallback)
            p.intCallback(newValue);
        break;
    }
    case DD_UINT8:
    {
        uint8_t newValue = (uint8_t)selected;
        *(uint8_t *)p.value = newValue;
        if (p.uint8Callback)
            p.uint8Callback(newValue);
        break;
    }
    case DD_UINT16:
    {
        uint16_t newValue = (uint16_t)selected;
        *(uint16_t *)p.value = newValue;
        if (p.uint16Callback)
            p.uint16Callback(newValue);
        break;
    }
    case DD_UINT:
    {
        unsigned int newValue = (unsigned int)selected;
        *(unsigned int *)p.value = newValue;
        if (p.uintCallback)
            p.uintCallback(newValue);
        break;
    }
    case DD_ULONG:
    {
        unsigned long newValue = (unsigned long)selected;
        *(unsigned long *)p.value = newValue;
        if (p.ulongCallback)
            p.ulongCallback(newValue);
        break;
    }
    }

    return true;
}

bool WiFiTuner::applyParameterValue(Parameter &p, double value)
{
    if (!isfinite(value))
        return false;

    if (p.type == DROPDOWN)
    {
        if (value < 0.0 || value >= p.optionCount || value != floor(value))
            return false;
        return setDropdownOption(p, (int)value);
    }

    if (p.type == INT_SLIDER)
    {
        int newValue;
        if (value < p.minInt)
            newValue = p.minInt;
        else if (value > p.maxInt)
            newValue = p.maxInt;
        else
            newValue = (int)value;

        if (newValue != *(int *)p.value)
        {
            *(int *)p.value = newValue;
            if (p.intCallback)
                p.intCallback(newValue);
            if (p.floatCallback)
                p.floatCallback((float)newValue);
        }
        return true;
    }

    if (p.type == UINT8_SLIDER)
    {
        uint8_t newValue;
        if (value < p.minUInt8)
            newValue = p.minUInt8;
        else if (value > p.maxUInt8)
            newValue = p.maxUInt8;
        else
            newValue = (uint8_t)value;

        if (newValue != *(uint8_t *)p.value)
        {
            *(uint8_t *)p.value = newValue;
            if (p.uint8Callback)
                p.uint8Callback(newValue);
        }
        return true;
    }

    if (p.type == UINT16_SLIDER)
    {
        uint16_t newValue;
        if (value < p.minUInt16)
            newValue = p.minUInt16;
        else if (value > p.maxUInt16)
            newValue = p.maxUInt16;
        else
            newValue = (uint16_t)value;

        if (newValue != *(uint16_t *)p.value)
        {
            *(uint16_t *)p.value = newValue;
            if (p.uint16Callback)
                p.uint16Callback(newValue);
        }
        return true;
    }

    if (p.type == UINT_SLIDER)
    {
        unsigned int newValue;
        if (value < p.minUInt)
            newValue = p.minUInt;
        else if (value > p.maxUInt)
            newValue = p.maxUInt;
        else
            newValue = (unsigned int)value;

        if (newValue != *(unsigned int *)p.value)
        {
            *(unsigned int *)p.value = newValue;
            if (p.floatCallback)
                p.floatCallback((float)newValue);
            if (p.uintCallback)
                p.uintCallback(newValue);
        }
        return true;
    }

    if (p.type == ULONG_SLIDER)
    {
        unsigned long newValue;
        if (value < p.minULong)
            newValue = p.minULong;
        else if (value > p.maxULong)
            newValue = p.maxULong;
        else
            newValue = (unsigned long)value;

        if (newValue != *(unsigned long *)p.value)
        {
            *(unsigned long *)p.value = newValue;
            if (p.floatCallback)
                p.floatCallback((float)newValue);
            if (p.ulongCallback)
                p.ulongCallback(newValue);
        }
        return true;
    }

    if (p.type == SLIDER || p.type == ANGLE)
    {
        if (value < p.minFloat)
            value = p.minFloat;
        else if (value > p.maxFloat)
            value = p.maxFloat;

        float newValue = (float)value;
        if (newValue != *(float *)p.value)
        {
            *(float *)p.value = newValue;
            if (p.floatCallback)
                p.floatCallback(newValue);
        }
        return true;
    }

    return false;
}

void WiFiTuner::handleRoot()
{
    _server.send(410, "text/plain", "The tuner interface is available in the Android app.");
}

void WiFiTuner::handleSchema()
{
    String response = "{\"version\":1,\"parameters\":[";
    bool firstParameter = true;

    for (int i = 0; i < _parameterCount; ++i)
    {
        const Parameter &parameter = _parameters[i];
        const char *type = nullptr;
        double minimum = 0.0;
        double maximum = 0.0;
        double step = 0.0;

        switch (parameter.type)
        {
        case SLIDER:
        case ANGLE:
            type = parameter.type == ANGLE ? "angle" : "slider";
            minimum = parameter.minFloat;
            maximum = parameter.maxFloat;
            step = parameter.stepFloat;
            break;
        case INT_SLIDER:
            type = "slider";
            minimum = parameter.minInt;
            maximum = parameter.maxInt;
            step = parameter.stepInt;
            break;
        case UINT8_SLIDER:
            type = "slider";
            minimum = parameter.minUInt8;
            maximum = parameter.maxUInt8;
            step = parameter.stepUInt8;
            break;
        case UINT16_SLIDER:
            type = "slider";
            minimum = parameter.minUInt16;
            maximum = parameter.maxUInt16;
            step = parameter.stepUInt16;
            break;
        case UINT_SLIDER:
            type = "slider";
            minimum = parameter.minUInt;
            maximum = parameter.maxUInt;
            step = parameter.stepUInt;
            break;
        case ULONG_SLIDER:
            type = "slider";
            minimum = parameter.minULong;
            maximum = parameter.maxULong;
            step = parameter.stepULong;
            break;
        case DROPDOWN:
            type = "dropdown";
            break;
        case BUTTON:
            type = "button";
            break;
        case DISPLAY_TEXT:
            type = "display";
            break;
        case LOG_CHART:
            type = "chart";
            break;
        case LOG_RAW:
            type = "raw";
            break;
        default:
            continue;
        }

        if (!firstParameter)
            response += ',';
        firstParameter = false;

        response += "{\"id\":";
        response += String(i);
        response += ",\"name\":";
        appendJsonString(response, parameter.name);
        response += ",\"tab\":";
        appendJsonString(response, parameter.tab.length() ? parameter.tab : String("Main"));
        response += ",\"category\":";
        appendJsonString(response, parameter.category);
        response += ",\"type\":";
        appendJsonString(response, type);

        double value;
        if (getParameterValue(parameter, value))
        {
            response += ",\"value\":";
            response += String(value, 6);
        }

        if (parameter.type == DROPDOWN)
        {
            const double currentValue = dropdownCurrentValue(parameter);
            int selected = -1;
            for (int option = 0; option < parameter.optionCount; ++option)
            {
                if (parameter.optionValues[option] == currentValue)
                {
                    selected = option;
                    break;
                }
            }

            response += ",\"selected\":";
            response += String(selected);
            response += ",\"options\":[";
            int labelStart = 0;
            for (int option = 0; option < parameter.optionCount; ++option)
            {
                if (option > 0)
                    response += ',';

                int labelEnd = parameter.optionLabels.indexOf('\n', labelStart);
                if (labelEnd < 0)
                    labelEnd = parameter.optionLabels.length();
                const String label = parameter.optionLabels.substring(labelStart, labelEnd);

                response += "{\"label\":";
                appendJsonString(response, label);
                response += ",\"value\":";
                response += String(parameter.optionValues[option], 6);
                response += '}';
                labelStart = labelEnd + 1;
            }
            response += ']';
        }
        else if (parameter.type == LOG_CHART || parameter.type == LOG_RAW)
        {
            response += ",\"series\":[";
            int start = 0;
            while (start < static_cast<int>(parameter.logSeries.length()))
            {
                int end = parameter.logSeries.indexOf(',', start);
                if (end < 0)
                    end = parameter.logSeries.length();
                if (start > 0)
                    response += ',';
                appendJsonString(response, parameter.logSeries.substring(start, end));
                start = end + 1;
            }
            response += ']';
            if (parameter.hasChartBounds)
            {
                response += ",\"min\":" + String(parameter.chartMinimum, 6);
                response += ",\"max\":" + String(parameter.chartMaximum, 6);
            }
        }
        else if (parameter.type != BUTTON && parameter.type != DISPLAY_TEXT)
        {
            response += ",\"min\":";
            response += String(minimum, 6);
            response += ",\"max\":";
            response += String(maximum, 6);
            response += ",\"step\":";
            response += String(step, 6);
        }

        response += '}';
    }

    response += "],\"signals\":[";
    if (_logSource)
    {
        for (size_t i = 0; i < _logSource->signalCount(); ++i)
        {
            if (i > 0)
                response += ',';
            appendJsonString(response, _logSource->signalName(i));
        }
    }
    response += "]}";
    _server.sendHeader("Cache-Control", "no-store");
    _server.send(200, "application/json", response);
}

void WiFiTuner::handleUpdate()
{
    if (notificationActive())
    {
        _server.send(423, "text/plain", "Parameters are locked by an active notification");
        return;
    }

    if (!_server.hasArg("id") || !_server.hasArg("value"))
    {
        _server.send(400, "text/plain", "Missing parameters");
        return;
    }

    int index = _server.arg("id").toInt();
    Parameter *p = findParameter(index);

    if (!p)
    {
        _server.send(404, "text/plain", "Invalid parameter");
        return;
    }

    String value = _server.arg("value");

    if (p->type == BUTTON || p->type == DISPLAY_TEXT ||
        p->type == LOG_CHART || p->type == LOG_RAW)
    {
        _server.send(400, "text/plain", "Parameter cannot be updated");
        return;
    }

    double parsedValue;
    if (p->type == DROPDOWN || p->type == INT_SLIDER)
        parsedValue = value.toInt();
    else if (p->type == UINT8_SLIDER || p->type == UINT16_SLIDER ||
             p->type == UINT_SLIDER || p->type == ULONG_SLIDER)
        parsedValue = strtoul(value.c_str(), nullptr, 10);
    else
        parsedValue = value.toFloat();

    if (!applyParameterValue(*p, parsedValue))
    {
        _server.send(400, "text/plain", "Invalid parameter value");
        return;
    }

    _server.send(200, "text/plain", "OK");
}

void WiFiTuner::handleButton()
{
    if (notificationActive())
    {
        _server.send(423, "text/plain", "Controls are locked by an active notification");
        return;
    }

    if (!_server.hasArg("id"))
    {
        _server.send(400, "text/plain", "Missing parameter");
        return;
    }

    int index = _server.arg("id").toInt();
    Parameter *p = findParameter(index);

    if (!p || p->type != BUTTON)
    {
        _server.send(404, "text/plain", "Invalid button");
        return;
    }

    if (p->buttonCallback)
        p->buttonCallback();

    _server.send(200, "text/plain", "OK");
}

void WiFiTuner::handleDisplay()
{
    _server.send(200, "text/plain", _displayText);
}

void WiFiTuner::handleNotification()
{
    if (notificationActive())
        _server.send(200, "text/plain", _notificationMessage);
    else
        _server.send(200, "text/plain", "");
}

void WiFiTuner::handleLogParse()
{
    if (!_logSource)
    {
        _server.send(404, "text/plain", "No log source is configured.");
        return;
    }

    File file = _logSource->openRead();
    if (!file)
    {
        _server.send(503, "text/plain", _logSource->status());
        return;
    }

    _server.sendHeader("Cache-Control", "no-store");
    _server.sendHeader("X-Log-Status", _logSource->status());
    _server.streamFile(file, "text/csv");
    file.close();
}

void WiFiTuner::handleLogStats()
{
    if (!_logSource)
    {
        _server.send(404, "text/plain", "No log source is configured.");
        return;
    }

    _server.sendHeader("Cache-Control", "no-store");
    _server.send(200, "application/json", _logSource->lastSessionStatsJson());
}

void WiFiTuner::handleLogClear()
{
    if (notificationActive())
    {
        _server.send(423, "text/plain", "Controls are locked by an active notification");
        return;
    }

    if (!_logSource)
    {
        _server.send(404, "text/plain", "No log source is configured.");
        return;
    }

    if (!_logSource->clear())
    {
        _server.send(500, "text/plain", _logSource->status());
        return;
    }

    _server.send(200, "text/plain", _logSource->status());
}

void WiFiTuner::appendDisplayText(const char *text)
{
    if (!text || text[0] == '\0')
        return;

    _displayText += text;

    if (_displayText.length() > DISPLAY_BUFFER_SIZE)
    {
        _displayText.remove(0, _displayText.length() - DISPLAY_BUFFER_SIZE);
    }
}

String WiFiTuner::parameterHTML(const Parameter &p)
{
    String html;
    int index = &p - _parameters;

    html += "<div class='parameter'>";

    if (p.type == DISPLAY_TEXT)
    {
        html += "<div class='label'>";
        html += p.name;
        html += "</div>";
        html += "<pre id='display'></pre>";
        html += "</div>";

        return html;
    }

    if (p.type == BUTTON)
    {
        html += "<button class='button' onclick='pressButton(";
        html += String(index);
        html += ")'>";
        html += p.name;
        html += "</button>";
        html += "</div>";

        return html;
    }

    if (p.type == LOG_CHART || p.type == LOG_RAW)
    {
        html += "<div class='label'>";
        html += p.name;
        html += "</div>";

        if (p.type == LOG_CHART)
        {
            html += "<div class='chart-controls'>";
            html += "<button type='button' class='chart-control' disabled data-chart-action='zoom-out' onclick=\"adjustLogChart('logview";
            html += String(index);
            html += "', 'zoom-out')\">Zoom -</button>";
            html += "<button type='button' class='chart-control' disabled data-chart-action='zoom-in' onclick=\"adjustLogChart('logview";
            html += String(index);
            html += "', 'zoom-in')\">Zoom +</button>";
            html += "<button type='button' class='chart-control' disabled data-chart-action='left' onclick=\"adjustLogChart('logview";
            html += String(index);
            html += "', 'left')\">Left</button>";
            html += "<button type='button' class='chart-control' disabled data-chart-action='right' onclick=\"adjustLogChart('logview";
            html += String(index);
            html += "', 'right')\">Right</button>";
            html += "<button type='button' class='chart-control' disabled data-chart-action='reset' onclick=\"adjustLogChart('logview";
            html += String(index);
            html += "', 'reset')\">Reset</button>";
            html += "<span class='chart-range' id='logrange";
            html += String(index);
            html += "'>Parse saved data to enable chart navigation.</span>";
            html += "</div>";
            html += "<div class='chart-help'>Drag horizontally to pan. Hold Ctrl while scrolling to zoom.</div>";
            html += "<canvas class='log-chart' id='logview";
            html += String(index);
            html += "' data-log-series='";
            html += p.logSeries;
            html += "' data-legend-id='loglegend";
            html += String(index);
            html += "' data-session-stats-id='logstats";
            html += String(index);
            html += "' data-range-id='logrange";
            html += String(index);
            html += "'";
            if (p.hasChartBounds)
            {
                html += " data-chart-min='";
                html += String(p.chartMinimum, 6);
                html += "' data-chart-max='";
                html += String(p.chartMaximum, 6);
                html += "'";
            }
            html += "></canvas>";
            html += "<div class='chart-session-stats' id='logstats";
            html += String(index);
            html += "'>Parse saved data to load last-session min/max.</div>";
            html += "<div class='chart-legend' id='loglegend";
            html += String(index);
            html += "'></div>";
        }
        else
        {
            html += "<pre class='log-raw' id='logview";
            html += String(index);
            html += "' data-log-series='";
            html += p.logSeries;
            html += "'>Press Parse saved data to load these signals.</pre>";
        }

        html += "</div>";
        return html;
    }

    html += "<div class='label'>";
    html += p.name;
    html += "</div>";

    if (p.type == ANGLE)
    {
        const float value = constrain(*(float *)p.value, p.minFloat, p.maxFloat);
        html += "<div class='angle-control'>";
        html += "<svg class='angle-wheel' id='angleWheel";
        html += String(index);
        html += "' data-parameter-id='";
        html += String(index);
        html += "' role='slider' tabindex='0' aria-label='Angle control' aria-valuemin='-180' aria-valuemax='180' aria-valuenow='";
        html += String(value, 1);
        html += "' viewBox='-30 -20 320 340' xmlns='http://www.w3.org/2000/svg'>";
        html += "<circle class='angle-ring' cx='130' cy='130' r='82'/>";
        html += "<path class='angle-tick' d='M130 42V58 M202 130H218 M130 202V218 M42 130H58'/>";
        html += "<text class='angle-mark' x='130' y='29' text-anchor='middle'>0</text>";
        html += "<line class='angle-pointer' id='anglePointer";
        html += String(index);
        html += "' x1='130' y1='130' x2='130' y2='56'/>";
        html += "<path class='angle-arrowhead' id='angleHead";
        html += String(index);
        html += "' d='M130 56 L124 68 L136 68 Z'/>";
        html += "<text class='angle-readout' id='angleReadout";
        html += String(index);
        html += "' x='130' y='86' text-anchor='middle' transform='rotate(-90 130 86)'>0</text>";
        html += "</svg></div>";
    }
    else if (p.type == DROPDOWN)
    {
        double current = dropdownCurrentValue(p);
        bool selectedFound = false;

        html += "<select class='dropdown' id='dropdown";
        html += String(index);
        html += "' onchange='dropdownChanged(";
        html += String(index);
        html += ", this.value)'>";

        int start = 0;

        for (int i = 0; i < p.optionCount; i++)
        {
            int end = p.optionLabels.indexOf('\n', start);

            if (end < 0)
                end = p.optionLabels.length();

            html += "<option value='";
            html += String(i);
            html += "'";

            if (!selectedFound && p.optionValues[i] == current)
            {
                html += " selected";
                selectedFound = true;
            }

            html += ">";
            html += p.optionLabels.substring(start, end);
            html += "</option>";

            start = end + 1;
        }

        html += "</select>";
    }
    else if (p.type == SLIDER)
    {
        float value = *(float *)p.value;

        html += "<div class='control'>";

        html += "<input type='range' id='slider";
        html += String(index);
        html += "' ";
        html += "min='" + String(p.minFloat) + "' ";
        html += "max='" + String(p.maxFloat) + "' ";
        html += "step='" + String(p.stepFloat) + "' ";
        html += "value='" + String(value) + "' ";
        html += "oninput='sliderChanged(";
        html += String(index);
        html += ", this.value)'>";

        html += "<input class='value' type='number' id='value";
        html += String(index);
        html += "' ";
        html += "min='" + String(p.minFloat) + "' ";
        html += "max='" + String(p.maxFloat) + "' ";
        html += "step='" + String(p.stepFloat) + "' ";
        html += "value='" + String(value) + "' ";
        html += "onchange='valueChanged(";
        html += String(index);
        html += ", this.value)'>";

        html += "</div>";
    }
    else if (p.type == INT_SLIDER)
    {
        int value = *(int *)p.value;

        html += "<div class='control'>";

        html += "<input type='range' id='slider";
        html += String(index);
        html += "' ";
        html += "min='" + String(p.minInt) + "' ";
        html += "max='" + String(p.maxInt) + "' ";
        html += "step='" + String(p.stepInt) + "' ";
        html += "value='" + String(value) + "' ";
        html += "oninput='sliderChanged(";
        html += String(index);
        html += ", this.value)'>";

        html += "<input class='value' type='number' id='value";
        html += String(index);
        html += "' ";
        html += "min='" + String(p.minInt) + "' ";
        html += "max='" + String(p.maxInt) + "' ";
        html += "step='" + String(p.stepInt) + "' ";
        html += "value='" + String(value) + "' ";
        html += "onchange='valueChanged(";
        html += String(index);
        html += ", this.value)'>";

        html += "</div>";
    }
    else if (p.type == UINT_SLIDER)
    {
        unsigned int value = *(unsigned int *)p.value;

        html += "<div class='control'>";

        html += "<input type='range' id='slider";
        html += String(index);
        html += "' ";
        html += "min='" + String(p.minUInt) + "' ";
        html += "max='" + String(p.maxUInt) + "' ";
        html += "step='" + String(p.stepUInt) + "' ";
        html += "value='" + String(value) + "' ";
        html += "oninput='sliderChanged(";
        html += String(index);
        html += ", this.value)'>";

        html += "<input class='value' type='number' id='value";
        html += String(index);
        html += "' ";
        html += "min='" + String(p.minUInt) + "' ";
        html += "max='" + String(p.maxUInt) + "' ";
        html += "step='" + String(p.stepUInt) + "' ";
        html += "value='" + String(value) + "' ";
        html += "onchange='valueChanged(";
        html += String(index);
        html += ", this.value)'>";

        html += "</div>";
    }
    else if (p.type == UINT8_SLIDER)
    {
        uint8_t value = *(uint8_t *)p.value;

        html += "<div class='control'>";

        html += "<input type='range' id='slider";
        html += String(index);
        html += "' ";
        html += "min='" + String(p.minUInt8) + "' ";
        html += "max='" + String(p.maxUInt8) + "' ";
        html += "step='" + String(p.stepUInt8) + "' ";
        html += "value='" + String(value) + "' ";
        html += "oninput='sliderChanged(";
        html += String(index);
        html += ", this.value)'>";

        html += "<input class='value' type='number' id='value";
        html += String(index);
        html += "' ";
        html += "min='" + String(p.minUInt8) + "' ";
        html += "max='" + String(p.maxUInt8) + "' ";
        html += "step='" + String(p.stepUInt8) + "' ";
        html += "value='" + String(value) + "' ";
        html += "onchange='valueChanged(";
        html += String(index);
        html += ", this.value)'>";

        html += "</div>";
    }
    else if (p.type == UINT16_SLIDER)
    {
        uint16_t value = *(uint16_t *)p.value;

        html += "<div class='control'>";

        html += "<input type='range' id='slider";
        html += String(index);
        html += "' ";
        html += "min='" + String(p.minUInt16) + "' ";
        html += "max='" + String(p.maxUInt16) + "' ";
        html += "step='" + String(p.stepUInt16) + "' ";
        html += "value='" + String(value) + "' ";
        html += "oninput='sliderChanged(";
        html += String(index);
        html += ", this.value)'>";

        html += "<input class='value' type='number' id='value";
        html += String(index);
        html += "' ";
        html += "min='" + String(p.minUInt16) + "' ";
        html += "max='" + String(p.maxUInt16) + "' ";
        html += "step='" + String(p.stepUInt16) + "' ";
        html += "value='" + String(value) + "' ";
        html += "onchange='valueChanged(";
        html += String(index);
        html += ", this.value)'>";

        html += "</div>";
    }
    else if (p.type == ULONG_SLIDER)
    {
        unsigned long value = *(unsigned long *)p.value;

        html += "<div class='control'>";

        html += "<input type='range' id='slider";
        html += String(index);
        html += "' ";
        html += "min='" + String(p.minULong) + "' ";
        html += "max='" + String(p.maxULong) + "' ";
        html += "step='" + String(p.stepULong) + "' ";
        html += "value='" + String(value) + "' ";
        html += "oninput='sliderChanged(";
        html += String(index);
        html += ", this.value)'>";

        html += "<input class='value' type='number' id='value";
        html += String(index);
        html += "' ";
        html += "min='" + String(p.minULong) + "' ";
        html += "max='" + String(p.maxULong) + "' ";
        html += "step='" + String(p.stepULong) + "' ";
        html += "value='" + String(value) + "' ";
        html += "onchange='valueChanged(";
        html += String(index);
        html += ", this.value)'>";

        html += "</div>";
    }

    html += "</div>";

    return html;
}

String WiFiTuner::generateHTML()
{
    String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Robot Tuner</title>

<style>
body {
    font-family: Arial;
    background: #111;
    color: #eee;
    margin: 20px 70px 20px 20px;
}

h1 {
    margin-bottom: 25px;
}

.category {
    margin-top: 25px;
    margin-bottom: 10px;
    font-size: 22px;
    font-weight: bold;
}

.tabs {
    display: flex;
    gap: 8px;
    margin-bottom: 18px;
    overflow-x: auto;
}

.tab-button {
    flex: 0 0 auto;
    padding: 10px 16px;
    border: 0;
    border-radius: 6px;
    background: #333;
    color: #eee;
    cursor: pointer;
}

.tab-button.active {
    background: #1769aa;
}

.tab-panel {
    display: none;
}

.tab-panel.active {
    display: block;
}

.group {
    margin-bottom: 20px;
}

.parameter {
    background: #222;
    padding: 12px;
    margin: 8px 0;
    border-radius: 8px;
}

.label {
    margin-bottom: 8px;
}

.control {
    display: flex;
    align-items: center;
    gap: 10px;
}

input[type=range] {
    flex: 1;
    touch-action: pan-y;
    pointer-events: none;
}

input[type=range]::-webkit-slider-thumb {
    pointer-events: auto;
}

input[type=range]::-moz-range-thumb {
    pointer-events: auto;
}

.value {
    width: 80px;
    box-sizing: border-box;
}

.angle-control {
    display: flex;
    justify-content: center;
}

.angle-wheel {
    display: block;
    width: min(100%, 320px);
    height: auto;
    overflow: visible;
    touch-action: none;
    user-select: none;
    cursor: grab;
}

.angle-wheel:active {
    cursor: grabbing;
}

.angle-wheel:focus-visible {
    outline: 2px solid #1769aa;
    outline-offset: 3px;
    border-radius: 8px;
}

.angle-ring {
    fill: none;
    stroke: #eee;
    stroke-width: 3;
}

.angle-tick {
    fill: none;
    stroke: #eee;
    stroke-width: 4;
    stroke-linecap: round;
}

.angle-mark {
    fill: #eee;
    font: 18px Arial, sans-serif;
}

.angle-pointer {
    stroke: #fff;
    stroke-width: 3;
    stroke-linecap: round;
}

.angle-arrowhead {
    fill: #fff;
}

.angle-readout {
    fill: #fff;
    font: 16px monospace;
    dominant-baseline: middle;
}

.dropdown {
    width: 100%;
    padding: 6px;
    box-sizing: border-box;
}

.button {
    width: 100%;
    padding: 12px;
    font-size: 16px;
    border: none;
    border-radius: 6px;
    cursor: pointer;
}

.button:disabled {
    opacity: 0.5;
    cursor: not-allowed;
}

.log-toolbar {
    display: flex;
    flex-wrap: wrap;
    gap: 8px;
    align-items: center;
    margin: 12px 0;
}

.log-toolbar .button {
    width: auto;
}

#logStatus {
    flex-basis: 100%;
    color: #bbb;
}

#logStatus.error {
    color: #ff7777;
}

#lastSessionSummary {
    flex-basis: 100%;
    color: #bbb;
}

.log-chart {
    display: block;
    width: 100%;
    height: 260px;
    background: #050505;
    border-radius: 6px;
    cursor: grab;
    touch-action: pan-y;
}

.log-chart.is-panning {
    cursor: grabbing;
}

.chart-controls {
    display: flex;
    flex-wrap: wrap;
    align-items: center;
    gap: 8px;
    margin: 8px 0;
}

.chart-control {
    width: auto;
    padding: 6px 10px;
    background: #252525;
    border: 1px solid #555;
    border-radius: 4px;
    color: #eee;
    cursor: pointer;
}

.chart-control:disabled {
    cursor: default;
    opacity: 0.45;
}

.chart-range {
    flex: 1 1 220px;
    color: #bbb;
    font-size: 12px;
    text-align: right;
}

.chart-help {
    margin: 0 0 6px;
    color: #888;
    font-size: 12px;
}

.chart-legend {
    display: flex;
    flex-wrap: wrap;
    gap: 10px;
    margin: 8px 0 12px;
    color: #bbb;
    font-size: 13px;
}

.chart-session-stats {
    margin: 6px 0;
    color: #bbb;
    font-size: 12px;
}

.chart-legend-item {
    display: inline-flex;
    align-items: center;
    gap: 5px;
}

.chart-legend-swatch {
    width: 12px;
    height: 12px;
    border-radius: 2px;
}

.log-raw {
    background: #050505;
    color: #9cff9c;
    padding: 12px;
    max-height: 420px;
    overflow: auto;
    white-space: pre;
    border-radius: 6px;
    font: 13px monospace;
}

#display {
    background: #050505;
    color: #9cff9c;
    padding: 12px;
    min-height: 120px;
    white-space: pre-wrap;
    overflow-wrap: anywhere;
    border-radius: 8px;
    font: 14px monospace;
}

#notificationOverlay {
    position: fixed;
    inset: 0;
    z-index: 1000;
    display: none;
    align-items: center;
    justify-content: center;
    padding: 24px;
    background: rgba(0, 0, 0, 0.82);
}

#notificationOverlay.visible {
    display: flex;
}

#notificationPanel {
    box-sizing: border-box;
    width: min(100%, 520px);
    padding: 24px;
    border: 1px solid #ffcc66;
    border-radius: 12px;
    background: #222;
    box-shadow: 0 12px 48px #000;
    text-align: center;
}

#notificationMessage {
    margin: 0 0 20px;
    white-space: pre-wrap;
    overflow-wrap: anywhere;
    font-size: 20px;
}

#notificationPanel .button {
    background: #1769aa;
    color: #eee;
}
</style>

<script>
function sendUpdate(id, value) {
    fetch('/update?id=' + id + '&value=' + encodeURIComponent(value));
}

function sliderChanged(id, value) {
    document.getElementById('value' + id).value = value;
    sendUpdate(id, value);
}

function valueChanged(id, value) {
    const slider = document.getElementById('slider' + id);

    if (slider) {
        slider.value = value;
    }

    sendUpdate(id, value);
}

function updateAngleWheel(wheel, value) {
    const numericValue = Number(value);
    let angle = Number.isFinite(numericValue)
        ? Math.max(-180, Math.min(180, Math.round(numericValue * 10) / 10))
        : 0;
    if (angle === 0) angle = 0;

    const id = wheel.dataset.parameterId;
    const pointer = document.getElementById('anglePointer' + id);
    const arrowhead = document.getElementById('angleHead' + id);
    const readout = document.getElementById('angleReadout' + id);
    if (!pointer || !arrowhead || !readout) return;

    const radians = angle * Math.PI / 180;
    const pointerRadius = 74;
    const arrowheadLength = 12;
    const tipX = 130 + Math.sin(radians) * pointerRadius;
    const tipY = 130 - Math.cos(radians) * pointerRadius;
    pointer.setAttribute('x2', tipX);
    pointer.setAttribute('y2', tipY);

    const direction = Math.atan2(tipY - 130, tipX - 130);
    const baseX = tipX - arrowheadLength * Math.cos(direction);
    const baseY = tipY - arrowheadLength * Math.sin(direction);
    const sideX = 5 * Math.sin(direction);
    const sideY = 5 * Math.cos(direction);
    arrowhead.setAttribute(
        'd',
        `M ${tipX} ${tipY} L ${baseX - sideX} ${baseY + sideY} L ${baseX + sideX} ${baseY - sideY} Z`
    );

    const displayValue = String(angle);
    const fontSize = displayValue.length > 4 ? 14 : 16;
    const textWidth = displayValue.length * fontSize * 0.6;
    const labelRadius = Math.max(
        8,
        Math.min(44, pointerRadius - arrowheadLength - textWidth / 2 - 4)
    );
    const textRotation = angle < 0 ? angle + 90 : angle - 90;
    const textDirection = textRotation * Math.PI / 180;
    const labelOffset = 20;
    const labelX = 130 + Math.sin(radians) * labelRadius + Math.sin(textDirection) * labelOffset;
    const labelY = 130 - Math.cos(radians) * labelRadius - Math.cos(textDirection) * labelOffset;
    readout.style.fontSize = fontSize + 'px';
    readout.setAttribute('x', labelX);
    readout.setAttribute('y', labelY);
    readout.setAttribute('text-anchor', 'middle');
    readout.setAttribute('transform', `rotate(${textRotation} ${labelX} ${labelY})`);
    readout.textContent = displayValue;
    wheel.setAttribute('aria-valuenow', String(angle));
    wheel.setAttribute('aria-valuetext', String(angle) + ' degrees');
}

function updateAngleFromPointer(wheel, event, forceSend) {
    const transform = wheel.getScreenCTM();
    if (!transform) return;

    const point = wheel.createSVGPoint();
    point.x = event.clientX;
    point.y = event.clientY;
    const localPoint = point.matrixTransform(transform.inverse());
    const deltaX = localPoint.x - 130;
    const deltaY = 130 - localPoint.y;
    if (Math.hypot(deltaX, deltaY) < 8) return;

    const angle = Math.round(Math.atan2(deltaX, deltaY) * 1800 / Math.PI) / 10;
    updateAngleWheel(wheel, angle);

    const now = Date.now();
    if (forceSend || now - Number(wheel.dataset.lastSentAt || 0) >= 50) {
        sendUpdate(wheel.dataset.parameterId, angle);
        wheel.dataset.lastSentAt = String(now);
    }
}

function initializeAngleWheels() {
    document.querySelectorAll('.angle-wheel').forEach(wheel => {
        updateAngleWheel(wheel, wheel.getAttribute('aria-valuenow'));
        let activePointerId = null;

        wheel.addEventListener('pointerdown', event => {
            if (event.button !== 0) return;
            activePointerId = event.pointerId;
            wheel.setPointerCapture(activePointerId);
            updateAngleFromPointer(wheel, event, true);
            event.preventDefault();
        });

        wheel.addEventListener('pointermove', event => {
            if (event.pointerId !== activePointerId) return;
            updateAngleFromPointer(wheel, event, false);
            event.preventDefault();
        });

        wheel.addEventListener('pointerup', event => {
            if (event.pointerId !== activePointerId) return;
            updateAngleFromPointer(wheel, event, true);
            activePointerId = null;
            if (wheel.hasPointerCapture(event.pointerId)) {
                wheel.releasePointerCapture(event.pointerId);
            }
        });

        wheel.addEventListener('pointercancel', event => {
            if (event.pointerId === activePointerId) activePointerId = null;
        });

        wheel.addEventListener('keydown', event => {
            let angle = Number(wheel.getAttribute('aria-valuenow'));
            if (event.key === 'ArrowUp' || event.key === 'ArrowRight') angle += 1;
            else if (event.key === 'ArrowDown' || event.key === 'ArrowLeft') angle -= 1;
            else if (event.key === 'Home') angle = -180;
            else if (event.key === 'End') angle = 180;
            else return;

            angle = Math.max(-180, Math.min(180, angle));
            updateAngleWheel(wheel, angle);
            sendUpdate(wheel.dataset.parameterId, angle);
            event.preventDefault();
        });
    });
}

function dropdownChanged(id, value) {
    sendUpdate(id, value);
}

let parsedLogHeaders = [];
let parsedLogRows = [];
let lastSessionStats = null;
let lastSessionStatsError = '';
let notificationRefreshFailed = false;
const chartViewStates = new WeakMap();

function showTab(tabId, button) {
    document.querySelectorAll('.tab-panel').forEach(panel => panel.classList.remove('active'));
    document.querySelectorAll('.tab-button').forEach(tabButton => tabButton.classList.remove('active'));
    document.getElementById(tabId).classList.add('active');
    button.classList.add('active');
    if (parsedLogRows.length) {
        renderLogViews();
    }
}

function setLogStatus(message, isError) {
    const status = document.getElementById('logStatus');
    if (!status) return;
    status.textContent = message;
    status.classList.toggle('error', isError);
}

function formatSessionBytes(bytes) {
    const exactBytes = Math.round(bytes);
    if (exactBytes < 1024) return exactBytes.toLocaleString() + ' bytes';
    return exactBytes.toLocaleString() + ' bytes (' +
        (exactBytes / 1024).toFixed(2) + ' KiB)';
}

function renderLastSessionSummary() {
    const summary = document.getElementById('lastSessionSummary');
    if (!summary) return;

    if (!lastSessionStats) {
        summary.textContent = lastSessionStatsError
            ? 'Last-session statistics unavailable: ' + lastSessionStatsError
            : 'Last-session statistics have not been loaded.';
        return;
    }
    if (!lastSessionStats.hasSession) {
        summary.textContent = 'No completed robot session is available.';
        return;
    }

    const bytesSaved = Number(lastSessionStats.bytesSaved);
    const flushCount = Number(lastSessionStats.flushCount);
    summary.textContent = 'Last session: ' + formatSessionBytes(bytesSaved) +
        ' of sample data saved; ' + flushCount + ' successful buffer ' +
        (flushCount === 1 ? 'flush.' : 'flushes.');
}

function renderChartSessionStats(canvas) {
    const summary = document.getElementById(canvas.dataset.sessionStatsId);
    if (!summary) return;

    if (!lastSessionStats) {
        summary.textContent = lastSessionStatsError
            ? 'Last-session min/max unavailable: ' + lastSessionStatsError
            : 'Parse saved data to load last-session min/max.';
        return;
    }
    if (!lastSessionStats.hasSession) {
        summary.textContent = 'No completed robot session is available.';
        return;
    }

    const signalRanges = lastSessionStats.signals || {};
    const ranges = canvas.dataset.logSeries.split(',').filter(Boolean).map(name => {
        const range = signalRanges[name];
        if (!range || !Number.isFinite(Number(range.min)) ||
            !Number.isFinite(Number(range.max))) {
            return name + ': no saved samples';
        }
        return name + ': min ' + Number(range.min).toFixed(3) +
            ', max ' + Number(range.max).toFixed(3);
    });
    summary.textContent = 'Last session - ' + ranges.join('; ');
}

function updateSessionStatsViews() {
    renderLastSessionSummary();
    document.querySelectorAll('canvas[data-session-stats-id]')
        .forEach(renderChartSessionStats);
}

function refreshLastSessionStats() {
    return fetch('/logs/stats', { cache: 'no-store' })
        .then(response => response.text().then(text => {
            if (!response.ok) throw new Error(text || response.statusText);
            return JSON.parse(text);
        }))
        .then(stats => {
            if (!stats || typeof stats.hasSession !== 'boolean' ||
                !Number.isFinite(Number(stats.bytesSaved)) ||
                !Number.isFinite(Number(stats.flushCount)) ||
                !stats.signals || typeof stats.signals !== 'object') {
                throw new Error('Invalid last-session statistics response.');
            }
            lastSessionStats = stats;
            lastSessionStatsError = '';
            updateSessionStatsViews();
            return '';
        })
        .catch(error => {
            lastSessionStats = null;
            lastSessionStatsError = error.message;
            updateSessionStatsViews();
            return error.message;
        });
}

function parseSavedLogs() {
    setLogStatus('Reading saved data from the robot...', false);
    fetch('/logs', { cache: 'no-store' })
        .then(response => response.text().then(text => {
            if (!response.ok) throw new Error(text || response.statusText);
            return { response, text };
        }))
        .then(result => {
            const lines = result.text.split(/\r?\n/);
            while (lines.length && lines[lines.length - 1] === '') lines.pop();
            if (!lines.length) throw new Error('The saved log file is empty.');

            parsedLogHeaders = lines[0].split(',');
            if (parsedLogHeaders[0] !== 'elapsed_ms' && parsedLogHeaders[0] !== 'uptime_ms') {
                throw new Error('Unsupported log file format.');
            }

            parsedLogRows = [];
            let incompleteRows = 0;
            for (let i = 1; i < lines.length; i++) {
                const row = lines[i].split(',');
                if (row.length !== parsedLogHeaders.length) {
                    incompleteRows++;
                    continue;
                }
                parsedLogRows.push(row);
            }

            resetLogChartViews();
            return refreshLastSessionStats().then(statsError => {
                const missing = renderLogViews();
                let message = result.response.headers.get('X-Log-Status') || 'Saved data loaded.';
                message += ' Parsed ' + parsedLogRows.length + ' samples.';
                if (incompleteRows) message += ' Ignored ' + incompleteRows + ' incomplete rows.';
                if (missing.length) message += ' Missing configured columns: ' + missing.join(', ') + '.';
                if (statsError) message += ' Last-session statistics are unavailable.';
                setLogStatus(message, missing.length > 0);
            });
        })
        .catch(error => setLogStatus('Parse failed: ' + error.message, true));
}

const chartColors = ['#4fc3f7', '#ff7043', '#a5d6a7', '#ffd54f', '#ce93d8', '#80cbc4', '#f48fb1', '#b0bec5'];

function chartColor(index) {
    return chartColors[index % chartColors.length];
}

function formatChartValue(value, decimals) {
    return String(Number(value.toFixed(decimals)));
}

function chartSeriesForCanvas(canvas) {
    return canvas.dataset.logSeries.split(',').filter(Boolean).map((name, index) => ({
        name,
        column: parsedLogHeaders.indexOf(name),
        colorIndex: index
    }));
}

function getChartView(canvas) {
    const total = parsedLogRows.length;
    let view = chartViewStates.get(canvas);
    if (!view) {
        view = { start: 0, end: total, drag: null };
        chartViewStates.set(canvas, view);
    }

    const size = Math.max(0, Math.min(total, view.end - view.start));
    view.start = Math.max(0, Math.min(total - size, view.start));
    view.end = view.start + size;
    return view;
}

function resetChartView(canvas) {
    chartViewStates.set(canvas, { start: 0, end: parsedLogRows.length, drag: null });
    canvas.classList.remove('is-panning');
}

function resetLogChartViews() {
    document.querySelectorAll('canvas[data-log-series]').forEach(resetChartView);
}

function setChartView(canvas, start, size) {
    const total = parsedLogRows.length;
    const minimumSize = Math.min(2, total);
    size = Math.max(minimumSize, Math.min(total, Math.round(size)));
    const maxStart = total - size;
    const view = getChartView(canvas);
    view.start = Math.max(0, Math.min(maxStart, Math.round(start)));
    view.end = view.start + size;
    view.drag = null;
    canvas.classList.remove('is-panning');
    redrawChart(canvas);
}

function zoomChart(canvas, zoomIn, anchorRatio) {
    const total = parsedLogRows.length;
    if (total < 2) return;

    const view = getChartView(canvas);
    const currentSize = view.end - view.start;
    const factor = 0.75;
    const nextSize = zoomIn
        ? Math.max(Math.min(2, total), Math.floor(currentSize * factor))
        : Math.min(total, Math.ceil(currentSize / factor));
    const ratio = Math.max(0, Math.min(1, anchorRatio));
    const anchor = view.start + currentSize * ratio;
    setChartView(canvas, anchor - nextSize * ratio, nextSize);
}

function panChart(canvas, direction) {
    const view = getChartView(canvas);
    const size = view.end - view.start;
    const amount = Math.max(1, Math.round(size * 0.5));
    setChartView(canvas, view.start + direction * amount, size);
}

function adjustLogChart(canvasId, action) {
    const canvas = document.getElementById(canvasId);
    if (!canvas || !parsedLogRows.length) return;

    if (action === 'zoom-in') {
        zoomChart(canvas, true, 0.5);
    } else if (action === 'zoom-out') {
        zoomChart(canvas, false, 0.5);
    } else if (action === 'left') {
        panChart(canvas, -1);
    } else if (action === 'right') {
        panChart(canvas, 1);
    } else if (action === 'reset') {
        resetChartView(canvas);
        redrawChart(canvas);
    }
}

function setupChartInteractions(canvas) {
    if (canvas.dataset.chartInteractionsReady === 'true') return;
    canvas.dataset.chartInteractionsReady = 'true';

    canvas.addEventListener('wheel', event => {
        if (!event.ctrlKey || parsedLogRows.length < 2) return;
        event.preventDefault();
        const bounds = canvas.getBoundingClientRect();
        const plotWidth = Math.max(1, bounds.width - 60);
        const anchorRatio = (event.clientX - bounds.left - 48) / plotWidth;
        zoomChart(canvas, event.deltaY < 0, anchorRatio);
    }, { passive: false });

    canvas.addEventListener('pointerdown', event => {
        if (event.button !== 0 || !parsedLogRows.length) return;
        const view = getChartView(canvas);
        if (view.end - view.start >= parsedLogRows.length) return;
        view.drag = {
            pointerId: event.pointerId,
            clientX: event.clientX,
            start: view.start,
            end: view.end
        };
        canvas.setPointerCapture(event.pointerId);
        canvas.classList.add('is-panning');
        event.preventDefault();
    });

    canvas.addEventListener('pointermove', event => {
        const view = chartViewStates.get(canvas);
        if (!view || !view.drag || view.drag.pointerId !== event.pointerId) return;
        const bounds = canvas.getBoundingClientRect();
        const plotWidth = Math.max(1, bounds.width - 60);
        const size = view.drag.end - view.drag.start;
        const shift = Math.round((view.drag.clientX - event.clientX) * size / plotWidth);
        view.start = Math.max(0, Math.min(parsedLogRows.length - size, view.drag.start + shift));
        view.end = view.start + size;
        redrawChart(canvas);
    });

    const finishPan = event => {
        const view = chartViewStates.get(canvas);
        if (!view || !view.drag || view.drag.pointerId !== event.pointerId) return;
        view.drag = null;
        canvas.classList.remove('is-panning');
    };
    canvas.addEventListener('pointerup', finishPan);
    canvas.addEventListener('pointercancel', finishPan);
    canvas.addEventListener('lostpointercapture', finishPan);
}

function updateChartControls(canvas, view) {
    const total = parsedLogRows.length;
    const size = view.end - view.start;
    const fullRange = view.start === 0 && view.end === total;
    canvas.parentElement.querySelectorAll('.chart-control').forEach(button => {
        const action = button.dataset.chartAction;
        button.disabled = total === 0
            || (action === 'zoom-in' && size <= Math.min(2, total))
            || (action === 'zoom-out' && fullRange)
            || (action === 'left' && view.start === 0)
            || (action === 'right' && view.end === total)
            || (action === 'reset' && fullRange);
    });

    const range = document.getElementById(canvas.dataset.rangeId);
    if (!range) return;
    if (!total || !size) {
        range.textContent = 'Parse saved data to enable chart navigation.';
        return;
    }
    const firstTime = parsedLogRows[view.start][0];
    const lastTime = parsedLogRows[view.end - 1][0];
    range.textContent = 'Samples ' + (view.start + 1) + '-' + view.end + ' of ' + total
        + ' (' + firstTime + '-' + lastTime + ' ms)';
}

function redrawChart(canvas) {
    const series = chartSeriesForCanvas(canvas).filter(signal => signal.column >= 0);
    drawChart(canvas, series);
}

function renderLogViews() {
    const missing = [];
    document.querySelectorAll('canvas[data-log-series]').forEach(canvas => {
        const series = chartSeriesForCanvas(canvas);
        series.forEach(signal => {
            if (signal.column < 0 && missing.indexOf(signal.name) < 0) {
                missing.push(signal.name);
            }
        });
        const available = series.filter(signal => signal.column >= 0);
        setupChartInteractions(canvas);
        drawChart(canvas, available);
        renderChartLegend(canvas, available);
        renderChartSessionStats(canvas);
    });

    document.querySelectorAll('pre[data-log-series]').forEach(view => {
        const signals = view.dataset.logSeries.split(',').filter(Boolean);
        const columns = signals.map(name => {
            const column = parsedLogHeaders.indexOf(name);
            if (column < 0 && missing.indexOf(name) < 0) {
                missing.push(name);
            }
            return { name, column };
        });
        const missingSignals = columns.filter(signal => signal.column < 0);

        if (missingSignals.length) {
            view.textContent = 'Missing saved columns: ' +
                missingSignals.map(signal => signal.name).join(', ') + '.';
            return;
        }

        view.textContent = parsedLogRows
            .map(row => row[0] + 'ms - ' +
                columns.map(signal => row[signal.column]).join(', '))
            .join('\n') || 'No samples recorded.';
    });
    return missing;
}

function renderChartLegend(canvas, series) {
    const legend = document.getElementById(canvas.dataset.legendId);
    if (!legend) return;
    legend.textContent = '';
    series.forEach(signal => {
        const item = document.createElement('span');
        item.className = 'chart-legend-item';
        const swatch = document.createElement('span');
        swatch.className = 'chart-legend-swatch';
        swatch.style.backgroundColor = chartColor(signal.colorIndex);
        const label = document.createElement('span');
        label.textContent = signal.name;
        item.append(swatch, label);
        legend.append(item);
    });
}

function drawChart(canvas, series) {
    const view = getChartView(canvas);
    updateChartControls(canvas, view);
    const width = Math.max(300, canvas.clientWidth || 300);
    const height = 260;
    const scale = window.devicePixelRatio || 1;
    canvas.width = width * scale;
    canvas.height = height * scale;
    const context = canvas.getContext('2d');
    if (!context) return;
    context.scale(scale, scale);
    context.fillStyle = '#050505';
    context.fillRect(0, 0, width, height);

    const visibleRows = parsedLogRows.slice(view.start, view.end);
    if (!series.length) {
        context.fillStyle = '#bbb';
        context.fillText('No configured series are present in the saved log.', 12, 24);
        return;
    }

    let minimum = Infinity;
    let maximum = -Infinity;
    visibleRows.forEach(row => {
        series.forEach(signal => {
            const value = Number(row[signal.column]);
            if (!Number.isFinite(value)) return;
            minimum = Math.min(minimum, value);
            maximum = Math.max(maximum, value);
        });
    });

    if (minimum === Infinity) {
        context.fillStyle = '#bbb';
        context.fillText('No numeric samples.', 12, 24);
        return;
    }

    const hasFixedRange = canvas.dataset.chartMin !== undefined &&
        canvas.dataset.chartMax !== undefined;
    if (hasFixedRange) {
        const chartMinimum = Number(canvas.dataset.chartMin);
        const chartMaximum = Number(canvas.dataset.chartMax);
        if (!Number.isFinite(chartMinimum) || !Number.isFinite(chartMaximum) ||
            chartMinimum >= chartMaximum) {
            context.fillStyle = '#ff7777';
            context.fillText('Invalid configured Y-axis range.', 12, 24);
            return;
        }
        minimum = chartMinimum;
        maximum = chartMaximum;
    } else if (minimum === maximum) {
        const padding = Math.abs(minimum) * 0.05 || 1;
        minimum -= padding;
        maximum += padding;
    }

    const left = 72;
    const right = width - 12;
    const top = 12;
    const bottom = height - 42;
    const plotWidth = right - left;
    const plotHeight = bottom - top;
    const timestamps = visibleRows.map(row => Number(row[0]));
    const timestampsAreMonotonic = timestamps.every((timestamp, index) =>
        Number.isFinite(timestamp) && (index === 0 || timestamp >= timestamps[index - 1]));
    const useTimeScale = timestampsAreMonotonic &&
        timestamps.length > 1 && timestamps[timestamps.length - 1] > timestamps[0];
    const xForIndex = index => {
        if (useTimeScale) {
            return left +
                (timestamps[index] - timestamps[0]) /
                (timestamps[timestamps.length - 1] - timestamps[0]) * plotWidth;
        }
        return visibleRows.length > 1
            ? left + index * plotWidth / (visibleRows.length - 1)
            : (left + right) / 2;
    };

    context.font = '11px monospace';
    context.lineWidth = 1;
    const tickDecimals = Math.max(3, Math.min(6, Math.ceil(-Math.log10((maximum - minimum) / 4))));
    for (let tick = 0; tick <= 4; tick++) {
        const fraction = tick / 4;
        const y = bottom - fraction * plotHeight;
        const value = minimum + fraction * (maximum - minimum);
        context.strokeStyle = '#303030';
        context.beginPath();
        context.moveTo(left, y);
        context.lineTo(right, y);
        context.stroke();
        context.fillStyle = '#bbb';
        context.textAlign = 'right';
        context.fillText(formatChartValue(value, tickDecimals), left - 8, y + 4);
    }

    const xTickCount = visibleRows.length > 1 ? 2 : 0;
    for (let tick = 0; tick <= xTickCount; tick++) {
        const fraction = xTickCount ? tick / xTickCount : 0.5;
        const index = xTickCount
            ? Math.round(fraction * (visibleRows.length - 1))
            : 0;
        const x = xForIndex(index);
        context.strokeStyle = '#252525';
        context.beginPath();
        context.moveTo(x, top);
        context.lineTo(x, bottom);
        context.stroke();
        context.fillStyle = '#bbb';
        context.textAlign = xTickCount === 0
            ? 'center'
            : tick === 0
                ? 'left'
                : tick === xTickCount
                    ? 'right'
                    : 'center';
        const label = timestampsAreMonotonic
            ? Math.round(timestamps[index]) + 'ms'
            : 'sample ' + (view.start + index + 1);
        context.fillText(label, x, height - 8);
    }

    context.strokeStyle = '#777';
    context.beginPath();
    context.moveTo(left, top);
    context.lineTo(left, bottom);
    context.lineTo(right, bottom);
    context.stroke();

    context.lineWidth = 2;
    context.lineJoin = 'round';
    context.lineCap = 'round';
    context.save();
    context.beginPath();
    context.rect(left, top, plotWidth, plotHeight);
    context.clip();
    series.forEach(signal => {
        context.strokeStyle = chartColor(signal.colorIndex);
        context.beginPath();
        let drawing = false;
        visibleRows.forEach((row, index) => {
            const value = Number(row[signal.column]);
            if (!Number.isFinite(value)) {
                drawing = false;
                return;
            }
            const x = xForIndex(index);
            const y = bottom - (value - minimum) * plotHeight / (maximum - minimum);
            if (drawing) context.lineTo(x, y);
            else context.moveTo(x, y);
            drawing = true;
        });
        context.stroke();
    });
    context.restore();
}

function clearSavedLogs() {
    if (!window.confirm('Clear all saved robot logs? If LittleFS cannot mount, this will format the log partition.')) return;
    setLogStatus('Clearing saved data...', false);
    fetch('/logs/clear', { method: 'POST' })
        .then(response => response.text().then(text => {
            if (!response.ok) throw new Error(text || response.statusText);
            return text;
        }))
        .then(message => {
            parsedLogHeaders = [];
            parsedLogRows = [];
            document.querySelectorAll('canvas[data-log-series]').forEach(canvas => {
                resetChartView(canvas);
                drawChart(canvas, []);
                renderChartLegend(canvas, []);
            });
            document.querySelectorAll('pre[data-log-series]').forEach(view => {
                view.textContent = 'Press Parse saved data to load these signals.';
            });
            return refreshLastSessionStats().then(() => message);
        })
        .then(message => setLogStatus(message, false))
        .catch(error => setLogStatus('Clear failed: ' + error.message, true));
}

function pressButton(id) {
    fetch('/button?id=' + id);
}

function cancelNotification() {
    fetch('/notification/cancel', { cache: 'no-store' })
        .then(response => {
            if (!response.ok) throw new Error(response.statusText || `HTTP ${response.status}`);
        })
        .catch(error => console.error('Failed to dismiss tuner notification:', error));
}

function refreshNotification() {
    fetch('/notification', { cache: 'no-store' })
        .then(response => {
            if (!response.ok) throw new Error(response.statusText || `HTTP ${response.status}`);
            return response.text();
        })
        .then(message => {
            notificationRefreshFailed = false;
            const overlay = document.getElementById('notificationOverlay');
            const messageElement = document.getElementById('notificationMessage');

            if (!overlay || !messageElement) {
                return;
            }

            messageElement.textContent = message;
            overlay.classList.toggle('visible', message.length > 0);
        })
        .catch(error => {
            if (!notificationRefreshFailed) {
                console.error('Failed to refresh tuner notification:', error);
                notificationRefreshFailed = true;
            }
        });
}

function refreshDisplay() {
    fetch('/display', { cache: 'no-store' })
        .then(response => response.text())
        .then(text => {
            const display = document.getElementById('display');

            if (display) {
                display.textContent = text;
            }
        })
        .catch(() => {});
}

setInterval(refreshDisplay, 250);
setInterval(refreshNotification, 100);
setInterval(refreshLastSessionStats, 1000);
window.addEventListener('load', () => {
    refreshDisplay();
    refreshNotification();
    refreshLastSessionStats();
    initializeAngleWheels();

});
window.addEventListener('resize', () => {
    if (!parsedLogHeaders.length) return;
    document.querySelectorAll('canvas[data-log-series]').forEach(redrawChart);
});
</script>

</head>
<body>

<h1>Robot Tuner</h1>
<div id="notificationOverlay" role="dialog" aria-modal="true" aria-labelledby="notificationMessage">
    <div id="notificationPanel">
        <p id="notificationMessage" aria-live="assertive"></p>
        <button class="button" type="button" onclick="cancelNotification()">Dismiss</button>
    </div>
</div>
)rawliteral";

    String tabNames[MAX_PARAMETERS];
    int tabCount = 0;
    for (int i = 0; i < _parameterCount; ++i)
    {
        const String tabName = _parameters[i].tab.length() ? _parameters[i].tab : String("Main");
        bool alreadyAdded = false;
        for (int j = 0; j < tabCount; ++j)
        {
            if (tabNames[j] == tabName)
            {
                alreadyAdded = true;
                break;
            }
        }

        if (!alreadyAdded)
            tabNames[tabCount++] = tabName;
    }

    if (tabCount == 0)
        tabNames[tabCount++] = _currentTab.length() ? _currentTab : String("Main");

    html += "<div class='tabs' role='tablist'>";
    for (int i = 0; i < tabCount; ++i)
    {
        html += "<button type='button' class='tab-button";
        if (i == 0)
            html += " active";
        html += "' onclick=\"showTab('tab";
        html += String(i);
        html += "', this)\">";
        html += tabNames[i];
        html += "</button>";
    }
    html += "</div>";

    String logTab;
    for (int i = 0; i < _parameterCount; ++i)
    {
        if (_parameters[i].type == LOG_CHART || _parameters[i].type == LOG_RAW)
        {
            logTab = _parameters[i].tab;
            break;
        }
    }

    for (int tabIndex = 0; tabIndex < tabCount; ++tabIndex)
    {
        html += "<div class='tab-panel";
        if (tabIndex == 0)
            html += " active";
        html += "' id='tab";
        html += String(tabIndex);
        html += "'>";

        if (!_hasDisplay && tabIndex == 0)
        {
            html += "<div class='category'>Display</div>";
            html += "<div class='group'><div class='parameter'><pre id='display'></pre></div></div>";
        }

        if (_logSource && tabNames[tabIndex] == logTab)
        {
            html += "<div class='log-toolbar'>";
            html += "<button class='button' type='button' onclick='parseSavedLogs()'>Parse saved data</button>";
            html += "<button class='button' type='button' onclick='clearSavedLogs()'>Clear flash data</button>";
            html += "<span id='logStatus'>Press Parse saved data to read the flash log.</span>";
            html += "<span id='lastSessionSummary'>Loading last-session statistics...</span>";
            html += "</div>";
        }

        String lastCategory;
        bool groupOpen = false;
        for (int i = 0; i < _parameterCount; ++i)
        {
            Parameter &p = _parameters[i];
            if (p.tab != tabNames[tabIndex])
                continue;

            if (p.category != lastCategory)
            {
                if (groupOpen)
                {
                    html += "</div>";
                    groupOpen = false;
                }

                if (p.category.length())
                {
                    html += "<div class='category'>";
                    html += p.category;
                    html += "</div><div class='group'>";
                    groupOpen = true;
                }

                lastCategory = p.category;
            }

            html += parameterHTML(p);
        }

        if (groupOpen)
            html += "</div>";
        html += "</div>";
    }

    html += "</body></html>";

    return html;
}