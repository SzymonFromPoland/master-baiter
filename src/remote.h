#pragma once
#include <Arduino.h>
#include <IRrecv.h>
#include <IRutils.h>

class Remote
{
public:
    using Callback = void (*)(decode_results);

    Remote(const uint8_t ir_pin, uint16_t max_listeners);
    ~Remote();

    void begin();
    void handle();

    void addNECListener(uint8_t address, uint8_t command, Callback onChange = nullptr, bool repeatable = false);

    void addPROGListener(Callback onChange = nullptr);
    void addSTARTListener(Callback onChange = nullptr);
    void addSTOPListener(Callback onChange = nullptr);

    void set_start_command(uint8_t command);
    void set_stop_command(uint8_t command);

private:
    uint8_t ir_pin;

    uint8_t start_command;
    uint8_t stop_command;

    IRrecv irrecv;
    decode_results results;
    decode_results last_valid_nec_results;
    bool has_last_valid_nec_results;
    int16_t held_listener;
    uint32_t held_started_at;

    struct NECListener
    {
        uint8_t address;
        uint8_t command;
        Callback callback;
        bool repeatable;
    };

    struct Listener
    {
        Callback callback;
    };

    NECListener *nec_listeners;
    uint16_t nec_count;
    uint16_t max_listeners;

    Listener prog_listener;
    Listener start_listener;
    Listener stop_listener;
};