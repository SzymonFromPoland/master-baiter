#include "drive.h"


void drive(int speed_left, int speed_right) {
  speed_left = constrain(speed_left, -100, 100);
  speed_right = constrain(speed_right, -100, 100);

  int pwm_left = map(abs(speed_left), 0, 100, 0, 255);
  int pwm_right = map(abs(speed_right), 0, 100, 0, 255);

  if (speed_right == 0 && speed_left == 0) {
    digitalWrite(tb_pins[2], LOW);  // AIN1
    digitalWrite(tb_pins[1], LOW);  // AIN2
    digitalWrite(tb_pins[4], LOW);  // BIN1
    digitalWrite(tb_pins[3], LOW);  // BIN2
  } else {
    digitalWrite(tb_pins[2], speed_left >= 0 ? HIGH : LOW);   // AIN1
    digitalWrite(tb_pins[1], speed_left >= 0 ? LOW : HIGH);   // AIN2
    digitalWrite(tb_pins[4], speed_right >= 0 ? HIGH : LOW);  // BIN1
    digitalWrite(tb_pins[3], speed_right >= 0 ? LOW : HIGH);  // BIN2
  }

  analogWrite(tb_pins[0], pwm_left);   // pwm A
  analogWrite(tb_pins[5], pwm_right);  // pwm B
}