#ifndef _RELAY_H
#define _RELAY_H

#include <Arduino.h>
#include "Constants.h"
#include "DailySchedule.h"

/*
  Relay represents one of a bank of relays that turn on and off for various
  pool equipment. The relays are not linked to any hardware pin, but instead
  given a (preferrably unique) ID so we can use the directly with pins or via an
  output register.
*/
class Relay {
  public:
    String name;
    RelayState state;
    PoolDailySchedule schedule;
    Relay();
    Relay(String _name, int _initially_on);
};

#endif
