#include "controller.h"
#include "config.h"
#include <IRrecv.h>
#include <IRutils.h>

IRrecv irrecv(RCV);
decode_results irResults;

bool ir_en = false;
bool tog1 = true;

void startIRTask()
{
  if (digital_mode)
  {
    pinMode(RCV, INPUT);
    ir_en = false;
  }
  else
  {
    irrecv.enableIRIn();
    ir_en = true;
  }
}

void viggli_bogli(int n)
{
  for (int i = 0; i < n; i++)
  {
    flag.write(95 + 15);
    delay(80);
    flag.write(95 - 20);
    delay(110);
  }
  flag.write(95);
}

void handle_ir()
{
  if (digital_mode)
  {
    if (ir_en)
    {
      irrecv.disableIRIn();
      pinMode(RCV, INPUT);
      delay(200);
      ir_en = false;
    }

    delay_started = digitalRead(RCV);

    if (delay_started && tog1)
    {
      viggli_bogli(1);
      tog1 = false;
    }
    else if (!delay_started)
    {
      tog1 = true;
    }
  }
  else
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
        viggli_bogli(3);
      }
      else if (address == 0x07)
      {
        if (command == START)
        {
          delay_started = true;
          viggli_bogli(1);
        }
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
}