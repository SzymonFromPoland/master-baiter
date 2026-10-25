#pragma once

#include <Arduino.h>
float get_dt(unsigned long now);

class PID
{
public:
    PID(
        float kp = 0.0f,
        float ki = 0.0f,
        float kd = 0.0f,
        float outputLimit = 1000.0f,
        float integralLimit = 1000.0f,
        float derivativeAlpha = 1.0f,
        float minDt = 0.002f);

    float compute(float error, float dt);

    void reset();

    void setGains(float kp, float ki, float kd);
    void setOutputLimit(float limit);
    void setIntegralLimit(float limit);
    void setDerivativeAlpha(float alpha);

    float getKp() const;
    float getKi() const;
    float getKd() const;

    float getIntegral() const;
    float getDerivative() const;
    float getPreviousError() const;

private:
    float Kp;
    float Ki;
    float Kd;

    float outputLimit;
    float integralLimit;

    float derivativeAlpha;
    float minDt;

    float previousError;
    float previousDerivative;
    float integral;
    float derivative;
};
