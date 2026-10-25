#include <pid.h>

float get_dt(unsigned long now)
{
    static unsigned long lastMicros = 0;
    float dt = (now - lastMicros) / 1000000.0f;
    lastMicros = now;

    return dt;
}

PID::PID(
    float kp,
    float ki,
    float kd,
    float outputLimit,
    float integralLimit,
    float derivativeAlpha,
    float minDt)
    : Kp(kp),
      Ki(ki),
      Kd(kd),
      outputLimit(abs(outputLimit)),
      integralLimit(abs(integralLimit)),
      derivativeAlpha(constrain(derivativeAlpha, 0.0f, 1.0f)),
      minDt(minDt),
      previousError(0.0f),
      previousDerivative(0.0f),
      integral(0.0f),
      derivative(0.0f)
{
}

float PID::compute(float error, float dt)
{
    if (dt < minDt)
        dt = minDt;

    float proportional = Kp * error;

    integral += error * dt;
    integral = constrain(integral, -integralLimit, integralLimit);

    float rawDerivative = (error - previousError) / dt;

    derivative = derivativeAlpha * rawDerivative +
                 (1.0f - derivativeAlpha) * previousDerivative;

    previousError = error;
    previousDerivative = derivative;

    float output =
        proportional +
        Ki * integral +
        Kd * derivative;

    return constrain(output, -outputLimit, outputLimit);
}

void PID::reset()
{
    previousError = 0.0f;
    previousDerivative = 0.0f;
    integral = 0.0f;
    derivative = 0.0f;
}

void PID::setGains(float kp, float ki, float kd)
{
    Kp = kp;
    Ki = ki;
    Kd = kd;
}

void PID::setOutputLimit(float limit)
{
    outputLimit = abs(limit);
}

void PID::setIntegralLimit(float limit)
{
    integralLimit = abs(limit);
}

void PID::setDerivativeAlpha(float alpha)
{
    derivativeAlpha = constrain(alpha, 0.0f, 1.0f);
}

float PID::getIntegral() const
{
    return integral;
}

float PID::getDerivative() const
{
    return derivative;
}

float PID::getPreviousError() const
{
    return previousError;
}

float PID::getKp() const
{
    return Kp;
}

float PID::getKi() const
{
    return Ki;
}

float PID::getKd() const
{
    return Kd;
}