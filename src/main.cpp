#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266mDNS.h>
#include <WiFiClient.h>
#include <WiFiUdp.h>
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>
#include "RemoteDebug.h"
#include <ArduinoOTA.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <FS.h>

#include <ShiftRegister74HC595.h>
#include <Bounce2.h>
#include <TimeLib.h>
#include <DNSServer.h>

//Turn on wifi debugging
#define DEBUG_ESP_WIFI 1
#define DEBUG_ESP_PORT Serial

#ifndef LED_BUILTIN
#define LED_BUILTIN D0
#endif


#include "Constants.h"
#include "Relay.h"
#include "PoolController.h"


//const byte        DNS_PORT = 53;          // Capture DNS requests on port 53
//IPAddress         apIP(10, 10, 10, 1);    // Private network for server
//DNSServer         dnsServer;              // Create the DNS object

//Global remote debug
RemoteDebug POOL_DEBUG;
RemoteDebug* debug = &POOL_DEBUG;

//PoolConfig POOL_CONFIG;
PoolController POOL_CONTROLLER(&POOL_DEBUG);

//Our web server
ESP8266WebServer SERVER(80);

void handleNotFound(){
  digitalWrite(LED_BUILTIN, 0);
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += SERVER.uri();
  message += "\nMethod: ";
  message += (SERVER.method() == HTTP_GET)?"GET":"POST";
  message += "\nArguments: ";
  message += SERVER.args();
  message += "\n";
  for (uint8_t i=0; i<SERVER.args(); i++){
    message += " " + SERVER.argName(i) + ": " + SERVER.arg(i) + "\n";
  }
  pdebugD("%s",message.c_str());
  SERVER.send(404, "text/plain", message);
  digitalWrite(LED_BUILTIN, 1);

  /*

  digitalWrite(LED_BUILTIN, 0);
  File dataFile = SPIFFS.open("/test.html", "r");   //open file to read
  if (!dataFile) {
      Serial.println("file open failed");
  }

  if (SERVER.streamFile(dataFile, "text/html") != dataFile.size()) {}    //a lot happening here
  dataFile.close();

  digitalWrite(LED_BUILTIN, 1);*/
}

void setSensors(){
  DynamicJsonDocument sched(2048);
  //pdebugI("MM: \"%s\"\n",SERVER.arg("plain").c_str());
  DeserializationError error = deserializeJson(sched,SERVER.arg("plain"));

  if (error == DeserializationError::Ok){
    pdebugD("Successfully parsed schedule update request, submitting to controller\n");
    String err="";
    JsonArray sensors = sched["sensors"];
    byte success = POOL_CONTROLLER.setJSONSensorsDetails(sensors,err);

    if (success == 0){
      pdebugW("Failed to update JSON sensors details:\n%s\n",err.c_str());
      SERVER.send(400, "text/plain", err);
      return;
    }
   
    SERVER.send(200,"text/plain","");
    return;
  }

  //If we get here, there is an error with the JSON (either syntax or semnatics)
  //TODO: More useful error return
  SERVER.send(400, "text/plain", error.c_str());
}

void setRelays(){
  DynamicJsonDocument sched(2048);
  //pdebugI("MM: \"%s\"\n",SERVER.arg("plain").c_str());
  DeserializationError error = deserializeJson(sched,SERVER.arg("plain"));

  if (error == DeserializationError::Ok){
    pdebugD("Successfully parsed schedule update request, submitting to controller\n");
    String err="";
    JsonArray relays = sched["relays"];
    byte success = POOL_CONTROLLER.setJSONRelayDetails(relays,err);

    if (success == 0){
      pdebugW("Failed to update JSON relay details:\n%s\n",err.c_str());
      SERVER.send(400, "text/plain", err);
      return;
    }
   
    SERVER.send(200,"text/plain","");
    return;
  }

  //If we get here, there is an error with the JSON (either syntax or semnatics)
  //TODO: More useful error return
  SERVER.send(400, "text/plain", error.c_str());
}

void getSchedule(){
    digitalWrite(LED_BUILTIN, 0);
    pdebugD("Getting relay schedule from pool controller\n");
    //DynamicJsonDocument jsonBuffer=POOL_CONTROLLER.dumpJSONRelaySchedule(); 
    DynamicJsonDocument jsonBuffer(100); 
    jsonBuffer["now"] = millis();
    String status;
    serializeJson(jsonBuffer, status);
    SERVER.sendHeader("Access-Control-Allow-Origin", "*");
    SERVER.send(200,"application/json",status);
    digitalWrite(LED_BUILTIN, 1);
}

void resetController(){
    digitalWrite(LED_BUILTIN, 0);
    pdebugD("Resetting pool controller config to defaults\n");
    POOL_CONTROLLER.reset_config();
    DynamicJsonDocument jsonBuffer(100); 
    jsonBuffer["now"] = millis();
    //jsonBuffer["success"] = (POOL_CONTROLLER.initialized == 1);
    String status;
    serializeJson(jsonBuffer, status);
    SERVER.sendHeader("Access-Control-Allow-Origin", "*");
    SERVER.send(200,"application/json",status);
    digitalWrite(LED_BUILTIN, 1);
}


void tempRequest(){

    digitalWrite(LED_BUILTIN, 0);

    pdebugD("Getting temp sensors from pool controller\n");

    DynamicJsonDocument jsonBuffer(512);
    POOL_CONTROLLER.getJSONSensorsDetails(jsonBuffer); 
    jsonBuffer["now"] = millis();
    String status;
    serializeJson(jsonBuffer, status);
    SERVER.sendHeader("Access-Control-Allow-Origin", "*");
    SERVER.send(200,"application/json",status);
    digitalWrite(LED_BUILTIN, 1);
}

void getRelays(){
    digitalWrite(LED_BUILTIN, 0);
    pdebugD("Getting relay states from pool controller\n");
    DynamicJsonDocument jsonBuffer(2048);
    POOL_CONTROLLER.getJSONRelayDetails(jsonBuffer); 
    jsonBuffer["now"] = millis();
    String status;
    serializeJson(jsonBuffer, status);
    SERVER.sendHeader("Access-Control-Allow-Origin", "*");
    SERVER.send(200,"application/json",status);
    digitalWrite(LED_BUILTIN, 1);
}

void getWifi(){
    digitalWrite(LED_BUILTIN, 0);
    pdebugD("Getting wifi info from pool controller\n");
    DynamicJsonDocument jsonBuffer(512);
    POOL_CONTROLLER.getJSONWifiDetails(jsonBuffer);
    jsonBuffer["now"] = millis();
    String status;
    serializeJson(jsonBuffer, status);
    SERVER.sendHeader("Access-Control-Allow-Origin", "*");
    SERVER.send(200,"application/json",status);
    digitalWrite(LED_BUILTIN, 1);
}

void getSolar(){
    digitalWrite(LED_BUILTIN, 0);
    pdebugD("Getting solar info from pool controller\n");
    DynamicJsonDocument jsonBuffer(256);
    POOL_CONTROLLER.getJSONSolarDetails(jsonBuffer);
    jsonBuffer["now"] = millis();
    String status;
    serializeJson(jsonBuffer, status);
    SERVER.sendHeader("Access-Control-Allow-Origin", "*");
    SERVER.send(200,"application/json",status);
    digitalWrite(LED_BUILTIN, 1);
}

void setSolar(){
  DynamicJsonDocument sched(256);
  DeserializationError error = deserializeJson(sched,SERVER.arg("plain"));

  if (error == DeserializationError::Ok){
    pdebugD("Successfully parsed solar update request, submitting to controller\n");
    String err="";
    JsonObject solar = sched.as<JsonObject>();
    byte success = POOL_CONTROLLER.setJSONSolarDetails(solar,err);

    if (success == 0){
      pdebugW("Failed to update JSON solar details:\n%s\n",err.c_str());
      SERVER.send(400, "text/plain", err);
      return;
    }
   
    SERVER.send(200,"text/plain","");
    return;
  }

  //If we get here, there is an error with the JSON (either syntax or semnatics)
  //TODO: More useful error return

  SERVER.send(400, "text/plain", "Invalid JSON");
}

void getGeneral(){
    digitalWrite(LED_BUILTIN, 0);
    pdebugD("Getting general info from pool controller\n");
    DynamicJsonDocument jsonBuffer(512);
    POOL_CONTROLLER.getJSONGeneralDetails(jsonBuffer); 
    jsonBuffer["now"] = millis();
    String status;
    serializeJson(jsonBuffer, status);
    SERVER.sendHeader("Access-Control-Allow-Origin", "*");
    SERVER.send(200,"application/json",status);
    digitalWrite(LED_BUILTIN, 1);
}

void setWifi(){
  DynamicJsonDocument sched(256);
  DeserializationError error = deserializeJson(sched,SERVER.arg("plain"));

  if (error == DeserializationError::Ok){
    pdebugD("Successfully parsed solar update request, submitting to controller\n");
    String err="";
    JsonObject solar = sched.as<JsonObject>();
    byte success = POOL_CONTROLLER.setJSONWifiDetails(solar,err);

    if (success == 0){
      pdebugW("Failed to update JSON solar details:\n%s\n",err.c_str());
      SERVER.send(400, "text/plain", err);
      return;
    }
   
    SERVER.send(200,"text/plain","");
    return;
  }

  //If we get here, there is an error with the JSON (either syntax or semnatics)
  //TODO: More useful error return

  SERVER.send(400, "text/plain", "Invalid JSON");
}


void setGeneral(){
  DynamicJsonDocument sched(256);
  DeserializationError error = deserializeJson(sched,SERVER.arg("plain"));

  if (error == DeserializationError::Ok){
    pdebugD("Successfully parsed solar update request, submitting to controller\n");
    String err="";
    JsonObject solar = sched.as<JsonObject>();
    byte success = POOL_CONTROLLER.setJSONGeneralDetails(solar,err);

    if (success == 0){
      pdebugW("Failed to update JSON solar details:\n%s\n",err.c_str());
      SERVER.send(400, "text/plain", err);
      return;
    }
   
    SERVER.send(200,"text/plain","");
    return;
  }

  //If we get here, there is an error with the JSON (either syntax or semnatics)
  //TODO: More useful error return

  SERVER.send(400, "text/plain", "Invalid JSON");
}

void getEverything(){
    digitalWrite(LED_BUILTIN, 0);
    pdebugD("Getting everything from pool controller\n");
    DynamicJsonDocument config(4096);
    POOL_CONTROLLER.getJSONWifiDetails(config);
    //NOTE Don't return our wifi password (if somebody puts the controller in manual
    // it could result in a real-world security issue)
    config["wifi"].remove("pw");

    POOL_CONTROLLER.getJSONRelayDetails(config);
    POOL_CONTROLLER.getJSONSensorsDetails(config);
    POOL_CONTROLLER.getJSONSolarDetails(config);
    POOL_CONTROLLER.getJSONGeneralDetails(config);
   
    config["now"] = millis();
    String status;
    serializeJson(config, status);
    SERVER.sendHeader("Access-Control-Allow-Origin", "*");
    SERVER.send(200,"application/json",status);
    digitalWrite(LED_BUILTIN, 1);
}

void setup()
{
  SPIFFS.begin();

  Serial.begin(9600);


  // initialize LED digital pin as an output.
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, 1); //HACK: The LED is reverse biased, we turn it off here 
  //      to spare the LD1117 on the v0.2 board

  POOL_DEBUG.setSerialEnabled(true);
  POOL_DEBUG.begin("pool_controller");
  POOL_DEBUG.setResetCmdEnabled(true);

    // if DNSServer is started with "*" for domain name, it will reply with
    // provided IP to all DNS request
    //dnsServer.start(DNS_PORT, "*", apIP);

    //SERVER.on("/info", HTTP_GET, infoRequest);
    //SERVER.on("/infojson", HTTP_GET, infoJSONRequest);
    //SERVER.on("/update", HTTP_GET, targetRequest);
    //SERVER.serveStatic("/recipe.html", SPIFFS, "/recipe.html");
    SERVER.on("/sensors",HTTP_GET,tempRequest);
    SERVER.on("/sensors",HTTP_POST,setSensors);
    SERVER.on("/wifi",HTTP_POST,setWifi);
    SERVER.on("/wifi",HTTP_GET,getWifi);
    SERVER.on("/solar",HTTP_GET,getSolar);
    SERVER.on("/solar",HTTP_POST,setSolar);
    SERVER.on("/relays",HTTP_GET,getRelays);
    SERVER.on("/relays",HTTP_POST,setRelays);
    SERVER.on("/reset",HTTP_GET,resetController);
    SERVER.on("/everything",HTTP_GET,getEverything);
    SERVER.on("/general",HTTP_GET,getGeneral);
    SERVER.on("/general",HTTP_POST,setGeneral);

    SERVER.onNotFound(handleNotFound);

    SERVER.begin();

    MDNS.begin(HOSTNAME);
    MDNS.addService("http","tcp",80);

    //DEBUG set to a fake time
    //setTime(0,0,0,1,1,2020);
  
 ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_FS
      type = "filesystem";
      SPIFFS.end();
    }

    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
    //Start our OTA service
    ArduinoOTA.setPassword(OTA_PASSWORD);
    ArduinoOTA.setHostname(HOSTNAME);
    ArduinoOTA.begin();

    //Load the pool controller config
    //NOTE: We do this here instead of in the constructor because
    //      we need SPIFFS started first
    POOL_CONTROLLER.load_config();

}

void loop()
{
    MDNS.update();

    POOL_CONTROLLER.update(); 
    //delay(1000);

    //digitalClockDisplay();

    //Uncomment for DNS server running (AP mode stuff)
    //dnsServer.processNextRequest();

    //Uncomment for OTA stuff
    ArduinoOTA.handle();

    SERVER.handleClient();
    POOL_DEBUG.handle();
}
