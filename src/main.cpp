#include <Arduino.h>
#include <Wire.h>
#include <ESP32Servo.h>
#include <VL53L1X_ULD.h>
#include <Adafruit_MPU6050.h>
#include <Preferences.h>

#include <tuner.h>
#include <drive.h>
#include <something.h>
#include <config.h>

VL53L1X_ULD sensor[SENSOR_COUNT];
VL53L1X_Result_t results[SENSOR_COUNT];
uint16_t distances[SENSOR_COUNT];
uint16_t spads[SENSOR_COUNT];

Adafruit_MPU6050 mpu;
Preferences prefs;
Servo flag;

bool started = false;
float web_started = false;

float yaw = 0.0f;
float target_yaw = 0.0f;
float gyro_bias = 0.0f;
bool en_gyro = true;
bool target_reached = false;

float base_speed = 100;

PIDState drivePID;
float Kp = 50.0;
float Ki = 0.0;
float Kd = 20.0;

PIDState gyroPID;
float gyroKp = 50.0;
float gyroKi = 0.0;
float gyroKd = 20.0;

float threshold = 770;
float threshold2 = threshold - 200;
float threshold3 = 350;

float error, output, left_speed, right_speed, to_target, gyro_output;
int last_seen = 1;
bool startup_done = false;

void calibrate_gyro_bias()
{
  int samples = 200;
  float sum = 0;
  sensors_event_t a, g, temp;

  for (int i = 0; i < samples; i++)
  {
    mpu.getEvent(&a, &g, &temp);
    sum += g.gyro.z;
  }

  gyro_bias = sum / (float)samples;
}

void setup()
{
  Serial.begin(115200);
  delay(2500);
  Wire.begin(5, 6);

  flag.setPeriodHertz(50);
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

  uint16_t roi[2] = {16, 7}; // 13x4
  set_sensor_settings(sensor[0], Short, roi, 15, 15, threshold);
  set_sensor_settings(sensor[1], Short, roi, 15, 15, threshold);
  set_sensor_settings(sensor[2], Short, roi, 15, 15, threshold);
  set_sensor_settings(sensor[3], Short, roi, 15, 15, threshold);
  set_sensor_settings(sensor[4], Short, roi, 15, 15, threshold);

  // WAZNE INFO!
  // KLAIBRACJA ZAWSZE PO WLACZENIU ROBOTA, NIE TRZYMAC GO W RUCHU PRZEZ PIERWSZE ~3 SEKUNDY, BO INACZEJ ZROBI SIE OGROMNY GYRO BIAS I ROBOT BEDZIE SIE CIAGLE KRECIL W JEDNA STRONE!
  // POLOZYC NA RINGU, WLACZYC I DOPIERO PO ~3 SEKUNDACH COS USTAWIAC!

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

  Serial.println("Done with setup!");

  prefs.begin("robot", false);
  Kp = prefs.getFloat("kp", Kp);
  Ki = prefs.getFloat("ki", Ki);
  Kd = prefs.getFloat("kd", Kd);
  gyroKp = prefs.getFloat("gkp", gyroKp);
  gyroKi = prefs.getFloat("gki", gyroKi);
  gyroKd = prefs.getFloat("gkd", gyroKd);
  target_yaw = prefs.getFloat("tyaw", target_yaw);
  threshold = prefs.getFloat("thres", threshold);
  base_speed = prefs.getFloat("base_speed", base_speed);
  prefs.end();

  static TuningParam mySettings[] = {
      {"ON/OFF", "st", &web_started, 0, 0, 0, TYPE_TOGGLE},
      {"PID Error", "err", &error, 0, 0, 0, TYPE_READONLY},
      {"PID Output", "out", &output, 0, 0, 0, TYPE_READONLY},
      {"Threshold", "thres", &threshold, 0, 1000, 5, TYPE_ARROWS},
      {"Base Speed", "base_speed", &base_speed, 0, 1000, 5, TYPE_ARROWS},
      {"Drive Kp", "kp", &Kp, 0, 100, 0.5, TYPE_ARROWS},
      {"Drive Ki", "ki", &Ki, 0, 100, 0.5, TYPE_ARROWS},
      {"Drive Kd", "kd", &Kd, 0, 100, 0.5, TYPE_ARROWS},
      {"Yaw", "yaw", &yaw, 0, 0, 0, TYPE_READONLY},
      {"Target Yaw", "tyaw", &target_yaw, -180, 180, 1, TYPE_SLIDER},
      {"Gyro Kp", "gkp", &gyroKp, 0, 100, 0.1, TYPE_ARROWS},
      {"Gyro Ki", "gki", &gyroKi, 0, 100, 0.1, TYPE_ARROWS},
      {"Gyro Kd", "gkd", &gyroKd, 0, 100, 0.05, TYPE_ARROWS},

  };

  startTuner(mySettings, sizeof(mySettings) / sizeof(mySettings[0]), results, SENSOR_COUNT, &prefs);

  weights(0, 0);
}

unsigned long targetTime = 0;
unsigned long lastTime = 0;
unsigned long panicTime = 0;

void loop()
{
  unsigned long now = millis();
  float dt = (now - lastTime) / 1000.0;
  lastTime = now;

  started = digitalRead(START) == HIGH;
  bool dip1 = !digitalRead(DIP1);
  bool dip2 = !digitalRead(DIP2);

  bool any_under_theshold1 = false;
  bool any_under_theshold2 = false;
  bool any_under_theshold3 = false;

  if (en_gyro)
  {
    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);
    float rate = (g.gyro.z - gyro_bias) * RAD_TO_DEG;
    yaw += rate * dt;
    yaw = fmod(yaw + 360.0f, 360.0f);
    to_target = fmod((target_yaw - yaw) + 540.0f, 360.0f) - 180.0f;
    gyro_output = pid(to_target, dt, gyroKp, gyroKi, gyroKd, gyroPID, 1.0f, 1000.0f);

    if (abs(to_target) <= 5.0f)
    {
      if (millis() - targetTime >= 50)
        target_reached = true;
    }
    else if (!target_reached)
    {
      targetTime = millis();
      target_reached = false;
    }
  }

  if (target_reached)
    en_gyro = false;

  if (!en_gyro || (!started && !web_started))
  {
    float num = 0.0f;
    float denom = 0.0f;

    for (int i = 0; i < SENSOR_COUNT; i++)
    {
      sensor[i].GetResult(&results[i]);
      distances[i] = (results[i].Status == 0) ? results[i].Distance : threshold;
      spads[i] = results[i].SigPerSPAD;

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

    error = (denom > 0.0001f) ? (num / denom) : 0.0f;
    output = pid(error, dt, Kp, Ki, Kd, drivePID, 1.0f, 1000.0f);

    if ((started) ? error < -0.01f : distances[0] < 100 || distances[1] < 100)
      last_seen = -1;
    else if ((started) ? error > 0.01f : distances[3] < 100 || distances[4] < 100)
      last_seen = 1;
  }

  handle_servo(now);
  handle_weights(now);

  if (started || web_started)
  {
    if (startup_done)
    {
      if (any_under_theshold1)
      {
        left_speed = base_speed + output;
        right_speed = base_speed - output;
      }
      else
      {
        left_speed = base_speed * last_seen;
        right_speed = base_speed * -last_seen;
      }
    }
    else if (dip1)
    {
      // MODE 1 - DIP 1 NA GORZE
      left_speed = 50;
      right_speed = 50;
    }
    else
    {
      // MODE 2 - DIP 1 NA DOLE
      left_speed = -gyro_output;
      right_speed = gyro_output;

      if (target_reached)
      {
        weights_pos = 1;
        servo_pos = 1;  
        left_speed = base_speed + output;
        right_speed = base_speed - output;

        if (any_under_theshold2 || now - panicTime > 500)
          startup_done = true;
      }
      else
      {
        panicTime = now;
      }
    }
  }
  else
  {
    weights_pos = 0;
    servo_pos = 0;
    yaw = 0;
    target_reached = false;
    startup_done = false;
    en_gyro = true;
    left_speed = 0;
    right_speed = 0;
    panicTime = now;
  }

  drive(left_speed, right_speed);

  // Serial.printf("%.1fms\t%.1f°\t%d %d\t%d\t%d\t%d\t%d\t%d\t%.1f\n", dt * 1000, yaw, dip1, dip2, distances[0], distances[1], distances[2], distances[3], distances[4], error);
  // Serial.printf("%d\t%d\t%d\t%d\t%d\n", spads[0], spads[1], spads[2], spads[3], spads[4]);
}
