#include <something.h>

unsigned long servo_start_time = 0;
volatile int servo_direction = 0;
volatile bool servo_toggle = true;
const int servo_stop_signal = 95;
const int servo_speed = 100;
bool servo_active = false;

unsigned long weights_start_time = 0;
bool weights_active = false;
int weights_pos = 0;

bool init_sensor(VL53L1X_ULD &sensor, uint8_t address, uint8_t xshut)
{
    digitalWrite(xshut, HIGH);
    delay(50);
    if (sensor.Begin(address) != VL53L1_ERROR_NONE)
    {
        Serial.printf("Sensor at 0x%X failed to init\n", address);
        return 0;
    }
    sensor.SetI2CAddress(address);
    delay(10);
    return 1;
}

void set_sensor_settings(VL53L1X_ULD &sensor, EDistanceMode mode, uint16_t roi[2], uint16_t timing_budget, uint16_t inter_measurement, uint16_t threshold)
{
    sensor.SetDistanceMode(mode);
    sensor.SetROI(roi[0], roi[1]);
    sensor.SetTimingBudgetInMs(timing_budget);
    sensor.SetInterMeasurementInMs(inter_measurement);
    sensor.SetInterruptPolarity(ActiveLOW);
    sensor.SetDistanceThreshold(0, threshold, Out);
    sensor.StartRanging();
}

void weights(int speed2, int speed1)
{
    speed1 = constrain(speed1, -255, 255);
    speed2 = constrain(speed2, -255, 255);

    digitalWrite(tb_pins[6], speed1 >= 0 ? LOW : HIGH);
    digitalWrite(tb_pins[7], speed2 >= 0 ? HIGH : LOW);
    analogWrite(tb_pins[8], abs(speed1));
    analogWrite(tb_pins[9], abs(speed2));
}

void handle_servo(unsigned long now)
{
    if (servo_direction != 0 && servo_toggle && !servo_active)
    {
        servo_active = true;
        servo_start_time = now;
        if (servo_direction == 1)
            flag.write(90 + servo_speed);
        else if (servo_direction == -1)
            flag.write(90 - servo_speed);
        servo_toggle = false;
    }

    if (servo_active && (now - servo_start_time >= 140))
    {
        flag.write(servo_stop_signal);
        servo_active = false;
    }
}

void handle_weights(unsigned long now)
{
    static bool weights_toggle = false;
    if (weights_pos == 0 && weights_toggle && !weights_active)
    {
        weights_active = true;
        weights_start_time = now;
        weights(-255, -255);
        weights_toggle = false;
    }
    else if (weights_pos == 1 && !weights_toggle && !weights_active)
    {
        weights_active = true;
        weights_start_time = now;
        weights(255, 255);
        weights_toggle = true;
    }

    if (weights_active && (now - weights_start_time >= 350))
    {
        weights(0, 0);
        weights_active = false;
    }
}



float pid(float error, float dt, float Kp, float Ki, float Kd, PIDState &state, float alpha, float integral_limit)
{
    if (dt < 0.002f)
        dt = 0.002f;

    float P = Kp * error;

    state.integral += error * dt;
    state.integral = constrain(state.integral, -integral_limit, integral_limit);
    float I = Ki * state.integral;
    float raw_derivative = (error - state.prev_error) / dt;
    float derivative = alpha * raw_derivative + (1.0f - alpha) * state.prev_derivative;

    state.prev_error = error;
    state.prev_derivative = derivative;

    return P + I + Kd * derivative;
}
