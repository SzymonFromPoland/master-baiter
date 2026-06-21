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
#include <controller.h>

VL53L1X_ULD sensor[SENSOR_COUNT];
VL53L1X_Result_t results[SENSOR_COUNT];
uint16_t distances[SENSOR_COUNT];
uint16_t spads[SENSOR_COUNT];

Adafruit_MPU6050 mpu;
Preferences prefs_global;
Servo flag;

bool delay_started = false;
bool started = false;
float web_started = false;

float yaw = 0.0f;
float target_yaw = 0.0f;
float arch_yaw = 0.0f;
float gyro_bias = 0.0f;
bool en_gyro = true;
bool target_reached = false;

float base_speed = 100;
float arch_speed_in = 50;
float arch_speed_out = 50;

PIDState drivePID;
float Kp = 50.0;
float Ki = 0.0;
float Kd = 20.0;

PIDState gyroPID;
float gyroKp = 50.0;
float gyroKi = 0.0;
float gyroKd = 20.0;

float threshold1 = 770;
float threshold2 = 500;
float threshold3 = 100;

float error, output, left_speed, right_speed, to_target, gyro_output;
int last_dir = 1;
bool startup_done = false;

float emul_dip1 = false;
float emul_dip2 = false;
float digital_mode = false;

float ARCH_PANIC_TIME = 500;
float PANIC_TIME = 500;
float START_DELAY = 4900 - 200;

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
  Wire.begin(5, 6);

  flag.setPeriodHertz(50);
  flag.attach(FLAG);

  startIRTask();

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

  uint16_t roi[2] = {13, 7}; // 13x4
  set_sensor_settings(sensor[0], Short, roi, 15, 15, threshold1);
  set_sensor_settings(sensor[1], Short, roi, 15, 15, threshold1);
  set_sensor_settings(sensor[2], Short, roi, 15, 15, threshold1);
  set_sensor_settings(sensor[3], Short, roi, 15, 15, threshold1);
  set_sensor_settings(sensor[4], Short, roi, 15, 15, threshold1);

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
  else
  {
    en_gyro = false;
    Serial.println("MPU6050 not detected - gyro control disabled");
  }

  Serial.println("Done with setup!");

  prefs_global.begin("robot", false);
  Kp = prefs_global.getFloat("kp", Kp);
  Ki = prefs_global.getFloat("ki", Ki);
  Kd = prefs_global.getFloat("kd", Kd);
  gyroKp = prefs_global.getFloat("gkp", gyroKp);
  gyroKi = prefs_global.getFloat("gki", gyroKi);
  gyroKd = prefs_global.getFloat("gkd", gyroKd);
  target_yaw = prefs_global.getFloat("tyaw", target_yaw);
  arch_yaw = prefs_global.getFloat("arch", arch_yaw);
  threshold1 = prefs_global.getFloat("thres1", threshold1);
  threshold2 = prefs_global.getFloat("thres2", threshold2);
  threshold3 = prefs_global.getFloat("thres3", threshold3);
  base_speed = prefs_global.getFloat("base_speed", base_speed);
  arch_speed_in = prefs_global.getFloat("archsi", arch_speed_in);
  arch_speed_out = prefs_global.getFloat("archso", arch_speed_out);
  PANIC_TIME = prefs_global.getFloat("panic", PANIC_TIME);
  ARCH_PANIC_TIME = prefs_global.getFloat("archpanic", ARCH_PANIC_TIME);
  digital_mode = prefs_global.getFloat("digital_mode", digital_mode);

  prefs_global.end();

  static TuningParam mySettings[] = {
      {"ON/OFF", "st", &web_started, 0, 0, 0, TYPE_TOGGLE},
      {"Digital Mode", "digital_mode", &digital_mode, 0, 0, 0, TYPE_TOGGLE},
      {"Emul DIP1", "dip1", &emul_dip1, 0, 0, 0, TYPE_TOGGLE},
      {"Emul DIP2", "dip2", &emul_dip2, 0, 0, 0, TYPE_TOGGLE},
      {"PID Error", "err", &error, 0, 0, 0, TYPE_READONLY},
      {"PID Output", "out", &output, 0, 0, 0, TYPE_READONLY},
      {"Threshold normal", "thres1", &threshold1, 0, 1000, 5, TYPE_ARROWS},
      {"Threshold trigger", "thres2", &threshold2, 0, 1000, 5, TYPE_ARROWS},
      {"Threshold slow", "thres3", &threshold3, 0, 1000, 5, TYPE_ARROWS},
      {"Base Speed", "base_speed", &base_speed, 0, 1000, 5, TYPE_ARROWS},
      {"Drive Kp", "kp", &Kp, 0, 100, 0.5, TYPE_ARROWS},
      {"Drive Ki", "ki", &Ki, 0, 100, 0.5, TYPE_ARROWS},
      {"Drive Kd", "kd", &Kd, 0, 100, 0.5, TYPE_ARROWS},
      {"Yaw", "yaw", &yaw, 0, 0, 0, TYPE_READONLY},
      {"Target Yaw", "tyaw", &target_yaw, -180, 180, 1, TYPE_ARROWS},
      {"Arch Yaw", "arch", &arch_yaw, -180, 180, 1, TYPE_ARROWS},
      {"Arch Left", "archsi", &arch_speed_in, 0, 100, 1, TYPE_ARROWS},
      {"Arch Right", "archso", &arch_speed_out, 0, 100, 1, TYPE_ARROWS},
      {"Arch Panic Time", "archpanic", &ARCH_PANIC_TIME, 0, 10000, 50, TYPE_ARROWS},
      {"Panic Time", "panic", &PANIC_TIME, 0, 10000, 50, TYPE_ARROWS},
      {"Gyro Kp", "gkp", &gyroKp, 0, 100, 0.1, TYPE_ARROWS},
      {"Gyro Ki", "gki", &gyroKi, 0, 100, 0.1, TYPE_ARROWS},
      {"Gyro Kd", "gkd", &gyroKd, 0, 100, 0.05, TYPE_ARROWS},

  };

  startTuner(mySettings, sizeof(mySettings) / sizeof(mySettings[0]), results, SENSOR_COUNT, &prefs_global);

  weights(0, 0);
}

unsigned long targetTime = 0;
unsigned long lastTime = 0;
unsigned long panicTime = 0;
unsigned long slowTime = 0;
unsigned long startTime = 0;

void loop()
{
  unsigned long now = millis();
  float dt = (now - lastTime) / 1000.0;
  lastTime = now;

  if (delay_started)
  {
    if (millis() - startTime > START_DELAY)
      started = true;
  }
  else
  {
    started = false;
    startTime = millis();
  }

  bool dip1 = !digitalRead(DIP1) || emul_dip1;
  bool dip2 = !digitalRead(DIP2) || emul_dip2;

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
    to_target = fmod(((dip1 ? arch_yaw : target_yaw) * -last_dir - yaw) + 540.0f, 360.0f) - 180.0f;
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
      distances[i] = (results[i].Status == 0) ? min(results[i].Distance, (uint16_t)threshold1) : (uint16_t)threshold1;
      spads[i] = results[i].SigPerSPAD;

      if (distances[i] < threshold1)
        sensor[i].ClearInterrupt();

      if (distances[i] < threshold1)
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
  }

  // Serial.printf("%d, %d, %d\n", any_under_theshold1, any_under_theshold2, any_under_theshold3);

  if ((started) ? error < -0.01f : distances[0] < 100 || distances[1] < 100)
    last_dir = -1;
  else if ((started) ? error > 0.01f : distances[3] < 100 || distances[4] < 100)
    last_dir = 1;

  handle_servo(now);
  handle_weights(now);
  handle_ir();

  // TODO wagi na   przod w odpowiednim momencie
  // TODO drive PID tuning

  if (started || web_started)
  {
    if (dip2)
    {
      startup_done = true;
      en_gyro = false;
      servo_pos = 1;
      weights_pos = 1;
    }

    if (startup_done)
    {
      weights_pos = 1;
      if (any_under_theshold1)
      {
        if (any_under_theshold3)
        {
          left_speed = 30 + (output * 0.67f);
          right_speed = 30 - (output * 0.67f);
        }
        else
        {
          slowTime = now;
          left_speed = base_speed + output;
          right_speed = base_speed - output;
        }

        if (now - slowTime > 500)
        {
          weights_pos = 0;
          left_speed = 100;
          right_speed = 100;
        }
      }
      else
      {
        left_speed = base_speed * last_dir;
        right_speed = base_speed * -last_dir;
      }
    }
    else if (dip1)
    {
      // MODE 1 - DIP 1 NA GORZE (OBJEZDZANIE)
      left_speed = -gyro_output;
      right_speed = gyro_output;

      servo_pos = 1;

      if (target_reached)
      {
        left_speed = (last_dir == 1) ? arch_speed_in : arch_speed_out;
        right_speed = (last_dir == 1) ? arch_speed_out : arch_speed_in;

        if ((any_under_theshold2 && now - panicTime > 500) || now - panicTime > (unsigned long)ARCH_PANIC_TIME)
        {
          last_dir = -last_dir;
          startup_done = true;
        }
      }
      else
      {
        panicTime = now;
      }
    }
    else
    {
      // MODE 2 - DIP 1 NA DOLE (KAT I PIZDA)
      left_speed = -gyro_output;
      right_speed = gyro_output;

      servo_pos = 1;

      if (target_reached)
      {
        weights_pos = 1;

        left_speed = base_speed + output;
        right_speed = base_speed - output;

        if ((any_under_theshold2 && now - panicTime > 100) || now - panicTime > (unsigned long)PANIC_TIME)
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
    servo_toggle = true;
    en_gyro = true;
    left_speed = 0;
    right_speed = 0;
    panicTime = now;
    slowTime = now;
  }

  drive(left_speed, right_speed);

  // Serial.printf("%.1fms\t%.1f°\t%d %d\t%d\t%d\t%d\t%d\t%d\t%.1f\n", dt * 1000, yaw, dip1, dip2, distances[0], distances[1], distances[2], distances[3], distances[4], error);
  // Serial.printf("%d\t%d\t%d\t%d\t%d\n", spads[0], spads[1], spads[2], spads[3], spads[4]);
}
