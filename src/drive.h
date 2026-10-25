#pragma once
#include <Arduino.h>

extern const uint8_t tb_pins[10];

class Drive
{
public:
    void begin();
    void setSpeed(int speed_left, int speed_right);
};