#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <stdarg.h>
#include <initializer_list>

class WiFiTuner
{
public:
    using FloatCallback = void (*)(float);
    using BoolCallback = void (*)(bool);
    using IntCallback = void (*)(int);
    using UInt8Callback = void (*)(uint8_t);
    using UInt16Callback = void (*)(uint16_t);
    using UIntCallback = void (*)(unsigned int);
    using ULongCallback = void (*)(unsigned long);
    using ButtonCallback = void (*)();

    WiFiTuner();
    ~WiFiTuner();

    void begin(const char *ssid = "Robot-Tuner");
    void handle();

    void notify(const char *message, unsigned long durationUs);
    void cancelNotification();
    bool notificationActive();

    size_t printf(const char *format, ...);
    void clearDisplay();

    void addCategory(const char *name);

    void addSlider(
        const char *name,
        float *value,
        float min,
        float max,
        float step = 0.1f,
        FloatCallback onChange = nullptr);

    void addSlider(
        const char *name,
        int *value,
        int min,
        int max,
        int step = 1,
        IntCallback onChange = nullptr);

    void addSlider(
        const char *name,
        uint16_t *value,
        uint32_t min,
        uint32_t max,
        uint32_t step = 1,
        UInt16Callback onChange = nullptr);

    void addSlider(
        const char *name,
        unsigned int *value,
        unsigned int min,
        unsigned int max,
        unsigned int step = 1,
        UIntCallback onChange = nullptr);

    void addSlider(
        const char *name,
        unsigned long *value,
        unsigned long min,
        unsigned long max,
        unsigned long step = 1,
        ULongCallback onChange = nullptr);

    void addSlider(
        const char *name,
        uint8_t *value,
        uint32_t min,
        uint32_t max,
        uint32_t step = 1,
        UInt8Callback onChange = nullptr);

    // Dropdown: labels[i] is shown in the list, values[i] is written to *value
    // when it is picked. The lists are copied, so temporaries are fine, e.g.
    //   tuner.addDropdown("Mode", &mode, {"Slow", "Fast"}, {0, 1}, onMode);
    void addDropdown(
        const char *name,
        float *value,
        std::initializer_list<const char *> labels,
        std::initializer_list<float> values,
        FloatCallback onChange = nullptr);

    void addDropdown(
        const char *name,
        bool *value,
        std::initializer_list<const char *> labels,
        std::initializer_list<bool> values,
        BoolCallback onChange = nullptr);

    void addDropdown(
        const char *name,
        int *value,
        std::initializer_list<const char *> labels,
        std::initializer_list<int> values,
        IntCallback onChange = nullptr);

    void addDropdown(
        const char *name,
        uint16_t *value,
        std::initializer_list<const char *> labels,
        std::initializer_list<uint16_t> values,
        UInt16Callback onChange = nullptr);

    void addDropdown(
        const char *name,
        unsigned int *value,
        std::initializer_list<const char *> labels,
        std::initializer_list<unsigned int> values,
        UIntCallback onChange = nullptr);

    void addDropdown(
        const char *name,
        unsigned long *value,
        std::initializer_list<const char *> labels,
        std::initializer_list<unsigned long> values,
        ULongCallback onChange = nullptr);

    void addDropdown(
        const char *name,
        uint8_t *value,
        std::initializer_list<const char *> labels,
        std::initializer_list<uint8_t> values,
        UInt8Callback onChange = nullptr);

    void addButton(
        const char *name,
        ButtonCallback onPress = nullptr);

    void addDisplay(const char *name = "Display");

private:
    WebServer _server;

    enum ParameterType
    {
        SLIDER,
        INT_SLIDER,
        UINT8_SLIDER,
        UINT16_SLIDER,
        UINT_SLIDER,
        ULONG_SLIDER,
        DROPDOWN,
        BUTTON,
        DISPLAY_TEXT
    };

    enum DropdownKind
    {
        DD_FLOAT,
        DD_BOOL,
        DD_INT,
        DD_UINT8,
        DD_UINT16,
        DD_UINT,
        DD_ULONG
    };

    struct Parameter
    {
        String name;
        String category;

        ParameterType type;

        void *value;

        float minFloat;
        float maxFloat;
        float stepFloat;

        int minInt;
        int maxInt;
        int stepInt;

        uint8_t minUInt8;
        uint8_t maxUInt8;
        uint8_t stepUInt8;

        uint16_t minUInt16;
        uint16_t maxUInt16;
        uint16_t stepUInt16;

        unsigned int minUInt;
        unsigned int maxUInt;
        unsigned int stepUInt;

        unsigned long minULong;
        unsigned long maxULong;
        unsigned long stepULong;

        // Dropdown data
        DropdownKind dropdownKind = DD_INT;
        String optionLabels;               // labels joined by '\n'
        double *optionValues = nullptr;    // heap array, optionCount entries
        int optionCount = 0;

        FloatCallback floatCallback;
        BoolCallback boolCallback;
        IntCallback intCallback;
        UInt8Callback uint8Callback = nullptr;
        UInt16Callback uint16Callback;
        UIntCallback uintCallback;
        ULongCallback ulongCallback;
        ButtonCallback buttonCallback;
    };

    static constexpr int MAX_PARAMETERS = 128;
    static constexpr int DISPLAY_BUFFER_SIZE = 4096;
    static constexpr int DISPLAY_PRINTF_BUFFER_SIZE = 512;

    Parameter _parameters[MAX_PARAMETERS];
    int _parameterCount;

    String _currentCategory;
    String _displayText;
    bool _hasDisplay;
    String _notificationMessage;
    unsigned long _notificationStartedUs;
    unsigned long _notificationDurationUs;
    bool _notificationActive;

    void handleRoot();
    void handleUpdate();
    void handleButton();
    void handleDisplay();
    void handleNotification();

    void appendDisplayText(const char *text);

    template <typename T>
    Parameter *makeDropdown(
        const char *name,
        T *value,
        DropdownKind kind,
        std::initializer_list<const char *> labels,
        std::initializer_list<T> values);

    double dropdownCurrentValue(const Parameter &parameter);

    String generateHTML();
    String parameterHTML(const Parameter &parameter);

    Parameter *findParameter(int index);
};