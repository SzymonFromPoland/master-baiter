#include <remote.h>

Remote::Remote(const uint8_t ir_pin, uint16_t max_listeners)
    : ir_pin(ir_pin),
      irrecv(ir_pin),
      nec_count(0),
      max_listeners(max_listeners),
      start_command(0x00),
      stop_command(0x00),
      has_last_valid_nec_results(false),
      held_listener(-1),
      held_started_at(0)
{
    nec_listeners = new NECListener[max_listeners];
}

Remote::~Remote()
{
    delete[] nec_listeners;
}

void Remote::begin()
{
    irrecv.enableIRIn();
}

void Remote::addNECListener(uint8_t address, uint8_t command, Callback onChange, bool repeatable)
{
    if (onChange != nullptr && nec_count < max_listeners)
        nec_listeners[nec_count++] = {address, command, onChange, repeatable};
}

void Remote::addPROGListener(Callback onChange)
{
    if (onChange != nullptr)
        prog_listener = {onChange};
}

void Remote::addSTARTListener(Callback onChange)
{
    if (onChange != nullptr)
        start_listener = {onChange};
}

void Remote::addSTOPListener(Callback onChange)
{
    if (onChange != nullptr)
        stop_listener = {onChange};
}

void Remote::handle()
{
    if (irrecv.decode(&results))
    {

        // Serial.printf("p: %u a: %u c: %u\n", results.decode_type, results.address, results.command);

        if (results.decode_type == NEC)
        {
            if (results.repeat)
            {
                constexpr uint32_t repeat_delay_us = 300000;
                constexpr uint32_t acceleration_interval_us = 500000;
                constexpr uint8_t maximum_repeat_multiplier = 5;
                const uint32_t held_for = micros() - held_started_at;

                if (has_last_valid_nec_results && held_listener >= 0 && nec_listeners[held_listener].repeatable && held_for >= repeat_delay_us)
                {
                    decode_results repeated_results = last_valid_nec_results;
                    repeated_results.repeat = true;
                    const uint32_t acceleration = (held_for - repeat_delay_us) / acceleration_interval_us;
                    const uint8_t multiplier = min<uint32_t>(1 + acceleration, maximum_repeat_multiplier);
                    for (uint8_t i = 0; i < multiplier; ++i)
                        nec_listeners[held_listener].callback(repeated_results);
                }
            }
            else
            {
                last_valid_nec_results = results;
                has_last_valid_nec_results = true;
                held_listener = -1;
                held_started_at = micros();

                for (uint16_t i = 0; i < nec_count; i++)
                {
                    if (nec_listeners[i].address == results.address && nec_listeners[i].command == results.command)
                    {
                        held_listener = i;
                        nec_listeners[i].callback(results);
                        break;
                    }
                }
            }
        }
        else if (results.decode_type == RC5)
        {
            if (results.address == 0x0B)
            {
                start_command = results.command + 1;
                stop_command = results.command;

                prog_listener.callback(results);
            }
            else if (results.address == 0x07)
            {
                if (results.command == start_command)
                {
                    start_listener.callback(results);
                }
                else if (results.command == stop_command)
                {
                    stop_listener.callback(results);
                }
            }
        }
        irrecv.resume();
    }
}

void Remote::set_start_command(uint8_t command)
{
    start_command = command;
}

void Remote::set_stop_command(uint8_t command)
{
    stop_command = command;
}