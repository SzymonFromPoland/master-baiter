#pragma once

#include <Arduino.h>
#include <ESP32Servo.h>
#include <VL53L1X_ULD.h>
#include <Adafruit_MPU6050.h>
#include <Preferences.h>
#include <config.h>

struct PIDState
{
    float prev_error = 0;
    float prev_derivative = 0;
    float integral = 0;
};

extern Servo flag;
extern PIDState drivePID;
extern unsigned long servo_start_time;
extern volatile int servo_direction;
extern volatile bool servo_toggle;
extern const int servo_stop_signal;
extern const int servo_speed;
extern bool servo_active;
extern unsigned long weights_start_time;
extern bool weights_active;
extern int weights_pos;

bool init_sensor(VL53L1X_ULD &sensor, uint8_t address, uint8_t xshut);
void set_sensor_settings(VL53L1X_ULD &sensor, EDistanceMode mode, uint16_t roi[2], uint16_t timing_budget, uint16_t inter_measurement, uint16_t threshold);
void handle_servo(unsigned long now);
void handle_weights(unsigned long now);
void weights(int speed2, int speed1);
float pid(float error, float dt, float Kp, float Ki, float Kd, PIDState &state, float alpha, float integral_limit);