#include "tuner.h"

#include <stdlib.h>

WiFiTuner::WiFiTuner()
    : _server(80),
      _parameterCount(0),
      _hasDisplay(false),
      _notificationStartedUs(0),
      _notificationDurationUs(0),
      _notificationActive(false)
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

void WiFiTuner::begin(const char *ssid)
{
    WiFi.mode(WIFI_AP);
    WiFi.softAP(ssid, "0123456789");

    _server.on("/", HTTP_GET, [this]()
               { handleRoot(); });

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

    Serial.println();
    Serial.println("WiFi tuner started");
    Serial.print("SSID: ");
    Serial.println(ssid);
    Serial.print("IP: ");
    Serial.println(WiFi.softAPIP());
}

void WiFiTuner::handle()
{
    notificationActive();
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

void WiFiTuner::addCategory(const char *name)
{
    _currentCategory = name;
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

double WiFiTuner::dropdownCurrentValue(const Parameter &p)
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

void WiFiTuner::handleRoot()
{
    _server.send(200, "text/html", generateHTML());
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

    if (p->type == BUTTON || p->type == DISPLAY_TEXT)
    {
        _server.send(400, "text/plain", "Parameter cannot be updated");
        return;
    }

    if (p->type == DROPDOWN)
    {
        int optionIndex = value.toInt();

        if (optionIndex < 0 || optionIndex >= p->optionCount)
        {
            _server.send(400, "text/plain", "Invalid option");
            return;
        }

        double selected = p->optionValues[optionIndex];

        if (selected != dropdownCurrentValue(*p))
        {
            switch (p->dropdownKind)
            {
            case DD_FLOAT:
            {
                float newValue = (float)selected;
                *(float *)p->value = newValue;

                if (p->floatCallback)
                    p->floatCallback(newValue);
                break;
            }
            case DD_BOOL:
            {
                bool newValue = selected != 0.0;
                *(bool *)p->value = newValue;

                if (p->boolCallback)
                    p->boolCallback(newValue);
                break;
            }
            case DD_INT:
            {
                int newValue = (int)selected;
                *(int *)p->value = newValue;

                if (p->intCallback)
                    p->intCallback(newValue);
                break;
            }
            case DD_UINT8:
            {
                uint8_t newValue = (uint8_t)selected;
                *(uint8_t *)p->value = newValue;

                if (p->uint8Callback)
                    p->uint8Callback(newValue);
                break;
            }
            case DD_UINT16:
            {
                uint16_t newValue = (uint16_t)selected;
                *(uint16_t *)p->value = newValue;

                if (p->uint16Callback)
                    p->uint16Callback(newValue);
                break;
            }
            case DD_UINT:
            {
                unsigned int newValue = (unsigned int)selected;
                *(unsigned int *)p->value = newValue;

                if (p->uintCallback)
                    p->uintCallback(newValue);
                break;
            }
            case DD_ULONG:
            {
                unsigned long newValue = (unsigned long)selected;
                *(unsigned long *)p->value = newValue;

                if (p->ulongCallback)
                    p->ulongCallback(newValue);
                break;
            }
            }
        }
    }
    else if (p->type == INT_SLIDER)
    {
        int newValue = value.toInt();
        newValue = constrain(newValue, p->minInt, p->maxInt);

        int oldValue = *(int *)p->value;

        if (newValue != oldValue)
        {
            *(int *)p->value = newValue;

            if (p->intCallback)
                p->intCallback(newValue);

            if (p->floatCallback)
                p->floatCallback((float)newValue);
        }
    }
    else if (p->type == UINT8_SLIDER)
    {
        unsigned long parsedValue = strtoul(value.c_str(), nullptr, 10);
        uint8_t newValue;

        if (parsedValue < p->minUInt8)
            newValue = p->minUInt8;
        else if (parsedValue > p->maxUInt8)
            newValue = p->maxUInt8;
        else
            newValue = (uint8_t)parsedValue;

        uint8_t oldValue = *(uint8_t *)p->value;

        if (newValue != oldValue)
        {
            *(uint8_t *)p->value = newValue;

            if (p->uint8Callback)
                p->uint8Callback(newValue);
        }
    }
    else if (p->type == UINT16_SLIDER)
    {
        unsigned long parsedValue = strtoul(value.c_str(), nullptr, 10);
        uint16_t newValue;

        if (parsedValue < p->minUInt16)
            newValue = p->minUInt16;
        else if (parsedValue > p->maxUInt16)
            newValue = p->maxUInt16;
        else
            newValue = (uint16_t)parsedValue;

        uint16_t oldValue = *(uint16_t *)p->value;

        if (newValue != oldValue)
        {
            *(uint16_t *)p->value = newValue;

            if (p->uint16Callback)
                p->uint16Callback(newValue);
        }
    }
    else if (p->type == UINT_SLIDER)
    {
        unsigned long parsedValue = strtoul(value.c_str(), nullptr, 10);
        unsigned int newValue;

        if (parsedValue < p->minUInt)
            newValue = p->minUInt;
        else if (parsedValue > p->maxUInt)
            newValue = p->maxUInt;
        else
            newValue = (unsigned int)parsedValue;

        unsigned int oldValue = *(unsigned int *)p->value;

        if (newValue != oldValue)
        {
            *(unsigned int *)p->value = newValue;

            if (p->floatCallback)
                p->floatCallback((float)newValue);

            if (p->uintCallback)
                p->uintCallback(newValue);
        }
    }
    else if (p->type == ULONG_SLIDER)
    {
        unsigned long newValue = strtoul(value.c_str(), nullptr, 10);

        if (newValue < p->minULong)
            newValue = p->minULong;
        else if (newValue > p->maxULong)
            newValue = p->maxULong;

        unsigned long oldValue = *(unsigned long *)p->value;

        if (newValue != oldValue)
        {
            *(unsigned long *)p->value = newValue;

            if (p->floatCallback)
                p->floatCallback((float)newValue);

            if (p->ulongCallback)
                p->ulongCallback(newValue);
        }
    }
    else
    {
        float newValue = value.toFloat();
        newValue = constrain(newValue, p->minFloat, p->maxFloat);

        float oldValue = *(float *)p->value;

        if (newValue != oldValue)
        {
            *(float *)p->value = newValue;

            if (p->floatCallback)
                p->floatCallback(newValue);
        }
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

    html += "<div class='label'>";
    html += p.name;
    html += "</div>";

    if (p.type == DROPDOWN)
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
    cursor: pointer;
    user-select: none;
}

.arrow {
    display: inline-block;
    margin-right: 8px;
    font-size: 16px;
    transition: transform 0.15s;
}

.category.collapsed .arrow {
    transform: rotate(-90deg);
}

.group.hidden {
    display: none;
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

function dropdownChanged(id, value) {
    sendUpdate(id, value);
}

function toggleCategory(header) {
    header.classList.toggle('collapsed');
    header.nextElementSibling.classList.toggle('hidden');
}

function pressButton(id) {
    fetch('/button?id=' + id);
}

function cancelNotification() {
    fetch('/notification/cancel', { cache: 'no-store' });
}

function refreshNotification() {
    fetch('/notification', { cache: 'no-store' })
        .then(response => response.text())
        .then(message => {
            const overlay = document.getElementById('notificationOverlay');
            const messageElement = document.getElementById('notificationMessage');

            if (!overlay || !messageElement) {
                return;
            }

            messageElement.textContent = message;
            overlay.classList.toggle('visible', message.length > 0);
        })
        .catch(() => {});
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
window.addEventListener('load', () => {
    refreshDisplay();
    refreshNotification();
});
</script>

</head>
<body>

<h1>Robot Tuner</h1>
<div id="notificationOverlay">
    <div id="notificationPanel">
        <p id="notificationMessage"></p>
        <button class="button" onclick="cancelNotification()">Dismiss</button>
    </div>
</div>
)rawliteral";

    String lastCategory;
    bool groupOpen = false;

    if (!_hasDisplay)
    {
        html += "<div class='category' onclick='toggleCategory(this)'><span class='arrow'>&#9660;</span>Display</div>";
        html += "<div class='group'>";
        html += "<div class='parameter'>";
        html += "<pre id='display'></pre>";
        html += "</div>";
        html += "</div>";
    }

    for (int i = 0; i < _parameterCount; i++)
    {
        Parameter &p = _parameters[i];

        if (p.category != lastCategory)
        {
            if (groupOpen)
            {
                html += "</div>";
                groupOpen = false;
            }

            if (p.category.length())
            {
                html += "<div class='category' onclick='toggleCategory(this)'><span class='arrow'>&#9660;</span>";
                html += p.category;
                html += "</div>";
                html += "<div class='group'>";
                groupOpen = true;
            }

            lastCategory = p.category;
        }

        html += parameterHTML(p);
    }

    if (groupOpen)
        html += "</div>";

    html += "</body></html>";

    return html;
}