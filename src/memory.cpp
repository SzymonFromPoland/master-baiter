#include "memory.h"

Memory memory;

Memory::Memory() {}

bool Memory::begin(const char *name) { return prefs.begin(name); }

void Memory::clear() { prefs.clear(); }

bool Memory::key_exists(const char *key) { return prefs.isKey(key); }

// write
void Memory::write_int(const char *key, int value) { prefs.putInt(key, value); }
void Memory::write_float(const char *key, float value) { prefs.putFloat(key, value); }
void Memory::write_bool(const char *key, bool value) { prefs.putBool(key, value); }
void Memory::write_uint8(const char *key, uint8_t value) { prefs.putUChar(key, value); }
void Memory::write_uint16(const char *key, uint16_t value) { prefs.putUShort(key, value); }
void Memory::write_uint32(const char *key, uint32_t value) { prefs.putUInt(key, value); }
void Memory::write_ulong(const char *key, unsigned long value) { prefs.putULong(key, static_cast<uint32_t>(value)); }

// read
int Memory::read_int(const char *key, int def)
{
    if (!key_exists(key))
        write_int(key, def);
    return prefs.getInt(key, def);
}

float Memory::read_float(const char *key, float def)
{
    if (!key_exists(key))
        write_float(key, def);
    return prefs.getFloat(key, def);
}

bool Memory::read_bool(const char *key, bool def)
{
    if (!key_exists(key))
        write_bool(key, def);
    return prefs.getBool(key, def);
}

uint8_t Memory::read_uint8(const char *key, uint8_t def)
{
    if (!key_exists(key))
        write_uint8(key, def);
    return prefs.getUChar(key, def);
}

uint16_t Memory::read_uint16(const char *key, uint16_t def)
{
    if (!key_exists(key))
        write_uint16(key, def);
    return prefs.getUShort(key, def);
}

uint32_t Memory::read_uint32(const char *key, uint32_t def)
{
    if (!key_exists(key))
        write_uint32(key, def);
    return prefs.getUInt(key, def);
}

unsigned long Memory::read_ulong(const char *key, unsigned long def)
{
    if (!key_exists(key))
        write_ulong(key, def);
    return prefs.getULong(key, static_cast<uint32_t>(def));
}
