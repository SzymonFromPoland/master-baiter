#ifndef DRIVE_H
#define DRIVE_H

#include <Arduino.h>

const int tb_pins[10] = {
    1,  // pwm A
    2,  // A in 2
    38, // A in 1
    16, // B in 1
    15, // B in 2
    7,  // pwm B
    17, // C in 1
    18, // C in 2
    8,  // pwm C1
    21  // pwm C2
};

void drive(int speed_left, int speed_right);

#endif