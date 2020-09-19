#include <FS.h>                   //this needs to be first, or it all crashes and burns...

#include <ESP8266WiFi.h>          // ESP8266 Core WiFi Library (you most likely already have this in your sketch)

#include <DNSServer.h>            // Local DNS Server used for redirecting all requests to the configuration portal
#include <WiFiManager.h>          // https://github.com/tzapu/WiFiManager WiFi Configuration Magic

#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson

//#include <ESP8266httpClient.h>
#include <SoftwareSerial.h>
#include <PZEM004T.h>             // https://github.com/olehs/PZEM004T Power Meter
#include <PubSubClient.h>
#include <Adafruit_NeoPixel.h>
#include <math.h>

#include "esp8266pzem.h"
#include "secret.h"                // Store all private info (SECRET_*)

// WIFI connection
const char*     ssid            = SECRET_WIFI_SSID;            // The SSID (name) of the Wi-Fi network you want to connect to
const char*     password        = SECRET_WIFI_PASSWORD;       // The password of the Wi-Fi network
char            localIp[20];

// MQTT server connection
const char*     mqttServer      = SECRET_MQTT_SERVERNAME;
const int       mqttPort        = 1883;
const char*     mqttUser        = SECRET_MQTT_USER;
const char*     mqttPassword    = SECRET_MQTT_PASSWORD;
const char*     deviceName      = "node131";

//-------------------------------------------------------------------

PZEM004T pzem(PZEM004T_RX_PIN,PZEM004T_TX_PIN);  // RX,TX (D2, D1) on NodeMCU
IPAddress ip(192,168,1,1); // required by pzem but not used

Adafruit_NeoPixel strip = Adafruit_NeoPixel(PZEM004T_COUNT, LED_PIN, NEO_RGB + NEO_KHZ800);

WiFiClient      wifiCnx;
PubSubClient    mqttCnx(wifiCnx);

//-------------------------------------------------------------------
bool led    = false;
char mqttBuffer[100];

//------------------------ S E T U P --------------------------------------
void setup() {
    // SERIAL
    Serial.begin(115200);
    debugln("Starting setup");

    //PIN
    pinMode(LED_BUILTIN, OUTPUT);                                           // set led pin as output
	pinMode(ADDRESS_PIN1, OUTPUT);
	pinMode(ADDRESS_PIN2, OUTPUT);
	pinMode(ADDRESS_PIN3, OUTPUT);
	digitalWrite(LED_BUILTIN, LOW);                                          // keep LED off

    // WIFI Connection
    WiFi.begin(ssid, password);         
    debug(VERSION);
    debug(" Connecting to ");
    debug(ssid); 
    debugln(" ...");

    int i = 0;
    while (WiFi.status() != WL_CONNECTED) { // Wait for the Wi-Fi to connect
        delay(100);
        debug(++i); 
        debug(' ');
    }
    // Local IP Copy
    String sLocalIp = String() + WiFi.localIP()[0] + "." + WiFi.localIP()[1] + "." + WiFi.localIP()[2] + "." + WiFi.localIP()[3];
    strcpy(localIp,sLocalIp.c_str());

    debugln('\n');
    debugln("Connection established!");  
    debug("IP address:\t");
    debugln(localIp);         

	// Multiplexer
	debugln("initiating multiplexer to 0 ");   
	selectDevice(0);

	//PZEM
	debugln("setting pzem ip to " + String(ip));   
	pzem.setAddress(ip);
	delay(1000);

	//MQTT
    debugln("setting mqtt server to " + String(mqttServer));   
    mqttCnx.setServer(mqttServer, 1883);                                      //Configuring MQTT server
    mqttCnx.setCallback(mqttCallback);                                        //La fonction de callback qui est executée à chaque réception de message  
    mqttCnx.disconnect();
    mqttConnect();
    mqttSend("IP", "started", localIp);										  // Send the IP address of the ESP8266 to the computer
								
	strip.begin();
	strip.show(); // Initialize all pixels to 'off'                             
	initLed();
	strip.setBrightness(LED_BRIGHTNESS);


}

//------------------------ M Q T T --------------------------------------
void mqttCallback(char* topic, byte* payload, unsigned int length) {

    int i;
    debugln("MQTT Message =>  topic: " + String(topic));
    debug(" | length: " + String(length,DEC));

    for(i=0; i<length; i++) {                                                   // create character buffer with ending null terminator (string)
        mqttBuffer[i] = payload[i];
    }
    mqttBuffer[i] = '\0';

    debugln(" payload: " + String(mqttBuffer));
}

void mqttConnect() {

    while (!mqttCnx.connected()) {
        if (mqttCnx.connect(deviceName, mqttUser, mqttPassword)) {
            debugln("MQTT connexion OK");
        } else {
            Serial.println("MQTT Connexion failed with state : " + mqttCnx.state());
            delay(1000);
        }
    }

}


void mqttSendValue(char* category, int sensor, char unit, float value) {
	char aValue[32];
	sprintf(aValue, "%.2f", value);
	String cCategory = String(category) + String(sensor);
	mqttSend(cCategory.c_str(), String(unit).c_str(), aValue);
}

void mqttSend(const char* category, const char* label, char* value) {

    mqttConnect();
    String topic = String(deviceName) + "/sensor/" + String(category) + "/" + String(label);
    mqttCnx.publish(topic.c_str(), String(value).c_str(), false); 

    debugln("MQTT " + String(topic) + ": " + String(value));
}



//------------------------ P Z E M  --------------------------------------
float getMeasure(char unit) {
	int i = 0;
	float r = -1.0;

	do {
		debugln("pzem reading " + String(unit) + " attempt " + String(i));
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
		i++;
	} while ( i < MAX_ATTEMPTS && r < 0.0);

	return r;
}

float sendMeasures(int sensor, char * units) {
	int   i;
	char  unit;
	float measure;
	float wattReturn;

	debugln("reading pzem #" + String(sensor));

	for (i = 0; i < strlen(units); i++) {
		unit = units[i];
		measure = getMeasure(unit);
		mqttSendValue("energy", sensor, unit, measure);
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
	//delay(50);
}

//------------------------ T E M P E R A T U R E   --------------------------------------

int getAnalogReading(int samples) {
	int analogData = 0;

	for(int i = 0; i < samples; i++) {
		analogData += analogRead(A0);
		delay(10);
	}

	analogData = analogData / samples;
	return analogData;
}

float getTemperature(int analogData) {
  
  int     R0 = 10000;         // Thermistor resistance at room temp  ????????????????????
  int     Rs = 110000;        // Bridge 2nd resistor ????????????????????
  int     T0_deg = 25;        // Room Temp
  int     T0_Coeff = 3950;    // Thermistor coefficient at room temp
  float   K = 273.15;         // Kelvin convertion
  //float   Vcc = 3.3;
  float   Va;                 // Analog Volt reading
  float   Rfactor;            // Steinhart formula Value
  float   RBase;              // Steinhart formula Value
  float   steinhart;          // Steinhart formula Value
  float   T;                  // Temperature
  float   R;                  // Thermistor resistance
  char    unit = 'C';         // MQTT unit

  Va = (float) analogData;
  R = (Va * Rs) / (1023 - Va);

  Rfactor = R/R0;
  RBase =  (1/(K+T0_deg));
  steinhart = 1/(log(Rfactor) / T0_Coeff + RBase); 

  T = steinhart - K;   

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

	// Manual logarithmic style conversion to fit the led color scale 
	// Watts from 0 to 10096 are converted to color : 
	// Green(0) -> Blue(300) -> Cyan(1000) -> yellow -> Red(3500) -> Purple(6000) ->White(>10096)
	if (iWatt > 211) {iWatt = (iWatt + 211) / 2;}   // 300 -> 256
	if (iWatt > 418) {iWatt = (iWatt + 418) / 2;}   // 1000 -> 512
	if (iWatt > 912) {iWatt = (iWatt + 912) / 2;}   // 3500 -> 1024
	if (iWatt > 1224) {iWatt = (iWatt + 1224) / 2;} // 6000 -> 1280
													// 10096 -> 1536

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
		selectDevice(led);
		pzem.setAddress(ip);
		setColor(led, color);
		delay(150);
		strip.show();
		debugln("init led " + String(led));   
	}

	for (bright = 10; bright <= LED_BRIGHTNESS; bright+=10) {
		strip.setBrightness(bright);
		strip.show();
		delay(50);
	}
  
}

//------------------------ L O O P --------------------------------------

void loop() {

	int   	sensor;
	int		analogValue = 0;
	int		analogCount = 0;
	float 	actualWatt;
	float 	boardTemp;
	Rgb   	color;
	char  	Wunits[] = "W"; // VAWE = Volt-Ampere-Watt-Energy
	char  	Vunits[] = "V";


	for (sensor = 0; sensor < PZEM004T_COUNT; sensor++) {
		// Select the sensor
		selectDevice(sensor);

		if (sensor == 0) {
			// Reading pzem Volt and sending mqtt measure
			sendMeasures(sensor, Vunits);
		}

		// Reading pzem watts and sending mqtt measure
		actualWatt = sendMeasures(sensor, Wunits);

		// Setting the Watt corresponding color on the led 
		color = convertToRgb(actualWatt);
		setColor(sensor, color);
		strip.show();

		// Temp reading
		analogValue += getAnalogReading(4);
		analogCount++;
	}
	// Mqtt Send
	mqttCnx.loop();
	mqttCnx.disconnect();

	// Temp compute & send
	analogValue = analogValue / analogCount;
	boardTemp = getTemperature(analogValue);
  	mqttSendValue("temp", 0, 'C', boardTemp);

	// If Wifi lost then reset the ESP
	if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Wifi connexion lost : Rebooting ESP");
		delay(5000);
		ESP.reset();
	}
}
