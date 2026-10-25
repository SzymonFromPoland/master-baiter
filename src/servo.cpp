#include <servo.h>

void ServoControl::begin(uint8_t pin, int midpoint, unsigned long rotation_time_ms)
{
    this->midpoint = midpoint;
    rotation_time_us = rotation_time_ms * 1000UL;
    actuator.setPeriodHertz(50);
    actuator.attach(pin);
    attached = true;
    stop();
}

void ServoControl::down()
{
    if (!servo_down)
    {
        servo_down = true;
        move(0);
    }
}

void ServoControl::test()
{
    testing = true;
    move(0);
}

void ServoControl::handle(bool started)
{
    if (!attached)
        return;

    if (testing)
    {
        if (micros() - movement_started_us >= rotation_time_us)
        {
            testing = false;
            moving = false;
            stop();
        }
        return;
    }

    if (!started)
    {
        servo_down = false;
        moving = false;
        stop();
        return;
    }

    if (moving && micros() - movement_started_us >= rotation_time_us)
    {
        moving = false;
        stop();
    }
}

void ServoControl::move(int command)
{
    if (!attached)
        return;

    actuator.write(command);
    movement_started_us = micros();
    moving = true;
}

void ServoControl::stop()
{
    if (attached)
        actuator.write(midpoint);
}
