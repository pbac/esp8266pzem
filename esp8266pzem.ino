

/* FOR COMMENTS PLEASE USE https://github.com/bfritscher/esp8266_PZEM-004T */
/* NOTIFICATION ARE NOT SENT BY EMAIL FOR GISTS :-( */

// VERSION 2 with WiFIManager integration for device_name
#include <FS.h>                   //this needs to be first, or it all crashes and burns...

#include <ESP8266WiFi.h>          // ESP8266 Core WiFi Library (you most likely already have this in your sketch)
#include <Ticker.h>               // for LED status

#include <DNSServer.h>            // Local DNS Server used for redirecting all requests to the configuration portal
#include <ESP8266WebServer.h>     // Local WebServer used to serve the configuration portal
#include <WiFiManager.h>          // https://github.com/tzapu/WiFiManager WiFi Configuration Magic

#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson

//#include <ESP8266httpClient.h>
#include <SoftwareSerial.h>
#include <PZEM004T.h>             // https://github.com/olehs/PZEM004T Power Meter
#include <PubSubClient.h>
#include <Adafruit_NeoPixel.h>
#include "esp8266pzem.h"

#include <math.h>

// select which pin will trigger the configuration portal when set to LOW

const char*   mqttServer = "jix.subeo.net";
const int     mqttPort = 1883;
const char*   mqttUser = "";
const char*   mqttPassword = "";

//-------------------------------------------------------------------
PZEM004T pzem(PZEM004T_RX_PIN,PZEM004T_TX_PIN);  // RX,TX (D2, D1) on NodeMCU
IPAddress ip(192,168,1,1); // required by pzem but not used
Adafruit_NeoPixel strip = Adafruit_NeoPixel(PZEM004T_COUNT, LED_PIN, NEO_RGB + NEO_KHZ800);

//httpClient  http;
Ticker      ticker;

WiFiClient espClient;
PubSubClient client(espClient);

char device_name[40];
WiFiManagerParameter custom_device_name("name", "device name", device_name, 40, " required");

//-------------------------------------------------------------------
bool debug  = true;
bool led    = false;
char mqttBuffer[100];

//------------------------ L E D --------------------------------------
void tick() {
  led = !led;                                               //toggle state
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, led);                         // set pin to the opposite state
}

void configModeCallback (WiFiManager *myWiFiManager) {    //gets called when WiFiManager enters configuration mode
  ticker.attach(0.2, tick);                               //entered config mode, make led toggle faster
}


//------------------------ C O N F I G --------------------------------------
void saveConfigCallback () {                               //callback notifying us of the need to save config
  Serial.println("saving config");
  strcpy(device_name, custom_device_name.getValue());
  DynamicJsonBuffer jsonBuffer;
  JsonObject& json = jsonBuffer.createObject();
  json["device_name"] = device_name;
  File configFile = SPIFFS.open("/config.json", "w");
  if (!configFile) {
     Serial.println("failed to open config file for writing");
  }
  json.printTo(Serial);
  json.printTo(configFile);
  configFile.close();  
}

boolean readConfig() {
  //SPIFFS.format();                                          // clean FS, for testing
  Serial.println("mounting FS...");                           //read configuration from FS json
  if (SPIFFS.begin()) {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      Serial.println("reading config file");                   //file exists, reading and loading
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        std::unique_ptr<char[]> buf(new char[size]);            // Allocate a buffer to store contents of the file.

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          Serial.println("\nparsed json");
          strcpy(device_name, json["device_name"]);
          return true;
        } else {
          Serial.println("failed to load json config");
          return false;
        }
      }
    }
  } else {
    Serial.println("failed to mount FS");
    return false;
  }
}

//------------------------ S E T U P --------------------------------------
void setup() {
  Serial.begin(115200);
  Serial.println("Starting setup");
  
  pinMode(LED_BUILTIN, OUTPUT);                                           // set led pin as output
  ticker.attach(0.6, tick);                                               // start ticker with 0.6 because we start in AP mode and try to connect
  
  WiFiManager wifiManager;                                                // Local intialization. Once its business is done, there is no need to keep it around
  // wifiManager.setDebugOutput(false);
  // wifiManager.resetSettings();                                         // reset settings - for testing
  wifiManager.setTimeout(180);                                            // sets timeout until configuration portal gets turned off useful to make it all retry or go to sleep in seconds
  wifiManager.setSaveConfigCallback(saveConfigCallback);                  // set config save notify callback
  wifiManager.setAPCallback(configModeCallback);                          // set callback that gets called when connecting to previous WiFi fails, and enters Access Point mode
  wifiManager.addParameter(&custom_device_name);                          // add all your parameters here
  // wifiManager.setCustomHeadElement("<style>html{filter: invert(100%); -webkit-filter: invert(100%);}</style>");

  if(!readConfig()) {
    wifiManager.resetSettings();
  }
  
  String ssid = "POWER_MONITOR_" + String(ESP.getChipId());
  if(!wifiManager.autoConnect(ssid.c_str(), NULL)) {                      // fetches ssid and pass and tries to connect if it does not connect it starts an access point with auto generated name
    Serial.println("failed to connect and hit timeout");                  //  from 'ESP' and the esp's Chip ID use and goes into a blocking loop awaiting configuration
    ESP.reset();                                                          // reset and try again, or maybe put it to deep sleep
    delay(5000);
  }

  ticker.detach();                                                         // if you get here you have connected to the WiFi
  digitalWrite(LED_BUILTIN, LOW);                                          // keep LED on
 
  Serial.println("initiating multiplexer to 0 ");   
   // Setting the 3 pins to output mode.
  pinMode(ADDRESS_PIN1, OUTPUT);
  pinMode(ADDRESS_PIN2, OUTPUT);
  pinMode(ADDRESS_PIN3, OUTPUT);
  selectDevice(0);

 Serial.println("setting pzem ip to " + String(ip));   
  pzem.setAddress(ip);
  delay(5000);
  digitalWrite(LED_BUILTIN, HIGH);                                          // keep LED off

  Serial.println("setting mqtt server to " + String(mqttServer));   
  client.setServer(mqttServer, 1883);                                      //Configuration de la connexion au serveur MQTT
  client.setCallback(mqttCallback);                                        //La fonction de callback qui est executée à chaque réception de message  
  client.disconnect();
  mqttConnect();
                               
  strip.begin();
  strip.show(); // Initialize all pixels to 'off'                             
  initLed();
  strip.setBrightness(LED_BRIGHTNESS);


}

//------------------------ M Q T T --------------------------------------
void mqttCallback(char* topic, byte* payload, unsigned int length) {

  int i = 0;
  if ( debug ) {
    Serial.println("Message recu =>  topic: " + String(topic));
    Serial.print(" | longueur: " + String(length,DEC));
  }
  
  for(i=0; i<length; i++) {                                                   // create character buffer with ending null terminator (string)
    mqttBuffer[i] = payload[i];
  }
  mqttBuffer[i] = '\0';
  
  String msgString = String(mqttBuffer);
  if ( debug ) {
    Serial.println("Payload: " + msgString);
  }
}

void mqttConnect() {
  //Serial.println("Checking MQTT connexion : " + String(client.connected()));
  while (!client.connected()) {
    Serial.print("Connecting to MQTT server...");
    if (client.connect("node131", mqttUser, mqttPassword)) {
      Serial.println("MQTT connexion OK");
    } else {
      Serial.print("MQTT Connexion failed with state ");
      Serial.print(client.state());
      delay(5000);
    }
  }

}

void mqttSend(const char* category, int sensor, char unit, float value) {
  mqttConnect();
  String topic = String(device_name) + "/sensor/" + String(category) + String(sensor) + "/" + String(unit);
  client.publish(topic.c_str(), String(value, 2).c_str(), false); 
  if ( debug ) {
    String msg   = "MQTT " + String(topic) + ": " + String(value, 2) + String(unit);
    Serial.println(msg);
  }
}


//------------------------ P Z E M  --------------------------------------
float getMeasure(char unit) {
  int i = 0;
  float r = -1.0;
  do {
    switch (unit) {
      case 'V':
        r = pzem.voltage(ip);
        break;
      case 'A':
        r = pzem.current(ip);
        break;
      case 'W':
        r = pzem.power(ip);
        break;
      case 'E':
        r = pzem.energy(ip);
        break;
    }
    wdt_reset();
    i++;
  } while ( i < MAX_ATTEMPTS && r < 0.0);
  return r;
}

float sendMeasures(int sensor) {
  int   i;
  char  units[] = "VAWE";
  char  unit;
  float measure;
  float wattReturn;

  for (i = 0; i < strlen(units); i++) {
    unit = units[i];
    measure = getMeasure(unit);
    mqttSend("energy", sensor, unit, measure);
    if (unit == 'W') {
      wattReturn = measure;
    }
  }
  return wattReturn;
}

void selectDevice(int channel) {
    if (channel & 1) {
      digitalWrite(ADDRESS_PIN1,HIGH);
    } else {
      digitalWrite(ADDRESS_PIN1,LOW);
    }
    if (channel & 2) {
      digitalWrite(ADDRESS_PIN2,HIGH);
    } else {
      digitalWrite(ADDRESS_PIN2,LOW);
    }
    if (channel & 4) {
      digitalWrite(ADDRESS_PIN3,HIGH);
    } else {
      digitalWrite(ADDRESS_PIN3,LOW);
    }
    delay(150);
}

//------------------------ T E M P E R A T U R E   --------------------------------------

int analogRead() {
  int analogData = 0;

  for(int i = 0; i < 10; i++) {
    analogData += analogRead(A0);
    delay(50);
  }

  analogData = analogData / 10;
  return analogData;
}

float getTemperature() {
  
  int     R0 = 10000;         // Thermistor resistance at room temp  ????????????????????
  int     Rs = 110000;        // Bridge 2nd resistor ????????????????????
  int     T0_deg = 25;        // Room Temp
  int     T0_Coeff = 3950;    // Thermistor coefficient at room temp
  float   K = 273.15;         // Kelvin convertion
  //float   Vcc = 3.3;
  float   Va;                 // Analog Volt reading
  float   Rfactor;            // Steinhart formula Value
  float   RBase;            // Steinhart formula Value
  float   steinhart;          // Steinhart formula Value
  float   T;                  // Temperature
  float   R;                  // Thermistor resistance
  char    unit = 'C';         // MQTT unit

  Va = (float) analogRead();
  R = (Va * Rs) / (1023 - Va);

  Rfactor = R/R0;
  RBase =  (1/(K+T0_deg));
  steinhart = 1/(log(Rfactor) / T0_Coeff + RBase); 

  T = steinhart - K;   
  mqttSend("temp", 0, unit, T);

  return T; 

}

//------------------------ N E O   P I X E L  --------------------------------------
Rgb convertToRgb(float watt) {

  Rgb color;
  int red;
  int green;
  int blue;

  int iWatt;


  iWatt = (int) (watt); 

  if (iWatt > 211) {iWatt = (iWatt + 211) / 2;}   // 300 -> 256
  if (iWatt > 418) {iWatt = (iWatt + 418) / 2;}   // 1000 -> 512
  if (iWatt > 912) {iWatt = (iWatt + 912) / 2;}   // 3500 -> 1024
  if (iWatt > 1224) {iWatt = (iWatt + 1224) / 2;} // 6000 -> 1280

  red   = 30;
  green = 30;
  blue  = 30;

  if (iWatt < 0) {     // PZEM return (-1) when read error.
    red   = 30;
    green = 30;
    blue  = 30;
  }

  if (iWatt == 0) {
    red   = 0;
    green = 30;
    blue  = 0;
  } 
  if (iWatt > 0) {      // Dark Blue -> Blue
    red   = 0;
    green = 0;
    blue  = iWatt;
  } 
  if (iWatt >= 256) {   // Blue -> Cyan
    red   = 0;
    green = iWatt - 256;
    blue  = 255;
  } 
  if (iWatt >= 512) {   // Cyan -> yellow
    red   = iWatt - 512;
    green = 255;
    blue  = 768 - iWatt;
  } 
  if (iWatt >= 768) {   // yellow -> red
    red   = 255;
    green = 1024 - iWatt;
    blue  = 0;
  } 
  if (iWatt >= 1024) {  // red -> purple
    red   = 255;
    green = 0;
    blue  = iWatt - 1024;
  } 
  if (iWatt >= 1280) {  // purple -> white
    red   = 255;
    green = iWatt - 1280;
    blue  = 255;
  } 
  if (iWatt >= 1536) {  // White
    red   = 255;
    green = 255;
    blue  = 255;
  } 


  color.r = (byte) red;
  color.g = (byte) green;
  color.b = (byte) blue;
  return color;
}

void setColor(int ledIndex, Rgb color) {
  strip.setPixelColor(ledIndex, color.r, color.g, color.b);
}

void initLed() {
  Rgb   color;
  int bright;
  int led;
 
  color.r = 255;
  color.g = 0;
  color.b = 0;
  strip.setBrightness(10);

  for (led = 0; led < PZEM004T_COUNT; led++) {
    setColor(led, color);
    delay(150);
    strip.show();
    Serial.println("init led " + String(led));   

  }

  for (bright = 10; bright <= LED_BRIGHTNESS; bright+=10) {
    strip.setBrightness(bright);
    strip.show();
    delay(150);
  }
  
}

//------------------------ L O O P --------------------------------------

void loop() {

  int   sensor;
  float actualWatt;
  float boardTemp;
  Rgb   color;
 
  for (sensor = 0; sensor < PZEM004T_COUNT; sensor++) {
    // TODO: selectionner le bon sensor
    selectDevice(sensor);

    //digitalWrite(ADDRESS_PIN1,(sensor & 1)) // Not sure HIGH & LOW are True/false
    //digitalWrite(ADDRESS_PIN2,(sensor & 2))
    //digitalWrite(ADDRESS_PIN3,(sensor & 4))

    actualWatt = sendMeasures(sensor);
    client.loop();
    client.disconnect();
    color = convertToRgb(actualWatt);
    setColor(sensor, color);
    strip.show();
    boardTemp = getTemperature();
    delay(1000);
  }
  
  // is configuration portal requested?
  if ( digitalRead(TRIGGER_PIN) == LOW ) {
    digitalWrite(LED_BUILTIN, LOW);  
    WiFiManager wifiManager;
    wifiManager.resetSettings();
    SPIFFS.format();
    ESP.reset();
    delay(5000);
  }
}
