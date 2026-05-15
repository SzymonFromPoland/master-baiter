#include "controller.h"
#include "config.h"
#include <IRrecv.h>
#include <IRutils.h>

IRrecv irrecv(RCV);
decode_results irResults;

void startIRTask()
{
  irrecv.enableIRIn();
}

void handle_ir()
{
  if (!irrecv.decode(&irResults))
    return;

  uint8_t START, STOP;
  prefs_global.begin("robot", false);
  STOP = prefs_global.getUInt("stop_address", 0);
  START = prefs_global.getUInt("start_address", 0);

  switch (irResults.decode_type)
  {
  case RC5:
  {
    uint8_t address = (irResults.value >> 6) & 0x1F;
    uint8_t command = irResults.value & 0x3F;
    uint8_t toggle = (irResults.value >> 11) & 0x01;

    Serial.printf("RC5 - Address: %u, Command: %u, Toggle: %u\n", address, command, toggle);

    if (address == 0x0B)
    {
      START = command + 1;
      STOP = command;
      prefs_global.putUInt("stop_address", STOP);
      prefs_global.putUInt("start_address", START);
    }
    else if (address == 0x07)
    {
      if (command == START)
        delay_started = true;
      else if (command == STOP)
        delay_started = false;
    }
    break;
  }
  default:
    break;
  }

  prefs_global.end();
  irrecv.resume();
}