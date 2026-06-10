#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <ESP32Servo.h>

extern Preferences prefs_global;
extern Servo flag;
extern bool delay_started;

void handle_ir();
void startIRTask(); 