#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>

class Gyro
{
public:
    Gyro();
    bool begin();
    bool read();

    using Callback = void (*)(bool);
    void onCalibrate(Callback onChange);

    void enable();
    void disable();

    // angle deg
    float get_gx() const;
    float get_gy() const;
    float get_gz() const;

    // acceleration deg/s
    float get_ax() const;
    float get_ay() const;
    float get_az() const;

    // callibration
    void callibrate();
    float get_bias() const;

    bool enabled = false;
    bool calibrate_flag = false;

private:
    Adafruit_MPU6050 mpu;

    uint8_t interrupt_pin;

    struct Listener
    {
        Callback callback;
    };

    Listener calibrate_listener;

    float gyroX = 0.0f;
    float gyroY = 0.0f;
    float gyroZ = 0.0f;

    float accelX = 0.0f;
    float accelY = 0.0f;
    float accelZ = 0.0f;

    const int samples = 500;
    int current_sample = 0;
    float sum = 0.0f;
    float bias = 0.0f;
};