#include <Arduino.h>
#include <ESP8266WiFi.h>
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


const byte        DNS_PORT = 53;          // Capture DNS requests on port 53
IPAddress         apIP(10, 10, 10, 1);    // Private network for server
DNSServer         dnsServer;              // Create the DNS object

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

void setRelays(){
  DynamicJsonDocument sched(2048);
  DeserializationError error = deserializeJson(sched,SERVER.arg("plain"));

  if (error == DeserializationError::Ok){
    pdebugD("Successfully parsed schedule update request, submitting to controller");
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

  SERVER.send(400, "text/plain", "Invalid JSON");
}

void getSchedule(){
    digitalWrite(LED_BUILTIN, 0);
    pdebugD("Getting relay schedule from pool controller\n");
    //DynamicJsonDocument jsonBuffer=POOL_CONTROLLER.dumpJSONRelaySchedule(); 
    DynamicJsonDocument jsonBuffer(100); 
    jsonBuffer["now"] = millis();
    String status;
    serializeJsonPretty(jsonBuffer, status);
    SERVER.sendHeader("Access-Control-Allow-Origin", "*");
    SERVER.send(200,"application/json",status);
    digitalWrite(LED_BUILTIN, 1);
}

void resetController(){
    digitalWrite(LED_BUILTIN, 0);
    pdebugD("Resetting pool controller config to defaults\n");
    //POOL_CONTROLLER.reset_config(); 
    POOL_CONTROLLER.load_config();
    DynamicJsonDocument jsonBuffer(100); 
    jsonBuffer["now"] = millis();
    //jsonBuffer["success"] = (POOL_CONTROLLER.initialized == 1);
    String status;
    serializeJsonPretty(jsonBuffer, status);
    SERVER.sendHeader("Access-Control-Allow-Origin", "*");
    SERVER.send(200,"application/json",status);
    digitalWrite(LED_BUILTIN, 1);
}


void tempRequest(){

    digitalWrite(LED_BUILTIN, 0);

    pdebugD("Getting temp sensors from pool controller\n");

    DynamicJsonDocument jsonBuffer=POOL_CONTROLLER.getJSONSensorsDetails(); 
    jsonBuffer["now"] = millis();
    String status;
    serializeJsonPretty(jsonBuffer, status);
    SERVER.sendHeader("Access-Control-Allow-Origin", "*");
    SERVER.send(200,"application/json",status);
    digitalWrite(LED_BUILTIN, 1);
}

void getRelays(){
    digitalWrite(LED_BUILTIN, 0);
    pdebugD("Getting relay states from pool controller\n");
    DynamicJsonDocument jsonBuffer=POOL_CONTROLLER.getJSONRelayDetails(); 
    jsonBuffer["now"] = millis();
    String status;
    serializeJsonPretty(jsonBuffer, status);
    SERVER.sendHeader("Access-Control-Allow-Origin", "*");
    SERVER.send(200,"application/json",status);
    digitalWrite(LED_BUILTIN, 1);
}

void getWifi(){
    digitalWrite(LED_BUILTIN, 0);
    pdebugD("Getting wifi info from pool controller\n");
    DynamicJsonDocument jsonBuffer=POOL_CONTROLLER.getJSONWifiDetails(); 
    jsonBuffer["now"] = millis();
    String status;
    serializeJsonPretty(jsonBuffer, status);
    SERVER.sendHeader("Access-Control-Allow-Origin", "*");
    SERVER.send(200,"application/json",status);
    digitalWrite(LED_BUILTIN, 1);
}

void setup()
{
  SPIFFS.begin();

  POOL_CONTROLLER.load_config();
  //initConfig();
  //loadConfig(&POOL_CONFIG,debug);
  //POOL_CONTROLLER.load_config(&POOL_CONFIG);

  Serial.begin(9600);

  // initialize LED digital pin as an output.
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, 1); //HACK: The LED is reverse biased, we turn it off here 
  //      to spare the LD1117 on the v0.2 board

  //Uncomment to start in AP mode
  /*WiFi.mode(WIFI_AP_STA);
    Serial.print("Configuring AP Mode: ");
    Serial.println(WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0)));
    Serial.print("Starting softAP: ");
    Serial.println(WiFi.softAP("RibNet", "muddbutt", 9, false));
  */

  POOL_DEBUG.setSerialEnabled(true);
  POOL_DEBUG.begin("pool_controller");
  POOL_DEBUG.setResetCmdEnabled(true);

    //Uncomment for AP mode stuff
    // if DNSServer is started with "*" for domain name, it will reply with
    // provided IP to all DNS request
    //dnsServer.start(DNS_PORT, "*", apIP);

    //SERVER.on("/info", HTTP_GET, infoRequest);
    //SERVER.on("/infojson", HTTP_GET, infoJSONRequest);
    //SERVER.on("/update", HTTP_GET, targetRequest);
    //SERVER.serveStatic("/recipe.html", SPIFFS, "/recipe.html");
    SERVER.on("/temp",HTTP_GET,tempRequest);
    SERVER.on("/wifi",HTTP_GET,getWifi);
    SERVER.on("/relays",HTTP_GET,getRelays);
    SERVER.on("/relays",HTTP_POST,setRelays);
    SERVER.on("/reset",HTTP_GET,resetController);

    SERVER.onNotFound(handleNotFound);

    SERVER.begin();

    //DEBUG set to a fake time
    //setTime(0,0,0,1,1,2020);


}

void printDigits(int digits) {
 // utility function for digital clock display: prints preceding colon and leading 0
 Serial.print(":");
 if (digits < 10)
 Serial.print('0');
 Serial.print(digits);
}

void digitalClockDisplay() {
 // digital clock display of the time
 Serial.print(hour());
 printDigits(minute());
 printDigits(second());
 Serial.print(" ");
 Serial.print(day());
 Serial.print(" ");
 Serial.print(month());
 Serial.print(" ");
 Serial.print(year());
 Serial.println();
}

void loop()
{
    //rdebugVln("Tick in loop...");
    POOL_CONTROLLER.update(); 
    delay(1000);

    //digitalClockDisplay();

    //Uncomment for DNS server running (AP mode stuff)
    //dnsServer.processNextRequest();

    //Uncomment for OTA stuff
    //ArduinoOTA.handle();
    SERVER.handleClient();
    POOL_DEBUG.handle();
}
