#pragma once

#include <Arduino.h>
#include <ESP32Servo.h>

class ServoControl
{
public:
    void begin(uint8_t pin, int midpoint = 95, unsigned long rotation_time_ms = 180);
    void down();
    void test();
    void move(int command);
    void stop();
    void handle(bool started);

    bool servo_down = false;

private:
    Servo actuator;
    int midpoint = 95;
    unsigned long rotation_time_us = 180000;
    unsigned long movement_started_us = 0;
    bool attached = false;
    bool moving = false;
    bool testing = false;
};
