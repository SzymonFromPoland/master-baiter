#include <gyro.h>

Gyro::Gyro() {}

bool Gyro::begin()
{
    if (!mpu.begin(0x68, &Wire))
        return false;

    mpu.setAccelerometerRange(MPU6050_RANGE_16_G);
    mpu.setGyroRange(MPU6050_RANGE_2000_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_260_HZ);
    mpu.setSampleRateDivisor(0);
    mpu.setHighPassFilter(MPU6050_HIGHPASS_0_63_HZ);

    enable();
    return true;
}

void Gyro::onCalibrate(Callback onChange)
{
    if (onChange != nullptr)
        calibrate_listener = {onChange};
}

void Gyro::enable()
{
    enabled = true;
}

void Gyro::disable()
{

    enabled = false;
}

bool Gyro::read()
{
    sensors_event_t a, g, temp;
    if (!mpu.getEvent(&a, &g, &temp))
        return false;

    gyroX = g.gyro.x;
    gyroY = g.gyro.y;
    gyroZ = g.gyro.z;

    accelX = a.acceleration.x;
    accelY = a.acceleration.y;
    accelZ = a.acceleration.z;

    if (calibrate_flag)
    {
        if (++current_sample > samples)
        {
            bias = sum / samples;
            calibrate_flag = false;
            calibrate_listener.callback(true);
            return true;
        }
        sum += get_gz();
    }

    return true;
}

float Gyro::get_gx() const
{
    return gyroX;
}

float Gyro::get_gy() const
{
    return gyroY;
}

float Gyro::get_gz() const
{
    return gyroZ;
}

float Gyro::get_ax() const
{
    return accelX;
}

float Gyro::get_ay() const
{
    return accelY;
}

float Gyro::get_az() const
{
    return accelZ;
}

float Gyro::get_bias() const
{
    return bias;
}

void Gyro::callibrate()
{
    calibrate_listener.callback(false);
    sum = 0.0f;
    current_sample = 0;
    calibrate_flag = true;
}