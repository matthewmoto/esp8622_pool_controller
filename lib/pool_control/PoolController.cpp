#include <ESP8266WiFi.h>
#include <Arduino.h>
#include <PoolController.h>
#include <math.h>
#include <FS.h>


PoolController::PoolController(RemoteDebug* debug){
  this->debug = debug;

  //Set all the initial state variables
  relay_output =  0;
  this->num_errors =0;
  pool_state = POOL_STATE_UNINITIALIZED;
  solar_state = SOLAR_DISABLED;
  solar_enabled = 0;
  time_state = POOL_TIME_UNINITIALIZED; 
  last_update=0;
  this->num_sensors=0;
  last_ntp_update = 0;
  ntp_update_seconds = DEFAULT_NTP_UPDATE_SECS;
  pool_water_sensor_name = "";
  roof_sensor_name = "";
  ambient_air_sensor_name = "";

  //Set up manual mode switch
  pinMode(POOL_MANUAL_MODE_PIN,INPUT);
  manualModeSwitch.attach(POOL_MANUAL_MODE_PIN);

  //set up the NTP UDP thing
  //NOTE: This seems to work fine across AP/STA mode switches
  udp.begin(NTP_UDP_PORT); 


  //Get the onewire/temp stuff initialized
  one_wire.begin(DEFAULT_DIGITAL_TEMP_PIN);
  digital_temp_sensors.setOneWire(&one_wire);
  digital_temp_sensors.begin();

  //Set the analog pin to INPUT (we always use it)
  analog_temp = new Thermistor(DEFAULT_ANALOG_THERM_PIN,
                               3.3, //VCC
                               1.0, //internal vRef
                               1023, //max digital num
                               POOL_THERM_SERIES_RES, //series resister (should be 47K)
                               POOL_THERM_NOM_RES, //resistance at nominal temp (usually 10K)
                               POOL_THERM_NOM_TEMP_C, //Nominal temp C (25C usually)
                               POOL_THERM_BETA, //Beta
                               POOL_THERM_NUM_SAMPLES, //number of samples to avg
                               POOL_THERM_SAMPLE_DELAY); //ms delay between samples
  //pinMode(DEFAULT_ANALOG_THERM_PIN,INPUT);

  //Set up the relay shift register
  relay_output = new ShiftRegister74HC595<1>(
      DEFAULT_POOL_RELAY_SHIFT_DATA,
      DEFAULT_POOL_RELAY_SHIFT_CLK,
      DEFAULT_POOL_RELAY_SHIFT_LATCH);
  relay_output->setAllHigh();

  //Attempt to load the config from SPIFFS
  //load_config();
}

void PoolController::reset_config(){
  DynamicJsonDocument doc(2048);
  byte all_good=1;
  String err="";

  pdebugI("populating config defaults\n");
  //Populate Wifi defaults
  deserializeJson(doc, (const __FlashStringHelper*)DEFAULT_WIFI_CONFIG);
  JsonObject j = doc["wifi"];
  all_good = setJSONWifiDetails(j,err, 1);
  if (!all_good) return; //error will be logged in the setJSON... method
  
  //Populate the relay defaults
  JsonArray a = doc["relays"];
  all_good = setJSONRelayDetails(a,err,1);
  if (!all_good) return; //error will be logged in the setJSON... method

  //Populate the sensor defaults
  JsonArray s = doc["sensors"];
  all_good = setJSONSensorsDetails(s,err,1);
  if (!all_good) return; //error will be logged in the setJSON... method
  
  //Populate the solar defaults
  JsonObject o = doc["solar"];
  all_good = setJSONSolarDetails(o,err,1);
  if (!all_good) return; //error will be logged in the setJSON... method

  //If we make it here, we're considered intialized
  if (this->pool_state == POOL_STATE_UNINITIALIZED){
    this->pool_state = POOL_STATE_RUN_SCHEDULE;
  }

  //Save a copy of the config
  save_config();
}

byte PoolController::save_config(){
  File configFile = SPIFFS.open(CONFIG_FILE_PATH,"w");
  if (!configFile){
    pdebugE("Unable to create config file path in SPIFFS: \"%s\". Config NOT saved\n",CONFIG_FILE_PATH);
    return 0;
  }
  
  pdebugI("Saving configuration to SPIFFS\n");
  DynamicJsonDocument config(4096);
  getJSONWifiDetails(config);
  getJSONRelayDetails(config);
  getJSONSensorsDetails(config);
  getJSONRelayDetails(config);
  getJSONSensorsDetails(config);
  getJSONSolarDetails(config);

  //Set all relay states to "off" for saving
  JsonArray relays = config["relays"];
  for (JsonVariant r: relays){
    JsonObject relay = r.as<JsonObject>(); 
    relay["state"]="off"; 
  }

  /*
  String nukeme;
  serializeJsonPretty(config,nukeme);
  pdebugI("MM:\n%s\n",nukeme.c_str());
  */
 
  if (serializeJsonPretty(config,configFile) == 0){
    pdebugE("Failed to write configuration to \"%s\"\n",CONFIG_FILE_PATH);
    return 0;
  }
  return 1;
}

byte PoolController::load_config ()
{
    pdebugI("Attempting to load config from SPIFFS \"%s\"",CONFIG_FILE_PATH);

    //First, try to load a config from SPIFFS
    File configFile = SPIFFS.open(CONFIG_FILE_PATH,"r");
    DynamicJsonDocument config(4096);
    
    DeserializationError error = deserializeJson(config,configFile);
    //if the SPIFFS load failed, roll with the defaults
    if (error){
      pdebugE("Error loading SPIFFS config file. Reverting to default config\n");
      reset_config();
      return 0;
    }

    String err; 
    JsonObject o = config["wifi"];
    if (!setJSONWifiDetails(o,err,1)){
      pdebugE("Error loading wifi details from config file. Reverting to default config. Err:\n%s",err.c_str());
      reset_config();
      return 0;
    }

    JsonArray a = config["relays"];
    if (!setJSONRelayDetails(a,err,1)){
      pdebugE("Error loading relay details from config file. Reverting to default config. Err:\n%s",err.c_str());
      reset_config();
      return 0;
    }

    a = config["sensors"];
    if (!setJSONSensorsDetails(a,err,1)){
      pdebugE("Error loading sensors details from config file. Reverting to default config. Err:\n%s",err.c_str());
      reset_config();
      return 0;
    }
    
    o = config["solar"];
    if (!setJSONSolarDetails(o,err,1)){
      pdebugE("Error loading solar details from config file. Reverting to default config. Err:\n%s",err.c_str());
      reset_config();
      return 0;
    }

  //If we make it here, we're considered intialized
  if (this->pool_state == POOL_STATE_UNINITIALIZED){
    this->pool_state = POOL_STATE_RUN_SCHEDULE;
  }
}

unsigned long timeOfDay(int h, int m, int s){
  return (long)h * SECS_PER_HOUR +
         (long)m * SECS_PER_MIN +
         (long)s;
}

//Assumes we have a reliable time from NTP
//Returns 1 if relay should be on (according to schedule), 0 otherwise
byte determineRelayFromSchedule(PoolDailySchedule& sched){

  //get the current time
  int h = hour();  
  int m = minute();
  int s = second(); 
  unsigned long nowToday = timeOfDay(h,m,s);

  unsigned long on_buff, off_buff;
  //iterate the schedule and see if it's between any on/off sections
  for (int x = 0;x < sched.num_schedules; x++){
      on_buff = timeOfDay(sched.on_time[x].Hour,
                          sched.on_time[x].Minute,
                          sched.on_time[x].Second);
      off_buff = timeOfDay(sched.off_time[x].Hour,
                           sched.off_time[x].Minute,
                           sched.off_time[x].Second);

      if (on_buff <= nowToday && nowToday < off_buff)
        return 1;
  }

  return 0;

}

void PoolController::update_solar_heating(){
  String solar_valve_name((const __FlashStringHelper*)POOL_RELAY_SOLAR_VALVE_NAME);
  String pump_relay_name((const __FlashStringHelper*)POOL_RELAY_PUMP_NAME);

  //Get a ref to the solar valve relay (to toggle)
  Relay* solar_relay = getRelayByName(solar_valve_name);
  Relay* pump_relay = getRelayByName(pump_relay_name);

  TempSensor* water_sensor = getSensorByName(pool_water_sensor_name);
  TempSensor* roof_sensor = getSensorByName(roof_sensor_name);

  //Don't evaluate if solar isn't turned on
  if (!solar_enabled){
    solar_state = SOLAR_DISABLED;
    return;
  }

  //reset the solar state for re-evaluation
  //(since we've already bailed if solar isn't enabled)
  if (solar_state == SOLAR_DISABLED){
    solar_state =SOLAR_BYPASS;
  }
  
  //Disable logic if we don't find both pump and solar relays
  if (solar_relay == 0 || pump_relay == 0){
    pdebugE("Unable to find both solar and pump relays by name! disabling solar logic!\n");
    solar_state = SOLAR_DISABLED;
    return; //NOTE: We're returning since we don't have some relays so we have nothing to toggle
  }

  //Also disable logic if we don't have pool water sensors
  else if (water_sensor == 0){
    pdebugE("Unable to find water temp sensor! disabling solar logic!\n");
    solar_state = SOLAR_DISABLED;
  }

  //Also bail if we aren't in the pool state to run the schedule
  else if (pool_state != POOL_STATE_RUN_SCHEDULE){
    pdebugI("Pool state is not set to run schedule. Disabing solar logic\n");
    solar_state = SOLAR_DISABLED;
  }

  //Also disable if the pump isn't running
  else if (pump_relay->state != POOL_RELAY_ON &&
           pump_relay->state != POOL_RELAY_MANUAL_ON){
    pdebugI("Pool pump is not running, disabling solar logic\n");
    solar_state = SOLAR_DISABLED;
  }

  byte roof_too_cold = 0;
  byte water_too_hot=0;
  byte roof_hot_enough = 0;
  byte water_too_cold=0;

  float roof_temp = POOL_TEMP_SENSOR_MISSING;

  switch (solar_state){
    case SOLAR_DISABLED: //solar heating isn't activated
      pdebugD("Solar heating is disabled, ensuring our solar relay is off\n");
      solar_relay->state = POOL_RELAY_MANUAL_OFF;
      break;
    case SOLAR_HEATING: //solar heating activated and circulating
      //Turn on the relay
      solar_relay->state = POOL_RELAY_MANUAL_ON;

      //If the roof cools off too much or the pump isn't running, close the valve
      //assess the roof
      if (roof_sensor == 0){
        pdebugW("Warning: Roof sensor not available, reverting to heuristic mode\n");
        roof_too_cold = 0;
      }
      else if(roof_sensor->temp < (solar_target_temp + POOL_SOLAR_OFF_ROOF_DELTA)){
        roof_too_cold = 1;
        roof_temp = roof_sensor->temp;
        pdebugI("Roof temperature (%.2f) is lower than the setpoint (%.2f) + fudge (%.2f)\n",
                 roof_sensor->temp, solar_target_temp, POOL_SOLAR_OFF_ROOF_DELTA);
      }

      //assess the water
      if (water_sensor->temp > (solar_target_temp + POOL_SOLAR_OFF_WATER_DELTA)){
        water_too_hot = 1;
        pdebugI("Water temperature (%.2f) is higher than the setpoint (%.2f) + fudge (%.2f)\n",
                 water_sensor->temp, solar_target_temp, POOL_SOLAR_OFF_WATER_DELTA);
      }

      if (roof_too_cold || water_too_hot){
        pdebugI("Solar bypass engaged\n");
        solar_state = SOLAR_BYPASS;
      }
      break;
    case SOLAR_BYPASS: //solar heating activated, but either the pump is off or the roof is cold
      //Turn on the relay
      solar_relay->state = POOL_RELAY_MANUAL_OFF;

      //If the roof cools off too much or the pump isn't running, close the valve
      //assess the roof
      if (roof_sensor == 0){
        pdebugW("Warning: Roof sensor not available, reverting to heuristic mode\n");
        roof_hot_enough = 1;
      }
      else if(roof_sensor->temp > (solar_target_temp + POOL_SOLAR_ON_ROOF_DELTA)){
        roof_temp = roof_sensor->temp;
        roof_hot_enough = 1;
      }

      //assess the water
      if (water_sensor->temp < (solar_target_temp + POOL_SOLAR_ON_WATER_DELTA)){
        water_too_cold = 1;
      }

      if (roof_hot_enough && water_too_cold){
        pdebugI("Roof temperature (%.2f) is hot enough above the setpoint (%.2f) + fudge (%.2f)\n",
                 roof_temp, solar_target_temp, POOL_SOLAR_ON_ROOF_DELTA);
        pdebugI("Water temperature (%.2f) is below than the setpoint (%.2f) + fudge (%.2f)\n",
                 water_sensor->temp, solar_target_temp, POOL_SOLAR_ON_WATER_DELTA);
        pdebugI("Solar heating engaged\n");
        solar_state = SOLAR_HEATING;
      }
      break;
  }
    
}

void PoolController::update_relays(){

  byte scheduled_on=0;
  RelayState s;
  byte anything_changed = 0;

  pdebugD("PoolController::update_relays() called\n");

  //DEBUG (comment out for production) (hardware debug logic)
  /*unsigned long now = millis();
  unsigned long secs = millis() / 1000L;
 
  RelayState onoff = POOL_RELAY_ON;
  if (secs & 1L){
    onoff = POOL_RELAY_OFF;
  } 
    
  pdebugI("DEBUG: Updating all relays to be %s\n",POOL_RELAY_STATE_STRINGS[onoff]); 
  for (int x = 0;x<MAX_RELAY;x++){
    relays[x].state = onoff;
  }

  pdebugD("Updating relay outputs: (");
  for (int x=0;x<MAX_RELAY;x++){
    pdebugD("%s=%s\n",relays[x].name.c_str(),POOL_RELAY_STATE_STRINGS[relays[x].state]);
    //NOTE: The relays we're using are active-low
    relay_output->setNoUpdate(x, (relays[x].state == POOL_RELAY_ON ||
          relays[x].state == POOL_RELAY_MANUAL_ON) ? LOW : HIGH);
  }
  pdebugD(")\n");
  relay_output->updateRegisters();

  return;*/ 
  //END DEBUG

  switch (pool_state){
    //If we're in manual/idle states, everything turns off. 
    //Also turn everything off if we're unitialized and still figuring stuff out
    case POOL_STATE_MANUAL:
    case POOL_STATE_IDLE:
    case POOL_STATE_NO_NTP:
    case POOL_STATE_UNINITIALIZED:
      for (int x=0;x<MAX_RELAY;x++){
        //record if we changed anything 
        if (relays[x].state != POOL_RELAY_OFF && 
            relays[x].state != POOL_RELAY_MANUAL_OFF){
          anything_changed=1;
        }
        relays[x].state = POOL_RELAY_OFF;
      }
      break; 

    case POOL_STATE_RUN_SCHEDULE:
      
      //iterate the schedule and update relay states appropriately
      for (int x = 0;x < MAX_RELAY; x++){
        scheduled_on = determineRelayFromSchedule(relays[x].schedule);

        switch (relays[x].state){
          //Handle manually set relays (and let them reset to running the schedule
          //when it catches up to the manually-set state)
          case POOL_RELAY_MANUAL_ON:
            s = (scheduled_on ? POOL_RELAY_ON : POOL_RELAY_MANUAL_ON); 
            break;
          case POOL_RELAY_MANUAL_OFF:
            s = ((scheduled_on == 0) ? POOL_RELAY_OFF : POOL_RELAY_MANUAL_OFF); 
            break;
          default:
            //handle vanilla schedule on/offs
            s = scheduled_on ? POOL_RELAY_ON : POOL_RELAY_OFF;
            break;
        }        

        //Update the relay state
        if (s != relays[x].state){
          anything_changed=1;
          relays[x].state = s;
        }
      }
      break;
  }

  //If we changed any relay states, update the shift register
  //if (anything_changed){
  if (1){
    pdebugD("Updating relay outputs: (");
    for (int x=0;x<MAX_RELAY;x++){
      pdebugD("%s=%s\n",relays[x].name.c_str(),POOL_RELAY_STATE_STRINGS[relays[x].state]);
      //NOTE: The relays we're using are active-low
      relay_output->setNoUpdate(x, (relays[x].state == POOL_RELAY_ON ||
                                   relays[x].state == POOL_RELAY_MANUAL_ON) ? LOW : HIGH);
    }
    pdebugD(")\n");
    relay_output->updateRegisters();
    
  } 
}

void PoolController::update()
{
  unsigned long now = millis();

  //Bail if we're unitialized
  if (this->pool_state == POOL_STATE_UNINITIALIZED){
    pdebugD("update() called with uninitialized PoolController, bailing...\n");
    return;
  }

  //Debounce our manual mode switch (need to call this often regardless of update
  //interval)
  manualModeSwitch.update();

  //Bail if we just updated the state (restrict updates to a set interval)
  if (now - this->last_update <= POOL_UPDATE_INTERVAL){
    return;
  }
  pdebugD("PoolController::update() running at %lu\n",now);

  //update our sensors and switch states
  update_temperature_sensors();

  //Update our ntp state (if it's time)
  update_ntp();

  //Figure out what to do with the relays based on all we know
  update_relays();

  //Get the next state based on our updates (provided we haven't gone critical)
  update_pool_state();

  //Update the solar heating logic
  update_solar_heating();

  //Log the update time to now (since it probably took a little time to do all that)
  last_update = millis();
}
void PoolController::getJSONWifiDetails(DynamicJsonDocument& info){
  //DynamicJsonDocument info(512);
  JsonObject wifi = info.createNestedObject("wifi");
  wifi["ssid"] = wifi_ssid;
  wifi["pw"] = wifi_pw;
  wifi["ntp_server"] = ntp_server_name;
  wifi["tz_offset"] = gmt_offset;
  //return info;
}

void PoolController::getJSONSensorsDetails(DynamicJsonDocument& info){
  //DynamicJsonDocument info(512);
  
  JsonArray d_sensors = info.createNestedArray("sensors");
  //Iterate the temp sensors
  for (int x=0;x<num_sensors;x++){
    JsonObject t = d_sensors.createNestedObject();
    t["name"] = temp_sensors[x].name;
    t["role"] = getSensorRole(temp_sensors[x].name); 
    t["temp_f"] = temp_sensors[x].temp;
    if (isSensorDigital(temp_sensors[x].name.c_str())) t["type"] = F("DS1820 Digital Sensor");
    else if (isSensorAnalog(temp_sensors[x].name.c_str())) t["type"] = F("Analog Thermistor");
    else t["type"] = F("not set"); 
  }
}

void PoolController::getJSONRelayDetails(DynamicJsonDocument& info){
  //DynamicJsonDocument info(2048);
  char timebuffer[32];
  
  JsonArray json_relays = info.createNestedArray("relays");
  //Iterate the temp sensors
  for (int x=0;x<MAX_RELAY;x++){
    JsonObject r = json_relays.createNestedObject();
    r["name"] = relays[x].name;
    r["state"] = POOL_RELAY_STATE_STRINGS[relays[x].state]; 

    //add the schedule in daily-time format
    JsonArray a = r.createNestedArray("schedule");
    for (int y = 0; y < relays[x].schedule.num_schedules; y++){
      JsonObject t = a.createNestedObject();
      sprintf(timebuffer,"%02d:%02d:%02d",relays[x].schedule.on_time[y].Hour,
          relays[x].schedule.on_time[y].Minute,
          relays[x].schedule.on_time[y].Second);
      t["on"]=timebuffer;
      sprintf(timebuffer,"%02d:%02d:%02d",relays[x].schedule.off_time[y].Hour,
          relays[x].schedule.off_time[y].Minute,
          relays[x].schedule.off_time[y].Second);
      t["off"]=timebuffer;
    }
  }
}

//Returns: 0 on failure, 1 on success (and puts time-of-day in target)
int createElements(const char *str,tmElements_t *target)
{
  int h=-1,m=-1,s=-1;
  int ret = sscanf(str, "%d:%d:%d", &h, &m, &s);

  if (ret != 3) return 0;
  if (h < 0 || h >23) return 0;
  if (m < 0 || m > 60) return 0;
  if (s < 0 || s > 60) return 0;

  target->Hour = h;
  target->Minute = m;
  target->Second = s;
  return 1;
}

//Returns 0 or a matching relay
Relay* PoolController::getRelayByName(String name){
  for (int x = 0;x< MAX_RELAY;x++){
    if (relays[x].name == name)
      return &(relays[x]);
  } 
  return 0;
}

byte PoolController::parseDailySchedule(PoolDailySchedule& d, JsonArray& schedule,String& err){
  TimeElements on_time,off_time;
  unsigned long on_secs,off_secs;
  unsigned long ons,ofs;
  d.num_schedules=0;
  for (JsonVariant s_item : schedule){
    String on_time_str = s_item["on"];
    String off_time_str = s_item["off"];
    pdebugD("Time requested: \"%s\" <-> \"%s\" \n",on_time_str.c_str(),off_time_str.c_str());

    //bail if the times are not formatted correctly
    if (! createElements(on_time_str.c_str(),&on_time) ||
        ! createElements(off_time_str.c_str(),&off_time)){
      err = "Invalid time string for on/off field(s)";
      return 0;
    }
   
    //bail if the off time is before the on time
    off_secs = timeOfDay(off_time.Hour,off_time.Minute,off_time.Second);
    on_secs = timeOfDay(on_time.Hour,on_time.Minute,on_time.Second);
    if ( off_secs <= on_secs){
      err = "Off-time cannot the same or before on-time";
      return 0;
    }

    //bail if the off/on range intersects any other ranges already stored
    for (int x=0;x<d.num_schedules;x++){
      ofs = timeOfDay(d.off_time[x].Hour,d.off_time[x].Minute,d.off_time[x].Second);
      ons = timeOfDay(d.on_time[x].Hour,d.on_time[x].Minute,d.on_time[x].Second);

      pdebugD("Compare overlap: %d/%d and %d/%d\n",on_secs,off_secs,ons,ofs);

      if (max(on_secs,ons) < min(off_secs,ofs)){
        err = "Time ranges can not overlap";
        return 0;
      }

      pdebugD("No overlap found!\n");
    }

    //If we get here, the schedule entry is valid, add it to the list
    d.on_time[d.num_schedules] = on_time;
    d.off_time[d.num_schedules] = off_time;
    d.num_schedules++;
  }

  pdebugD("Added %d schedule entries\n",d.num_schedules);

  return 1;

}

byte PoolController::setJSONRelayDetails(JsonArray& relays, String& err, byte loading_config){
  pdebugI("Got request to update relay schedule\n");

  //Make sure the update has the right number of relay elements
  /*if (relays.size() != MAX_RELAY){
    err = "Incorrect number of relays";
    pdebugE("%s\n",err.c_str());
    return 0;
  }*/

  //Iterate the schedule (validation only)
  PoolDailySchedule sched_buffer;
  JsonArray s;
  String state;
  String name;
  Relay* rp = 0;
  for (JsonVariant r : relays){
    JsonObject relay = r.as<JsonObject>(); 

    //Ensure the relay has a name (that's how we find it)
    if (relay["name"].isNull()){
        err = "Missing \"name\" field for relay specified";
        pdebugE("%s\n",err.c_str());
        return 0;
    }
    if (!loading_config){
      name = relay["name"].as<String>(); 
      rp = getRelayByName(relay["name"]);
      if (rp == 0){
          err = "Relay name specified doesn't match any known relay";
          pdebugE("%s\n",err.c_str());
          return 0;
      }
    }
    

    //Parse the schedule and check it for sanity
    s = relay["schedule"];
    if (s.isNull()) continue;
    //NOTE: err is set by parseDailySchedule if it fails
    if (!parseDailySchedule(sched_buffer, s, err)){
      pdebugE("%s\n",err.c_str());
      return 0;
    }  

    //Parse the state and check it for sanity
    if (!relay["state"].isNull()){
      state = relay["state"].as<String>(); 
      if (state != "on" && state != "off"){
        err = "Invalid \"state\" provided, must be \"on\" or \"off\"";
        pdebugE("%s\n",err.c_str());
        return 0;
      }
    }
  }

  //Iterate the schedule again and update (if we make it here
  //the schedule is valid)
  int x = 0;
  RelayState rstate;
  String name_buff;
  for (JsonVariant relay : relays){

    //Get our internal relay object
    //NOTE: If we're loading our config, we assume there
    //      are 8 relays and we're setting them up sequentially
    if (loading_config){
      rp = &(this->relays[x]);
    }
    //Otherwise, we just match the relays by name
    else{
      name = relay["name"].as<String>(); 
      rp = getRelayByName(relay["name"]);
    }

    //Parse the schedule and update it if present
    s = relay["schedule"];
    if (!s.isNull()){
      //NOTE: err is set by parseDailySchedule if it fails
      parseDailySchedule(sched_buffer, s, err);
       
      //Update our relay
      rp->schedule = sched_buffer;
    }

    //parse the relay state and update it if present
    if (!relay["state"].isNull()){
      state = relay["state"].as<String>(); 
      //If we're setting states as a config, just turn them on/of
      if (loading_config)
        rstate = (state == "on") ? POOL_RELAY_ON : POOL_RELAY_OFF;
      //Otherwise, note that the state change is a manual override of the schedule
      else
        rstate = (state == "on") ? POOL_RELAY_MANUAL_ON : POOL_RELAY_MANUAL_OFF;
      rp->state = rstate;
    }

    //If we set to update names (config loading only)
    if (loading_config){
      name_buff = relay["name"].as<String>();
      if (name_buff != ""){
        rp->name = name_buff;
      }
    }
      
    x++; 
  }

  pdebugI("Successfully updated relays schedule/states\n");

  //Save the config
  return save_config();
}

/*
    Sets up the wifi in either AP (manual mode) or STA (regular mode)
    depending on the pool state.
*/
//returns success on connection, 0 on failure
//static DNSServer         dnsServer;              // Create the DNS object
byte PoolController::connect_wifi(String ssid, String pw){
  byte connected = 0;

  if (ssid == nullptr || ssid == ""){
    pdebugE("Invalid SSID (null or empty) passed. failing\n");
    return 0;
  }

  switch (pool_state){
    case POOL_STATE_MANUAL:
      if (WiFi.getMode() != WIFI_AP_STA){
        WiFi.disconnect();
        WiFi.mode(WIFI_AP_STA);
        IPAddress apIP(10, 10, 10, 1);    // Private network for server
        pdebugI("Configuring AP Mode: %d\n",WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0)));
        pdebugI("Starting softAP: %d\n", WiFi.softAP("Poolnet"));
      }
      else{
        pdebugD("Already running AP mode, ignoring request\n");
        connected = 1;
      }
      break;
    default:
      //If we aren't in STA mode, let's try to connect
      if (WiFi.getMode() != WIFI_STA ||
          WiFi.SSID() != ssid ||
          WiFi.status() != WL_CONNECTED){
        pdebugI("Current WiFi mode is %d\n",WiFi.getMode());
        pdebugI("Attempting to connect to wifi SSID=%s\n",ssid.c_str());

        WiFi.disconnect();
        WiFi.mode(WIFI_STA);
        WiFi.begin(ssid.c_str(),pw.c_str());
        unsigned long now = millis();
        // Wait for connection
        while (millis() - now < WIFI_CONNECT_TIMEOUT){
          if (WiFi.status() != WL_CONNECTED) {
            connected = 1;
            break;
          }
          delay(1000);
        }

        if (connected){
          pdebugI("Connected! IP Address is %s\n",WiFi.localIP().toString().c_str());         // Send the IP address of the ESP8266 to the computer
        }  
        else{
          pdebugE("Connection failed!\n");
        }
      }
      else{
        pdebugD("Already connected to %s, ignoring request\n",ssid.c_str());
        connected = 1;
      }
  }
  return connected;
}

byte PoolController::setJSONWifiDetails(JsonObject& wifi, String& err, byte loading_config){
  pdebugI("Got request to update wifi details\n");

  String ssid = wifi["ssid"];
  String pw = wifi["pw"];

  String ntp_server = wifi["ntp_server"];
  int ntp_tz_offset = wifi["tz_offset"].as<int>();


  //Attempt to update the NTP settings
  if (ntp_server != ""){
    ntp_server_name=ntp_server;
    time_state = POOL_TIME_UNINITIALIZED;
  }
  //HACK: assume we aren't ever running in GMT...probably dumb
  if (ntp_tz_offset != 0){
    gmt_offset = ntp_tz_offset;
  }

  //Attempt to reconnect using the given details (if we're not in the manual pool state)
  if (ssid != "" && pw != ""){
    pdebugI("Attempting to update wifi details to SSID: \"%s\" PW: \"%s\"\n",ssid.c_str(),pw.c_str());
    byte connected = connect_wifi(ssid,pw);
    if (connected){ 
      pdebugI("Successfully connected (or bypassed because we're in manual mode)\n");
      wifi_ssid = ssid;
      wifi_pw = pw;
    }
    //If that didn't work got back to what we had before
    else{
      pdebugE("Error connecting, reverting to previous settings\n");
      connect_wifi(wifi_ssid,wifi_pw);
    }
  }

  //Save the config
  save_config();

  return 1;
}
void PoolController::getJSONSolarDetails(DynamicJsonDocument& info){
  //DynamicJsonDocument info(256);
  JsonObject solar = info.createNestedObject("solar");
  String solar_state_str="internal error";
  switch (solar_state){
    case SOLAR_DISABLED: 
        solar_state_str="disabled";
        break;
    case SOLAR_HEATING:
        solar_state_str="heating";
        break;
    case SOLAR_BYPASS:
        solar_state_str="bypass";
        break;
  }
  solar["enabled"] = solar_enabled ? "on" : "off";
  solar["state"] = solar_state_str;
  solar["target_temp"] = solar_target_temp;
}

byte PoolController::setJSONSolarDetails(JsonObject& solar, String& err, byte loading_config){
  pdebugI("Got request to update solar details\n");

  String enabled = solar["enabled"];
  float target_temp = solar["target_temp"].as<float>();

  //validate the enabled flag
  if (enabled != "on" && enabled != "off"){
    err = "Invalid \"enabled\" provided, must be \"on\" or \"off\"";
    pdebugE("%s\n",err.c_str());
    return 0;
  }

  //validate the target temp (if one was passed)
  if (solar.containsKey("target_temp")){
    if (target_temp < POOL_SOLAR_MIN_TEMP || target_temp > POOL_SOLAR_MAX_TEMP){
      err = "Target temperature is outside the valid range";
      pdebugE("%s\n",err.c_str());
      return 0;
    }
  }

  //Update the settings
  solar_enabled = (enabled == "on") ? 1 : 0;
  solar_target_temp = target_temp;
  solar_state = solar_enabled ? SOLAR_BYPASS : SOLAR_DISABLED; //NOTE: we set it to bypass since it may have been disabled
  pdebugI("Solar enabled: %d\nSolar target temp (f): %.2f\n",solar_enabled,solar_target_temp);

  //Save the config
  return save_config();
}

byte PoolController::validateJSONSensorsUpdate(JsonArray& sensors){
  String unused((const __FlashStringHelper*)TSR_UNUSED_STR);
  String pool=((const __FlashStringHelper*)TSR_WATER_STR);
  String roof=((const __FlashStringHelper*)TSR_SOLAR_STR);
  String ambient=((const __FlashStringHelper*)TSR_AMBIENT_STR);

  //TODO: Make sure there is only one of each role

  //TODO: Make sure only legit roles are present

  //TODO: Make sure there isn't to many sensors

  //TODO: Make sure we don't have duplicate names
  return 1;
}

void PoolController::assignSensorRole(String name, String role){
  String pool=((const __FlashStringHelper*)TSR_WATER_STR);
  String roof=((const __FlashStringHelper*)TSR_SOLAR_STR);
  String ambient=((const __FlashStringHelper*)TSR_AMBIENT_STR);
  if (role == pool)
    pool_water_sensor_name = name;
  else if (role == roof)
    roof_sensor_name = name;
  else if (role == ambient)
    ambient_air_sensor_name = name;
}
    
TempSensor* PoolController::getSensorByName(String name){
  if (name == ""){
    return 0;
  }

  for (int x=0;x<MAX_SENSORS;x++){
    if (temp_sensors[x].name == name){
      return &(temp_sensors[x]);
    }
  }
  return 0;
}

String PoolController::getSensorRole(String name){
  String unused((const __FlashStringHelper*)TSR_UNUSED_STR);
  String pool=((const __FlashStringHelper*)TSR_WATER_STR);
  String roof=((const __FlashStringHelper*)TSR_SOLAR_STR);
  String ambient=((const __FlashStringHelper*)TSR_AMBIENT_STR);

  if (pool_water_sensor_name == name)
    return pool;
  else if (roof_sensor_name == name)
    return roof;
  else if (ambient_air_sensor_name == name)
    return ambient;

  return unused;
}

byte PoolController::setJSONSensorsDetails(JsonArray& sensors, String& err, byte loading_config){
  String role = "";
  String name = ""; 
  byte present=0;

  pdebugI("Setting new JSON sensor details (config_loading=%d)\n",loading_config);

  if (!validateJSONSensorsUpdate(sensors)){
    return 0; //NOTE: the validate method logs the error reason
  }

  pdebugI("Setting %d sensor entries\n",sensors.size());
  this->num_sensors = 0;
  for (JsonVariant s : sensors){
    JsonObject sensor = s.as<JsonObject>(); 
    role = sensor["role"].as<String>();
    name = sensor["name"].as<String>();

    pdebugI("Adding sensor name=\"%s\"\n", name.c_str());
    addSensor(name, POOL_TEMP_SENSOR_MISSING);

    if (role != ""){
      assignSensorRole(name,role);
    }
  }

  //Save a copy of the config
  save_config();
  
  return 1; 
}

void PoolController::update_temperature_sensors(){

  pdebugD("Updating 1-wire temperature sensors\n");
  int device_count = digital_temp_sensors.getDeviceCount();
  digital_temp_sensors.requestTemperatures();
  DeviceAddress sensor_addr; //this is a uint[8] buffer....
  String hex_name;
  float temp_buffer;

  //Nuke all the sensors
  this->num_sensors = 0;

  pdebugD("Iterating found digital 1-wire sensors (%d)\n", device_count);

  //Add back digital sensors we find
  for (int x =0;x<device_count;x++){
    if (digital_temp_sensors.getAddress(sensor_addr,x)){
      hex_name=digitalTempAddrToHex(sensor_addr);
      temp_buffer= digital_temp_sensors.getTempFByIndex(x);
      if (temp_buffer != DEVICE_DISCONNECTED_F){
        pdebugD("Calling addSensor(\"%s\", %f)\n",hex_name.c_str(),temp_buffer);
        addSensor(hex_name,temp_buffer);
      }
      else
        pdebugE("Sensor \"%s\" could not be read. skipping.\n",hex_name.c_str());
    }
  }

  //Update analog thermistor
  pdebugD("Getting analog thermistor value\n");
  float tempF = this->analog_temp->readTempF();
  //int adc = analogRead(A0);
  //pdebugD("Thermistor raw (0-1023): %d\n");
  if (tempF < 0.0 || tempF > 212.0){
      pdebugW("Invalid temperator from analog sensor detected (%f), setting it to an error value\n",tempF);
      tempF = POOL_TEMP_SENSOR_MISSING;
  }
  else{
    pdebugI("Analog sensor temp(f): %f\n",tempF);
  }
  addSensor("analog",tempF);

  pdebugD("loggging any sensor problems\n");

  //Update sensor errors
  if (!isSensorPresent(pool_water_sensor_name))
    log_error(POOL_ERR_POOL_WATER_SENSOR_PROBLEM);
  else
    clear_error(POOL_ERR_POOL_WATER_SENSOR_PROBLEM);

  if (!isSensorPresent(roof_sensor_name))
    log_error(POOL_ERR_ROOF_TEMP_SENSOR_PROBLEM);
  else
    clear_error(POOL_ERR_ROOF_TEMP_SENSOR_PROBLEM);

  if (!isSensorPresent(ambient_air_sensor_name))
    log_error(POOL_ERR_AMBIENT_TEMP_SENSOR_PROBLEM);
  else
    clear_error(POOL_ERR_AMBIENT_TEMP_SENSOR_PROBLEM);
}


// send an NTP request to the time server at the given address
void PoolController::sendNTPpacket(IPAddress &address)
{
  // set all bytes in the buffer to 0
  memset(udp_packet_buffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  udp_packet_buffer[0] = 0b11100011;   // LI, Version, Mode
  udp_packet_buffer[1] = 0;     // Stratum, or type of clock
  udp_packet_buffer[2] = 6;     // Polling Interval
  udp_packet_buffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  udp_packet_buffer[12] = 49;
  udp_packet_buffer[13] = 0x4E;
  udp_packet_buffer[14] = 49;
  udp_packet_buffer[15] = 52;
  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  udp.beginPacket(address, 123); //NTP requests are to port 123
  udp.write(udp_packet_buffer, NTP_PACKET_SIZE);
  udp.endPacket();
}

void PoolController::update_ntp(){
  //Do nothing if we aren't due to ping the server yet and we're already initilized
  if (time_state == POOL_TIME_OK &&
      millis() - this->last_ntp_update < ((unsigned long)(ntp_update_seconds) * 1000L)){
    //pdebugD("Time state already intialized and it's too soon to do it again, skipping\n");
    return;
  }

  IPAddress ntpServerIP; // NTP server's ip address

  while (udp.parsePacket() > 0) ; // discard any previously received packets
  pdebugD("Requesting NTP time\n");
  // get a random server from the pool
  
  int err = WiFi.hostByName(ntp_server_name.c_str(), ntpServerIP);
  if (err != 1){
    time_state = POOL_TIME_NO_INTERNET;
    log_error(POOL_ERR_NO_NTP);
  }
  pdebugD("Using NTP Server: %s\nIP: %s",ntp_server_name.c_str(), ntpServerIP.toString().c_str());

  sendNTPpacket(ntpServerIP);
  uint32_t beginWait = millis();
  while (millis() - beginWait < 1500) {
    int size = udp.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      pdebugD("Receive NTP Response\n");
      udp.read(udp_packet_buffer, NTP_PACKET_SIZE);  // read packet into the buffer
      unsigned long secsSince1900;
      // convert four bytes starting at location 40 to a long integer
      secsSince1900 =  (unsigned long)udp_packet_buffer[40] << 24;
      secsSince1900 |= (unsigned long)udp_packet_buffer[41] << 16;
      secsSince1900 |= (unsigned long)udp_packet_buffer[42] << 8;
      secsSince1900 |= (unsigned long)udp_packet_buffer[43];
      time_t now = secsSince1900 - 2208988800UL + gmt_offset * SECS_PER_HOUR;
      setTime(now);
      this->last_ntp_update = millis();
      time_state = POOL_TIME_OK;

      //Remove any NTP errors from the list (since it just worked)
      clear_error(POOL_ERR_NO_NTP);

      return;
    }
  }

  //If we get here, we failed. Log the error and move on
  time_state = POOL_TIME_ERR;
  log_error(POOL_ERR_NO_NTP);
  pdebugD("NTP Response failure\n");
}


void PoolController::update_pool_state(){

  //If the manual mode switch is set, go to manual (without question)
  if (pool_state != POOL_STATE_UNINITIALIZED){
    manualModeSwitch.update();
    if (manualModeSwitch.read() == POOL_MANUAL_MODE){
      pdebugI("Manual mode switch activated, switching to manual operating mode\n");
      pool_state = POOL_STATE_MANUAL;
    }
  }

  //If we don't have a reliable time, switch to IDLE
  unsigned long unreliable_msec = TIME_UNRELIABLE_AFTER_HOURS;
  unreliable_msec *= 1000L * 60L * 60L; //convert hours to milliseconds

  switch (pool_state){
    case POOL_STATE_NO_NTP:
      if (millis() - last_ntp_update < unreliable_msec){
        pdebugE("Time is reliable again. Resuming schedule!.\n");
        pool_state = POOL_STATE_RUN_SCHEDULE;
      }
      break;
    case POOL_STATE_RUN_SCHEDULE:
      if (millis() - last_ntp_update > unreliable_msec){
        pdebugE("Time is now unreliable (we've gone %d hours without an NTP update). Going IDLE until we know what time it is.\n",TIME_UNRELIABLE_AFTER_HOURS);
        pool_state = POOL_STATE_NO_NTP;
      }
      break;
    case POOL_STATE_MANUAL:
      manualModeSwitch.update();
      if (manualModeSwitch.read() != POOL_MANUAL_MODE){
        pdebugI("Manual mode switch deactivated, switching to running the schedule\n");
        pool_state = POOL_STATE_RUN_SCHEDULE;
      }
      break;
  }

}

void PoolController::log_error(Pool_Error_Code err){
  //TODO
  //check if the error is already in the array

  //add it (if we have room)
}

void PoolController::clear_error(Pool_Error_Code err){
  //TODO
    //If the error is in the array, clear it out
}


String PoolController::digitalTempAddrToHex(DeviceAddress d){
  String hex_name="";
  for (int b = 0;b < sizeof(DeviceAddress); b++){
    hex_name+=byteToHex(d[b]);
  }
  return hex_name;
}


//NOTE: returns HEX_CONV_ERR on error
byte ascii_hex_2_bin_nibble(char nibble)
{
  if (nibble <= '9' && nibble >='0') return (byte)nibble-'0';
  if (nibble >= 'A' && nibble <='F') return ((byte)nibble-'A')+10;
  return HEX_CONV_ERR;
}

byte PoolController::ascii_hex_2_bin(String s){
  if (s.length() != 2){
   return HEX_CONV_ERR;
  }
  return (ascii_hex_2_bin_nibble(s[0])<<4)|ascii_hex_2_bin_nibble(s[1]);
}

//MM: You might be stupid here passing the return as a value (you've been writing too much java)
String PoolController::byteToHex(byte num) {
  char hexDigits[16] = {'0', '1', '2', '3', '4', '5', '6', '7', '8',
    '9', 'A', 'B', 'C', 'D', 'E', 'F'};
  char buff[3];
  buff[0] = hexDigits[num >> 4 & 0xF];
  buff[1] = hexDigits[num & 0xF];
  buff[2] = 0;
  return String(buff);
}

byte PoolController::isSensorPresent(String name){
  for (int x=0;x<num_sensors;x++)
    if (temp_sensors[x].name == name)
      return 1;
  return 0;
}

byte PoolController::isSensorDigital(const char* name){
  if (strlen(name) > 0 && strcmp(name,"analog")) return 1;
  return 0;
}

byte PoolController::isSensorAnalog(const char* name){
  if (strlen(name) > 0 && !strcmp(name,"analog")) return 1;
  return 0;
}


byte PoolController::addSensor(String name, float temp){
  //bail if we're tracking too many sensors
  if (num_sensors >= MAX_SENSORS){  
    pdebugE("too many temp sensors, ignoring: %s",name.c_str());
    return 0;
  }


  temp_sensors[num_sensors].name = name;
  temp_sensors[num_sensors].temp = temp;
  num_sensors++;
  return 1;
}

void PoolController::getJSONGeneralDetails(DynamicJsonDocument& info){
  //DynamicJsonDocument info(512);
  char timebuffer[32];
  sprintf(timebuffer,"%02d:%02d:%02d",hour(),minute(),second());
  JsonObject g = info.createNestedObject("general");
  g["mode"] = POOL_STATE_STRINGS[pool_state];
  g["time"] = timebuffer;
  g["last_time_update"] = last_ntp_update;
  g["last_status_update"] = last_update;
  g["pool_water_sensor_name"] = pool_water_sensor_name;
  g["roof_sensor_name"] = roof_sensor_name;
  g["ambient_air_sensor_name"] = ambient_air_sensor_name;
  JsonArray e = g.createNestedArray("errors");
  for (int x = 0 ;x < num_errors;x++){
    e.add(POOL_ERR_STRINGS[pool_errors[x]]);
  }
}

byte PoolController::setJSONGeneralDetails(JsonObject& general, String& err, byte loading_config){
  pdebugI("Got request to update general details (mode/time)\n");
  //NOTE: This method only lets callers set time and several operating modes
  String mode = general["mode"];
  String time = general["time"];

  if (time != ""){
    TimeElements t;
    int valid_time = createElements(time.c_str(),&t);
    if (!valid_time){
      err = "Invalid time string (must be HH:MM:SS 24 hour format)";
      pdebugE("%s: passed: \"%s\"\n",err.c_str(),time.c_str());
      return 0;
    }

    //Set the time as if it were from an NTP service
    //HACK: just set the date to Jan 1 2020 (since we don't care about date)
    setTime(t.Hour,t.Minute,t.Second,1,1,2020);
    this->last_ntp_update = millis();
    
    //TODO
  }

  //Only allow setting of IDLE/RUN_SCHEDULE modes
  PoolState new_state = POOL_STATE_UNINITIALIZED;
  if (mode == POOL_STATE_RUN_SCHEDULE_STR)
    new_state = POOL_STATE_RUN_SCHEDULE; 
  else if (mode == POOL_STATE_IDLE_STR)
    new_state = POOL_STATE_IDLE;
  else{
    err = "Invalid pool mode passed (only 'run_schedule' and 'idle' accepted)";
    pdebugE("%s: passed: \"%s\"\n",err.c_str(),mode.c_str());
    return 0;
  }

  if (new_state != POOL_STATE_UNINITIALIZED){
    pdebugI("Setting pool to state: %s\n",mode.c_str());
    pool_state = new_state;
  }

  if (!general["pool_water_sensor_name"].isNull()) pool_water_sensor_name=general["pool_water_sensor_name"].as<String>();
  if (!general["roof_sensor_name"].isNull()) roof_sensor_name=general["roof_sensor_name"].as<String>();
  if (!general["ambient_air_sensor_name"].isNull()) ambient_air_sensor_name=general["ambient_air_sensor_name"].as<String>();

  return 1;
}
