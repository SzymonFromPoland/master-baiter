#include <Arduino.h>
#include <Wire.h>
#include <ESP32Servo.h>
#include <VL53L1X_ULD.h>
#include <Adafruit_MPU6050.h>
#include <Preferences.h>

#include "drive.h"
#include "something.h"
#include "config.h"

VL53L1X_ULD sensor[SENSOR_COUNT];
uint16_t distances[SENSOR_COUNT];

Adafruit_MPU6050 mpu;
Preferences prefs;
Servo flag;

float yaw = 0.0f;
float gyro_bias = 0.0f;
unsigned long lastTime = 0;

const uint16_t threshold = 770;
const uint16_t threshold2 = threshold - 150;
const uint16_t threshold3 = 350;

PIDState drivePID;
float Kp = 50.0;
float Ki = 0.0;
float Kd = 20.0;

bool started = false;
bool tornado = false;
bool startup_done = false;
int base_speed = 100;
int last_seen = 1;
long start = 0;

void calibrate_gyro_bias()
{
  int samples = 200;
  float sum = 0;
  sensors_event_t a, g, temp;

  for (int i = 0; i < samples; i++)
  {
    mpu.getEvent(&a, &g, &temp);
    sum += g.gyro.z;
    delay(5);
  }

  gyro_bias = sum / (float)samples;
  Serial.println(gyro_bias);
}

void setup()
{
  Serial.begin(115200);
  Wire.begin(5, 6);

  prefs.begin("robot", false);
  Kp = prefs.getFloat("kp", 50.0);
  Kd = prefs.getFloat("kd", 20.0);
  prefs.end();

  flag.attach(FLAG);

  pinMode(START, INPUT_PULLUP);
  pinMode(DIP1, INPUT_PULLUP);
  pinMode(DIP2, INPUT_PULLUP);

  for (int pin : tb_pins)
    pinMode(pin, OUTPUT);

  pinMode(XSHUT1, OUTPUT);
  pinMode(XSHUT2, OUTPUT);
  pinMode(XSHUT3, OUTPUT);
  pinMode(XSHUT4, OUTPUT);
  pinMode(XSHUT5, OUTPUT);

  digitalWrite(XSHUT1, LOW);
  digitalWrite(XSHUT2, LOW);
  digitalWrite(XSHUT3, LOW);
  digitalWrite(XSHUT4, LOW);
  digitalWrite(XSHUT5, LOW);

  delay(50);

  if (!init_sensor(sensor[0], 0x55, XSHUT1))
    while (1)
      ;
  if (!init_sensor(sensor[1], 0x60, XSHUT2))
    while (1)
      ;
  if (!init_sensor(sensor[2], 0x65, XSHUT3))
    while (1)
      ;
  if (!init_sensor(sensor[3], 0x70, XSHUT4))
    while (1)
      ;
  if (!init_sensor(sensor[4], 0x75, XSHUT5))
    while (1)
      ;

  uint16_t roi[2] = {13, 4};
  set_sensor_settings(sensor[0], Short, roi, 20, 20, threshold);
  set_sensor_settings(sensor[1], Short, roi, 20, 20, threshold);
  set_sensor_settings(sensor[2], Short, roi, 20, 20, threshold);
  set_sensor_settings(sensor[3], Short, roi, 20, 20, threshold);
  set_sensor_settings(sensor[4], Short, roi, 20, 20, threshold);

  if (mpu.begin(0x68))
  {
    mpu.setAccelerometerRange(MPU6050_RANGE_16_G);
    mpu.setGyroRange(MPU6050_RANGE_2000_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_260_HZ);
    mpu.setSampleRateDivisor(0);
    mpu.setHighPassFilter(MPU6050_HIGHPASS_0_63_HZ);

    delay(200);
    calibrate_gyro_bias();
  }

  weights(0, 0);
}

void loop()
{
  unsigned long now = millis();
  float dt = (now - lastTime) / 1000.0;
  lastTime = now;

  started = digitalRead(START) == HIGH;

  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  float rate = (g.gyro.z - gyro_bias) * RAD_TO_DEG;
  yaw += rate * dt;
  yaw = fmod(yaw + 360.0f, 360.0f);

  VL53L1X_Result_t result[SENSOR_COUNT];

  bool any_under_theshold1 = false;
  bool any_under_theshold2 = false;
  bool any_under_theshold3 = false;

  float num = 0.0f;
  float denom = 0.0f;

  for (int i = 0; i < SENSOR_COUNT; i++)
  {
    sensor[i].GetResult(&result[i]);
    distances[i] = (result[i].Status == 0) ? result[i].Distance : threshold;

    if (distances[i] < threshold)
      sensor[i].ClearInterrupt();
    if (distances[i] < threshold)
      any_under_theshold1 = true;
    if (distances[i] < threshold2)
      any_under_theshold2 = true;
    if (distances[i] < threshold3)
      any_under_theshold3 = true;

    float s = 1.0f / (distances[i] + 0.000001f);
    num += s * (i - 2);
    denom += s;
  }
  float error = (denom > 0.0001f) ? (num / denom) : 0.0f;
  float output = pid(error, dt, Kp, Ki, Kd, drivePID, 1.0f, 1000.0f);

  bool dip1 = !digitalRead(DIP1);
  bool dip2 = !digitalRead(DIP2);

  if (error < -0.01f)
    last_seen = -1;
  else if (error > 0.01f)
    last_seen = 1;

  handle_servo(now);
  handle_weights(now);

  if (started)
  {
    if (any_under_theshold1)
      servo_direction = 1;

    if (any_under_theshold2 || millis() - start > 767)
      tornado = true;

    if (dip2 && distances[2] < 30)
      weights_pos = 1;

    if (distances[2] < 40 && startup_done)
    {
      base_speed = 100;
      output = 0;
    }
    else if (any_under_theshold3 && startup_done)
    {
      base_speed = 35;
    }
    else if (any_under_theshold1 && startup_done)
    {
      base_speed = 100;
    }
    else if (tornado && startup_done)
    {
      base_speed = 0;
      output = last_seen * 75;
    }
    else
    {
      if (dip1 && !startup_done)
      {
        if (last_seen == -1)
          drive(-100, 100);
        else if (last_seen == 1)
          drive(100, -100);

        servo_direction = 1;
        
        delay((last_seen == -1) ? 220 : 110);

        drive(100 - last_seen * 50, 100 + last_seen * 50);

        delay(500);

        startup_done = true;
      }
      else
      {
        tornado = true;
        startup_done = true;
      }
    }

    int speed_left = base_speed + output;
    int speed_right = base_speed - output;
    // drive(speed_left, speed_right);
  }
  else
  {
    weights_pos = 0;
    tornado = false;
    startup_done = false;
    start = millis();
    drive(0, 0);
  }

  Serial.printf("%.1fms\t%.1f°\t%d %d\t%d\t%d\t%d\t%d\t%d\t%.1f\n", dt * 1000, yaw, dip1, dip2, distances[0], distances[1], distances[2], distances[3], distances[4], error);
}
