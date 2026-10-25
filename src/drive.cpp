#include "drive.h"

namespace
{
  constexpr uint32_t LEDC_FREQ = 15000;
  constexpr uint8_t LEDC_RESOLUTION = 10;
  constexpr uint16_t MAX_PWM = (1 << LEDC_RESOLUTION) - 1;
  constexpr uint8_t LEDC_CHANNEL_1 = 0;
  constexpr uint8_t LEDC_CHANNEL_2 = 1;
}

void Drive::begin()
{
  ledcSetup(LEDC_CHANNEL_1, LEDC_FREQ, LEDC_RESOLUTION);
  ledcSetup(LEDC_CHANNEL_2, LEDC_FREQ, LEDC_RESOLUTION);

  ledcAttachPin(tb_pins[0], LEDC_CHANNEL_1);
  ledcAttachPin(tb_pins[5], LEDC_CHANNEL_2);
}

void Drive::setSpeed(int speed_left, int speed_right)
{
  speed_left = constrain(speed_left, -MAX_PWM, MAX_PWM);
  speed_right = constrain(speed_right, -MAX_PWM, MAX_PWM);

  uint32_t pwm_left = abs(speed_left);
  uint32_t pwm_right = abs(speed_right);

  if (speed_right == 0 && speed_left == 0)
  {
    digitalWrite(tb_pins[2], LOW); // AIN1
    digitalWrite(tb_pins[1], LOW); // AIN2
    digitalWrite(tb_pins[4], LOW); // BIN1
    digitalWrite(tb_pins[3], LOW); // BIN2
  }
  else
  {
    digitalWrite(tb_pins[1], speed_left >= 0 ? HIGH : LOW);  // AIN1
    digitalWrite(tb_pins[2], speed_left >= 0 ? LOW : HIGH);  // AIN2
    digitalWrite(tb_pins[4], speed_right >= 0 ? HIGH : LOW); // BIN1
    digitalWrite(tb_pins[3], speed_right >= 0 ? LOW : HIGH); // BIN2
  }

  ledcWrite(LEDC_CHANNEL_1, pwm_left);
  ledcWrite(LEDC_CHANNEL_2, pwm_right);
}