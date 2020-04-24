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
  time_state = POOL_TIME_UNINITIALIZED; 
  last_update=0;
  this->num_sensors=0;
  last_ntp_update = 0;
  pool_water_sensor_name = "";
  roof_sensor_name = "";

  //set up the NTP UDP thing
  //TODO: Do we need to care if we're set up as an AP rather than a STA?
  udp.begin(NTP_UDP_PORT); 


  //Get the onewire/temp stuff initialized
  one_wire.begin(DEFAULT_DIGITAL_TEMP_PIN);
  digital_temp_sensors.setOneWire(&one_wire);
  digital_temp_sensors.begin();

  //Set the analog pin to INPUT (we always use it)
  pinMode(DEFAULT_ANALOG_THERM_PIN,INPUT);

  //Set up the relay shift register
  relay_output = new ShiftRegister74HC595<1>(
      DEFAULT_POOL_RELAY_SHIFT_DATA,
      DEFAULT_POOL_RELAY_SHIFT_CLK,
      DEFAULT_POOL_RELAY_SHIFT_LATCH);
  relay_output->setAllLow();

  //Attempt to load the config from SPIFFS
  load_config();
}

void PoolController::populate_defaults(){
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
  DynamicJsonDocument wifi = getJSONWifiDetails();
  DynamicJsonDocument relays = getJSONRelayDetails();
  DynamicJsonDocument sensors = getJSONSensorsDetails();
 
  config.add(wifi);
  config.add(relays);
  config.add(sensors);
  

  if (serializeJsonPretty(config,configFile) == 0){
    pdebugE("Failed to write configuration to \"%s\"\n",CONFIG_FILE_PATH);
    return 0;
  }
  return 1;
}

void PoolController::reset_config(){
  pdebugI("Restting config data to defaults!");
  populate_defaults();
  save_config();
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
      populate_defaults();
      return 0;
    }

    String err; 
    JsonObject wifi = config["wifi"];
    if (!setJSONWifiDetails(wifi,err,1)){
      pdebugE("Error loading wifi details from config file. Reverting to default config. Err:\n%s",err.c_str());
      populate_defaults();
      return 0;
    }

    JsonArray a = config["relays"];
    if (!setJSONRelayDetails(a,err,1)){
      pdebugE("Error loading relay details from config file. Reverting to default config. Err:\n%s",err.c_str());
      populate_defaults();
      return 0;
    }

    a = config["sensors"];
    if (!setJSONSensorsDetails(a,err,1)){
      pdebugE("Error loading sensors details from config file. Reverting to default config. Err:\n%s",err.c_str());
      populate_defaults();
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

void PoolController::update_relays(){

  byte scheduled_on=0;
  RelayState s;
  byte anything_changed = 0;

  pdebugD("PoolController::update_relays() called\n");

  switch (pool_state){
    //If we're in manual/idle states, everything turns off. 
    //Also turn everything off if we're unitialized and still figuring stuff out
    case POOL_STATE_MANUAL:
    case POOL_STATE_IDLE:
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
      /*
      //iterate the schedule and update relay states appropriately
      for (int x = 0;x < MAX_RELAY; x++){
        scheduled_on = determineRelayFromSchedule(config->relay_schedules[x]);

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
      }*/
      break;
  }

  //If we changed any relay states, update the shift register
  if (anything_changed){
    pdebugI("Updating relay outputs: (");
    for (int x=0;x<MAX_RELAY;x++){
      pdebugI("%s=%s\n",relays[x].name.c_str(),POOL_RELAY_STATE_STRINGS[relays[x].state]);
      relay_output->setNoUpdate(x, (relays[x].state == POOL_RELAY_ON ||
                                   relays[x].state == POOL_RELAY_MANUAL_ON) ? HIGH : LOW);
    }
    pdebugI(")\n");
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

  //Bail if we just updated the state (restrict updates to a set interval)
  if (this->last_update - now <= POOL_UPDATE_INTERVAL){
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
}
DynamicJsonDocument PoolController::getJSONWifiDetails(){
  DynamicJsonDocument info(512);
  JsonObject wifi = info.createNestedObject("wifi");
  wifi["ssid"] = wifi_ssid;
  wifi["pw"] = wifi_pw;
  wifi["ntp_server"] = ntp_server_name;
  wifi["tz_offset"] = gmt_offset;
  return info;
}

DynamicJsonDocument PoolController::getJSONSensorsDetails(){
  DynamicJsonDocument info(512);
  
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

  return info;
}

DynamicJsonDocument PoolController::getJSONRelayDetails(){
  DynamicJsonDocument info(2048);
  char timebuffer[32];
  
  JsonArray json_relays = info.createNestedArray("relays");
  //Iterate the temp sensors
  for (int x=0;x<MAX_RELAY;x++){
    JsonObject r = json_relays.createNestedObject();
    r["name"] = relays[x].name;
    r["state"] = (const __FlashStringHelper*)(POOL_RELAY_STATE_STRINGS[relays[x].state]); 

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

  return info;
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

//Returns -1 or a matching relay
int PoolController::getRelayIndexByName(String& name){
  for (int x = 0;x< MAX_RELAY;x++){
    if (relays[x].name == name)
      return x;
  } 
  return -1;
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
  pdebugD("Got request to update relay schedule\n");

  //Make sure the update has the right number of relay elements
  if (relays.size() != MAX_RELAY){
    err = "Incorrect number of relays";
    return 0;
  }

  //Iterate the schedule (validation only)
  PoolDailySchedule sched_buffer;
  JsonArray s;
  String state;
  for (JsonVariant r : relays){
    JsonObject relay = r.as<JsonObject>(); 
    //Parse the schedule and check it for sanity
    s = relay["schedule"];
    if (s.isNull()) continue;
    //NOTE: err is set by parseDailySchedule if it fails
    if (!parseDailySchedule(sched_buffer, s, err)){
      return 0;
    }  

    //Parse the state and check it for sanity
    state = relay["state"].as<String>(); 
    if (state != ""){
      if (state != "on" && state != "off"){
        err = "Invalidate \"state\" provided, must be \"on\" or \"off\"";
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
    //Parse the schedule and update it if present
    s = relay["schedule"];
    if (!s.isNull()){
      //NOTE: err is set by parseDailySchedule if it fails
      parseDailySchedule(sched_buffer, s, err);
       
      //Update our relay
      this->relays[x].schedule = sched_buffer;
    }

    //parse the relay state and update it if present
    state = relay["state"].as<String>(); 
    if (state != ""){
      //If we're setting states as a config, just turn them on/of
      if (loading_config)
        rstate = (state == "on") ? POOL_RELAY_ON : POOL_RELAY_OFF;
      //Otherwise, note that the state change is a manual override of the schedule
      else
        rstate = (state == "on") ? POOL_RELAY_MANUAL_ON : POOL_RELAY_MANUAL_OFF;
      this->relays[x].state = rstate;
    }

    //If we set to update names (config loading only)
    if (loading_config){
      name_buff = relay["name"].as<String>();
      if (name_buff != ""){
        this->relays[x].name = name_buff;
      }
    }
      
    x++; 
  }

  pdebugI("Successfully updated relays schedule/states\n");

  //Save the config
  return save_config();
}

//returns success on connection, 0 on failure
byte PoolController::connect_wifi(String ssid, String pw){
  pdebugI("Attempting to connect to wifi SSID=%s\n",ssid.c_str());
  //TODO: Attempt to reconnect using the given details
  WiFi.disconnect();
  WiFi.begin(ssid.c_str(),pw.c_str());
  unsigned long now = millis();
  byte connected = 0;
  // Wait for connection
  while (millis() - now < WIFI_CONNECT_TIMEOUT){
    if (WiFi.status() != WL_CONNECTED) {
      connected = 1;
      break;
    }
    delay(1000);
  }

  if (connected){
    pdebugI("Attempting to connect to wifi SSID=%s\n",ssid.c_str());
    pdebugI("IP Address is %s\n",WiFi.localIP().toString().c_str());         // Send the IP address of the ESP8266 to the computer
  }  
  return connected;
}

byte PoolController::setJSONWifiDetails(JsonObject& wifi, String& err, byte loading_config){
  pdebugD("Got request to update wifi details\n");

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

  //Attempt to reconnect using the given details
  if (ssid != "" && pw != ""){
    pdebugI("Attempting to update wifi details to SSID: \"%s\" PW: \"%s\"\n",ssid.c_str(),pw.c_str());
    byte connected = connect_wifi(ssid,pw);
    if (connected){ 
      pdebugI("Successfully connected\n");
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

byte PoolController::validateJSONSensorsUpdate(JsonArray& sensors){
  String unused((const __FlashStringHelper*)TSR_UNUSED_STR);
  String pool=((const __FlashStringHelper*)TSR_WATER_STR);
  String roof=((const __FlashStringHelper*)TSR_SOLAR_STR);
  String ambient=((const __FlashStringHelper*)TSR_AMBIENT_STR);

  //TODO: Make sure there is only one of each role

  //TODO: Make sure only legit roles are present

  //TODO: Make sure there isn't to many sensors

  //TODO: Make sure we don't have duplicate names

  //TODO TODO TODO
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

  //TODO: Update analog sensors

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


void PoolController::update_analog_sensor(){
  //TBD
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
  if (time_state != POOL_TIME_UNINITIALIZED &&
      millis() - this->last_ntp_update < ((unsigned long)(ntp_update_seconds) * 1000L))
    return;

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
      clear_error(POOL_ERR_NO_NTP);

      //TODO: Remove any NTP errors from the list (since it just worked)
      return;
    }
  }

  time_state = POOL_TIME_ERR;
  log_error(POOL_ERR_NO_NTP);
  pdebugD("NTP Response failure\n");
  //TODO: log the problem
  //return 0; // return 0 if unable to get the time
}


void PoolController::update_pool_state(){
  //If we don't have a reliable time, switch to IDLE
  //TODO
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
