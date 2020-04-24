#include "Relay.h"
#include "Constants.h"

Relay::Relay(){
  this->name = "";
  this->state=POOL_RELAY_OFF;
}
Relay::Relay(String _name, int _initially_on)
: name{_name}{
  state = (_initially_on ? POOL_RELAY_ON : POOL_RELAY_OFF);
}

