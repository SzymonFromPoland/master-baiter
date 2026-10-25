#pragma once

#include <Arduino.h>
#include <Preferences.h>

class Memory
{
public:
    Memory();

    bool begin(const char *name);

    void load();
    void clear();

    void write_int(const char *key, int value);
    void write_float(const char *key, float value);
    void write_bool(const char *key, bool value);
    void write_uint8(const char *key, uint8_t value);
    void write_uint16(const char *key, uint16_t value);
    void write_uint32(const char *key, uint32_t value);
    void write_ulong(const char *key, unsigned long value);

    int read_int(const char *key, int def = 0);
    float read_float(const char *key, float def = 0.0f);
    bool read_bool(const char *key, bool def = false);
    uint8_t read_uint8(const char *key, uint8_t def = 0);
    uint16_t read_uint16(const char *key, uint16_t def = 0);
    uint32_t read_uint32(const char *key, uint32_t def = 0);
    unsigned long read_ulong(const char *key, unsigned long def = 0);

private:
    bool key_exists(const char *key);

    Preferences prefs;
};

extern Memory memory;