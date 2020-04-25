#ifndef POOL_CONSTANTS_H
#define POOL_CONSTANTS_H

#include "pgmspace.h"
#include <ArduinoJson.h>
#include "RemoteDebug.h"

#define pdebugA(fmt, ...) if (debug->isActive(debug->ANY)) 		debug->printf("(%s) " fmt, __func__, ##__VA_ARGS__)
#define pdebugP(fmt, ...) if (debug->isActive(debug->PROFILER)) 	debug->printf("(%s) " fmt, __func__, ##__VA_ARGS__)
#define pdebugV(fmt, ...) if (debug->isActive(debug->VERBOSE)) 	debug->printf("(%s) " fmt, __func__, ##__VA_ARGS__)
#define pdebugD(fmt, ...) if (debug->isActive(debug->DEBUG)) 		debug->printf("(%s) " fmt, __func__, ##__VA_ARGS__)
#define pdebugI(fmt, ...) if (debug->isActive(debug->INFO)) 		debug->printf("(%s) " fmt, __func__, ##__VA_ARGS__)
#define pdebugW(fmt, ...) if (debug->isActive(debug->WARNING)) 	debug->printf("(%s) " fmt, __func__, ##__VA_ARGS__)
#define pdebugE(fmt, ...) if (debug->isActive(debug->ERROR)) 		debug->printf("(%s) " fmt, __func__, ##__VA_ARGS__)

#define CONFIG_FILE_PATH "/pool_config.json"

//ms to wait before considering a wifi connection as a failure
#define WIFI_CONNECT_TIMEOUT 10000

// ID of the settings block (in EEPROM/flash)
#define CONFIG_VERSION "vb1"

// Tell it where to store your config data in EEPROM
#define CONFIG_START 32


//error sentinel for HEX string conversion failures
#define HEX_CONV_ERR 69 //<bill_and_teds_excellent_adventure>What number are you thinking of? 69 DUDE!</bill_and_teds_excellent_adventure>


//Maximum number of on/off times per relay per day
#define MAX_SCHEDULES 4

//size of error tracking array
#define MAX_POOL_ERRORS 8

//Maximum number of supported pool temp sensors
#define MAX_SENSORS 8

#define POOL_TEMP_SENSOR_MISSING -1.0

/*
  These two deltas define how much hotter the roof/pool needs
  to be than the pool to turn the solar on and off when 
  heating. 

  e.g. 1.0 and -5.0 mean that (when heating):
    * The roof needs to be 1 degF hotter than the pool 
      before we turn the solar on
    * The roof needs to be 5 degF cooler than the pool
      before we turn the solar off 

  This prevents us from flip/flopping the valve open and 
  closed a lot when we're near the limit.
*/
#define POOL_SOLAR_ON_ROOF_DELTA 1.0 
#define POOL_SOLAR_OFF_ROOF_DELTA -5.0 
#define POOL_SOLAR_ON_WATER_DELTA -0.5 //pool turns on only after it hit's -0.5 below setpoint
#define POOL_SOLAR_OFF_WATER_DELTA 1.0 //ditto for off only after it hits 1 above

#define POOL_SOLAR_MIN_TEMP 65.0
#define POOL_SOLAR_MAX_TEMP 150.0

//WIFI default data
static const char DEFAULT_WIFI_CONFIG[] PROGMEM = R"json(
{
  "wifi":{
    "ssid": "bknet",
    "pw": "REDACTED",
    "ntp_server": "us.pool.ntp.org",
    "tz_offset": -4
  },
  "solar":{
    "target_temp": 90.0,
    "enabled": "on"
  },
  "general":{
    "mode": "run_schedule",
    "time": "00:00:00",
    "last_time_update": 0,
    "time_status": "n/a",
    "errors":[],
    "last_status_update": 0
  },
  "relays":[
    {
      "name":"pump",
      "schedule":[],
      "state":"off"
    },
    {
      "name":"light",
      "schedule":[],
      "state":"off"
    },
    {
      "name":"solar_valve",
      "schedule":[],
      "state":"off"
    },
    {
      "name":"spa_drain",
      "schedule":[],
      "state":"off"
    },
    {
      "name":"spa_fill",
      "schedule":[],
      "state":"off"
    },
    {
      "name":"aux_1",
      "schedule":[],
      "state":"off"
    },
    {
      "name":"aux_2",
      "schedule":[],
      "state":"off"
    },
    {
      "name":"aux_3",
      "schedule":[],
      "state":"off"
    }
  ],
  "sensors":[
  ]
})json";


//NTP constants
#define NTP_PACKET_SIZE 48
#define NTP_UDP_PORT 8888
#define DEFAULT_UTC_OFFSET -4
#define DEFAULT_NTP_SERVER "us.pool.ntp.org"
//#define DEFAULT_NTP_UPDATE_SECS 300
#define DEFAULT_NTP_UPDATE_SECS 120

//Number of hours to distrust our time schedule without
//an NTP update
#define TIME_UNRELIABLE_AFTER_HOURS 48

//Ms between updates for the pool controller
#define POOL_UPDATE_INTERVAL 5000 

//TODO: Figure out which pins we can actually use here
//      (for all the pins below)

//DS1820 digital temerature probe (1-wire) input pin
#define DEFAULT_DIGITAL_TEMP_PIN D2

//Default thermister pin (only one on the ESP8266)
#define DEFAULT_ANALOG_THERM_PIN A0
#define POOL_THERM_SERIES_RES 47000 //series resister (should be 47K)
#define POOL_THERM_NOM_RES 10000 //resistance at nominal temp (usually 10K)
#define POOL_THERM_NOM_TEMP_C 25 //Nominal temp C (25C usually)
#define POOL_THERM_BETA 3950 //Beta
#define POOL_THERM_NUM_SAMPLES 5 //number of samples to avg
#define POOL_THERM_SAMPLE_DELAY 20 //ms delay between samples

//switch inputs
//NOTE: The MODE_PIN controls if we're in AP vs STA mode on the wifi
//      as well as activating the manual motor/light switches
#define POOL_MANUAL_MODE_PIN D1
#define POOL_MANUAL_MODE HIGH

//Flipping the manual mode switch 6 times (on -> off or off -> on)
//in 8 seconds resets the configuration to defaults
#define RESET_TIMEOUT 8
#define RESET_SWITCH_FLIPS 6

//Relay output pins (defaults)
#define MAX_RELAY 8 //number of relays for the controller
#define DEFAULT_POOL_RELAY_SHIFT_CLK D1
#define DEFAULT_POOL_RELAY_SHIFT_DATA D2
#define DEFAULT_POOL_RELAY_SHIFT_LATCH D3

//Relay meanings
#define POOL_RELAY_PUMP_INDEX 0
#define POOL_RELAY_LIGHT_INDEX 1
#define POOL_RELAY_SOLAR_VALVE_INDEX 2
#define POOL_RELAY_SPA_DRAIN_INDEX 3
#define POOL_RELAY_SPA_FILL_INDEX 4
static const char POOL_RELAY_PUMP_NAME[] PROGMEM = "pump";
static const char POOL_RELAY_LIGHT_NAME[] PROGMEM = "light";
static const char POOL_RELAY_SOLAR_VALVE_NAME[] PROGMEM = "solar_valve";
static const char POOL_RELAY_SPA_DRAIN_NAME[] PROGMEM = "spa_drain";
static const char POOL_RELAY_SPA_FILL_NAME[] PROGMEM = "spa_fill";
static const char *POOL_RELAY_NAME_STRINGS[] = {POOL_RELAY_PUMP_NAME,
                                                POOL_RELAY_LIGHT_NAME,
                                                POOL_RELAY_SOLAR_VALVE_NAME,
                                                POOL_RELAY_SPA_DRAIN_NAME,
                                                POOL_RELAY_SPA_FILL_NAME};


//NOTE: we only use the FAULT_IDLE state if things are so bitched that we don't dare
//      turn on the pump
enum PoolState {
  POOL_STATE_UNINITIALIZED, //Initial state before we load a config
  POOL_STATE_MANUAL, //Manual control. AP mode, all relays off (hardware switches)
  POOL_STATE_RUN_SCHEDULE, //Run the schedule on the relays (allows overrides)
  POOL_STATE_IDLE, //Do nothing (relays all off). Just read sensors
  POOL_STATE_NO_NTP //Same as idle but only because we don't have reliable time, it 
                    //bounces back to RUN_SCHEDULE when time comes back
};

enum SolarState {
  SOLAR_DISABLED, //solar heating isn't activated
  SOLAR_HEATING,  //solar heating activated and circulating
  SOLAR_BYPASS    //solar heating activated, but either the pump is off or the roof is cold
};

enum TimeState {
  POOL_TIME_UNINITIALIZED, //Right after startup, we haven' talked to an NPT server yet
  POOL_TIME_NO_INTERNET,
  POOL_TIME_OK,
  POOL_TIME_ERR
};

enum Pool_Error_Code {
  POOL_ERR_OK,
  POOL_ERR_NO_NTP,
  POOL_ERR_NO_WIFI,
  POOL_ERR_NO_DIGITAL_TEMP_SENSORS,
  POOL_ERR_ROOF_TEMP_SENSOR_PROBLEM,
  POOL_ERR_AMBIENT_TEMP_SENSOR_PROBLEM,
  POOL_ERR_POOL_WATER_SENSOR_PROBLEM
};

  
enum RelayState {
  POOL_RELAY_ON = 0,
  POOL_RELAY_OFF,
  POOL_RELAY_MANUAL_ON, //toggled manually, overrides scheduled state until next off
  POOL_RELAY_MANUAL_OFF, //ditto except until the next "on"
};

static const char POOL_ERR_OK_STR[] = "no error";
static const char POOL_ERR_NO_NTP_STR[] = "no NTP time";
static const char POOL_ERR_NO_WIFI_STR[] = "no wifi";
static const char POOL_ERR_NO_DIGITAL_TEMP_SENSORS_STR[] = "no digital temp sensors";
static const char POOL_ERR_ROOF_TEMP_SENSOR_PROBLEM_STR[] = "roof (solar) temp sensor problem";
static const char POOL_ERR_AMBIENT_TEMP_SENSOR_PROBLEM_STR[] = "ambient air temp sensor problem";
static const char POOL_ERR_POOL_WATER_SENSOR_PROBLEM_STR[] = "pool water temp sensor problem";
static const char* POOL_ERR_STRINGS[] = {
  POOL_ERR_OK_STR,
  POOL_ERR_NO_NTP_STR,
  POOL_ERR_NO_WIFI_STR,
  POOL_ERR_NO_DIGITAL_TEMP_SENSORS_STR,
  POOL_ERR_ROOF_TEMP_SENSOR_PROBLEM_STR,
  POOL_ERR_AMBIENT_TEMP_SENSOR_PROBLEM_STR,
  POOL_ERR_POOL_WATER_SENSOR_PROBLEM_STR
};




static const char POOL_STATE_UNINITIALIZED_STR[] = "uninitialized";
static const char POOL_STATE_MANUAL_STR[] = "manual_operation";
static const char POOL_STATE_RUN_SCHEDULE_STR[] = "run_schedule";
static const char POOL_STATE_IDLE_STR[] = "idle";
static const char POOL_STATE_NO_NTP_STR[] = "idle (no reliable time source)";
static const char *POOL_STATE_STRINGS[] = {POOL_STATE_UNINITIALIZED_STR,
                                           POOL_STATE_MANUAL_STR,
                                           POOL_STATE_RUN_SCHEDULE_STR,                            
                                           POOL_STATE_IDLE_STR,
                                           POOL_STATE_NO_NTP_STR};

//HACK: These ugly things and the table below are a way to get all this string data
//      in flash memory instead of simply being stuck in RAM (which we need more)
//      Please ensure there is partity between these an the state enums above
static const char TSR_WATER_STR[] PROGMEM = "water_temp";
static const char TSR_SOLAR_STR[] PROGMEM = "solar_roof_temp";
static const char TSR_AMBIENT_STR[] PROGMEM = "ambient_air_temp";
static const char TSR_UNUSED_STR[] PROGMEM = "unused";
static const char TSR_UNCHANGED_STR[] PROGMEM = "unchanged";
static const char *TSR_STRINGS[] = {
  TSR_WATER_STR,
  TSR_SOLAR_STR,
  TSR_AMBIENT_STR,
  TSR_UNUSED_STR,
  TSR_UNCHANGED_STR
};


static const char POOL_RELAY_STATE_ON_STR[] = "on";
static const char POOL_RELAY_STATE_OFF_STR[] = "off";
static const char POOL_RELAY_STATE_MAN_ON_STR[] = "on (manual)";
static const char POOL_RELAY_STATE_MAN_OFF_STR[] = "off (manual)";
static const char *POOL_RELAY_STATE_STRINGS[] = {POOL_RELAY_STATE_ON_STR,
                                            POOL_RELAY_STATE_OFF_STR,
                                            POOL_RELAY_STATE_MAN_ON_STR,
                                            POOL_RELAY_STATE_MAN_OFF_STR};


#endif
