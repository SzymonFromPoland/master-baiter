#pragma once

#include <Arduino.h>

extern const uint8_t tb_pins[10];

class Weight
{
public:
    void begin();
    void setPosition(int position);
    void setSpeed(int speed2, int speed1);
    void handle(bool started);

    int current_position = 0;

private:
    int target_position = 0;
    int last_position = 0;
    bool active;
    unsigned long weights_start_time;
};
