#ifndef _DAILY_SCHED
#define _DAILY_SCHED

#include <Arduino.h>
#include <TimeLib.h>
#include "Constants.h"

class PoolDailySchedule{
  public:
    TimeElements on_time[MAX_SCHEDULES];
    TimeElements off_time[MAX_SCHEDULES];
    int num_schedules;

    PoolDailySchedule();
    void clear();
    byte PoolValidateSchedule();

  
};

#endif
