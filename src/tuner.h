#ifndef TUNER_H
#define TUNER_H

#include <Arduino.h>
#include <VL53L1X_ULD.h>
#include <Preferences.h>

enum ParamType
{
    TYPE_SLIDER,
    TYPE_ARROWS,
    TYPE_TOGGLE,
    TYPE_READONLY,
    TYPE_BUTTON
};

struct TuningParam
{
    const char *label;
    const char *id;
    float *value;
    float min;
    float max;
    float step;
    ParamType type;
};

void startTuner(TuningParam *params, int numParams, VL53L1X_Result_t *results, int numSensors, Preferences *prefs_global);

#endif