#include "weight.h"

namespace
{
    constexpr uint8_t leftPwmChannel = 6;
    constexpr uint8_t rightPwmChannel = 7;
    constexpr uint32_t pwmFrequency = 15000;
    constexpr uint8_t pwmResolution = 10;
    constexpr int maxPwm = (1 << pwmResolution) - 1;
    constexpr unsigned long fullTravelDurationUs = 350000UL;
}

void Weight::begin()
{
    ledcSetup(leftPwmChannel, pwmFrequency, pwmResolution);
    ledcSetup(rightPwmChannel, pwmFrequency, pwmResolution);
    ledcAttachPin(tb_pins[8], leftPwmChannel);
    ledcAttachPin(tb_pins[9], rightPwmChannel);
}

void Weight::setPosition(int newPosition)
{
    target_position = constrain(newPosition, 0, 1);
}

void Weight::setSpeed(int speed1, int speed2)
{
    speed1 = constrain(speed1, -maxPwm, maxPwm);
    speed2 = constrain(speed2, -maxPwm, maxPwm);

    digitalWrite(tb_pins[7], speed1 >= 0 ? LOW : HIGH);
    digitalWrite(tb_pins[6], speed2 >= 0 ? HIGH : LOW);
    ledcWrite(rightPwmChannel, abs(speed1));
    ledcWrite(leftPwmChannel, abs(speed2));
}

void Weight::handle(bool started)
{
    if (target_position != last_position && !active)
    {
        active = true;
        weights_start_time = micros();

        if (target_position == 1 && last_position == 0)
        {
            setSpeed(-1023, -1023);
        }
        else if (target_position == 0 && last_position == 1)
        {
            setSpeed(1023, 1023);
        }

        last_position = target_position;
    }

    if (active && (micros() - weights_start_time >= 500000UL))
    {
        current_position = target_position;

        setSpeed(0, 0);
        active = false;
    }
}
