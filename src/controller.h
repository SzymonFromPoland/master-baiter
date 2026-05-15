#pragma once

#include <Arduino.h>
#include <Preferences.h>

extern Preferences prefs_global;
extern bool delay_started;

void handle_ir();
void startIRTask(); 