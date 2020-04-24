#ifndef _POOLCONTROLLER_H
#define _POOLCONTROLLER_H

#include <Arduino.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ShiftRegister74HC595.h>
#include <Bounce2.h>
#include <TimeLib.h>
#include "Constants.h"
#include "Relay.h"
#include "DailySchedule.h"

struct TempSensor{
  //"analog" for the analog pin
  //"<some hex string>" for DS1820 sensors
  String name;
  float temp;
};

struct PoolController
{
    //Wifi details
    String wifi_ssid;
    String wifi_pw;

    //Temperature sensor trackers (roles/presence/temp)
    String pool_water_sensor_name;
    String roof_sensor_name;
    String ambient_air_sensor_name;
    TempSensor temp_sensors[MAX_SENSORS];
    int num_sensors;

    //Digital temperature probe(s) (DS1820)
    OneWire one_wire;
    DallasTemperature digital_temp_sensors;

    //Relays to control equipment
    //NOTE: These are just state trackers, not 
    //      actual I/O objects
    Relay relays[MAX_RELAY];

    //I/O object for a 74HC595 shift register
    //that actually controls the relay outputs
    ShiftRegister74HC595<1>* relay_output;


    //Time tracking stuff (NTP and manual settings)
    int ntp_update_seconds;
    String ntp_server_name;    
    int gmt_offset;
    WiFiUDP udp;
    byte udp_packet_buffer[NTP_PACKET_SIZE];
    unsigned long last_ntp_update;
    TimeState time_state;

    //Error code Tracker
    Pool_Error_Code error[MAX_POOL_ERRORS];
    int num_errors;

    //Pool state tracking
    Bounce manualModeSwitch;
    PoolState pool_state;

    //Solar heating state tracking
    SolarState solar_state;
    float solar_target_temp;

    //Runtime ms counter for the last time update() was run
    unsigned long last_update;
    
    //Remote debugger
    RemoteDebug* debug;

    PoolController(RemoteDebug* debug);
  
    //Load flash default config (initial hardcoded values)
    void populate_defaults();

    //JSON Config file save/load/reset
    byte save_config ();
    void reset_config ();
    byte load_config ();

    //Main loop updated method for updating the pool states
    void update();


    //Utility methods for ascii hex <-> binary conversion
    String digitalTempAddrToHex(DeviceAddress d);
    byte ascii_hex_2_bin(String s);
    String byteToHex(byte num);

    // Internal Utility methods

    //Returns whether a sensor name is present in the list of sensors
    byte isSensorPresent(String name);

    //Returns non-zero if the name is non-null and not "analog"
    byte isSensorDigital(const char* name);
    
    //Returns whether the sensor is analog or not (the string "analog")
    byte isSensorAnalog(const char* name);
  
    //Update the list of one-wire sensors, set any error states
    //and update our internal name-listing
    void update_temperature_sensors();

    //Update the analog pool sensors presence and reading (set error
    //state if it seems disconnected or something)
    void update_analog_sensor();

    //Update the relay states based on the sensors, time, ntp state
    //and manual control status (manual control means we turn everything off
    //and let the hardware do what it does. 
    void update_relays();

    //If we're on a network, attempt to update the NTP time according
    //to our timezone offset and update our time state
    void sendNTPPacket(IPAddress &address);
    void update_ntp();

    /*
      Based on the current time, manual-mode switch and any other factors,
      determine what the current pool state should be.
    */
    void update_pool_state();

    void sendNTPpacket(IPAddress &address);

    time_t getNtpTime();

    //Returns: 0 on failure, 1 on success (and puts time-of-day in target)
    int parseTimeStr(const char *str,tmElements_t *target);

    void log_error(Pool_Error_Code err);
    void clear_error(Pool_Error_Code err);

    byte addSensor(String name,float temp = POOL_TEMP_SENSOR_MISSING);

    void assignSensorRole(String name, String role);

    String getSensorRole(String name);

    int getRelayIndexByName(String& name);
    byte parseDailySchedule(PoolDailySchedule& d, JsonArray& schedule,String& err);

    byte connect_wifi(String ssid, String pw);

    /////// JSON serialize/deserialize methods

    //Relay names/schedules (loading_config is flag for loading from internal config)
    byte setJSONRelayDetails(JsonArray& relays, String& err, byte loading_config = 0);
    DynamicJsonDocument getJSONRelayDetails();
 
    //Temp Sensors
    byte validateJSONSensorsUpdate(JsonArray& sensors);
    DynamicJsonDocument getJSONSensorsDetails();
    byte setJSONSensorsDetails(JsonArray& sensors, String& err, byte loading_config = 0); 

    //Wifi/NTP (ssid, password, ntp server/interval, UTC offset)
    DynamicJsonDocument getJSONWifiDetails();
    byte setJSONWifiDetails(JsonObject& wifi, String& err, byte loading_config = 0);
    //TODO: AP mode stuff 
    
    //Pins (relays, manual mode switch, analog sensor use, 1-wire bus pin)
    DynamicJsonDocument getJSONHardwareDetails();
    byte setJSONHardwareDetails(DynamicJsonDocument& hardware, String& err, byte loading_config = 0);

  
};


#endif
