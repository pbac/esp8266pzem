
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <SoftwareSerial.h>
#include <PZEM004T.h>			// https://github.com/olehs/PZEM004T Power Meter
#include <Adafruit_NeoPixel.h>
#include <math.h>

#include "main.h"
#include "secret.h"
#include "mqtt54.h"
 
#define		DEVICE_TYPE			"node"
#define		DEVICE_ID			"131"
#define		VERSION				"2.2.5"

#define		LED_BUILTIN			16
#define		ADDRESS_PIN1		12
#define		ADDRESS_PIN2		13
#define		ADDRESS_PIN3		15
#define		LED_PIN				14	//WS2812 led
#define		PZEM004T_RX_PIN		4
#define		PZEM004T_TX_PIN		5

#define		TRIGGER_PIN			0
#define		MAX_ATTEMPTS		3
#define		PZEM004T_COUNT		8	//Total PZEM004 & Leds
#define		LED_BRIGHTNESS		100
#define		WATT_AVG			10
#define		VOLT_AVG			20
#define		TEMP_AVG			30
#define		WATT_PRECISION		0
#define		VOLT_PRECISION		1
#define		TEMP_PRECISION		1


//-------------------------------------------------------------------

struct Rgb {
  byte r;
  byte g;
  byte b;
};

struct MeasureCache {
  byte		count;
  float	measure;
  float	sum;
};

MeasureCache	watt[PZEM004T_COUNT];
MeasureCache	volt;
MeasureCache	temp;

//-------------------------------------------------------------------

PZEM004T 		pzem(PZEM004T_RX_PIN,PZEM004T_TX_PIN);  // RX,TX (D2, D1) on NodeMCU
IPAddress 		ip(192,168,1,1); // required by pzem but not used

Adafruit_NeoPixel strip = Adafruit_NeoPixel(PZEM004T_COUNT, LED_PIN, NEO_RGB + NEO_KHZ800);

WiFiClient		wifiCnx;
Mqtt54			mqttCnx(wifiCnx, SECRET_MQTT_SERVERNAME, SECRET_MQTT_PORT, SECRET_MQTT_USER, SECRET_MQTT_PASSWORD);

//_______________________________________________________________________________________________________
//_______________________________________________________________________________________________________

void flashLed () {
	static bool	builtIn_led = 0;		// For flashing the ESP led

	builtIn_led = !builtIn_led;
	digitalWrite(LED_BUILTIN, builtIn_led);
}
void flashLed (bool	builtIn_led) {
	digitalWrite(LED_BUILTIN, builtIn_led);
}

//___________________________________  W I F I   C O N N E C T I O N  ___________________________________
//_______________________________________________________________________________________________________

int wifiConnection(const char * Ssid, const char * Password, const char * Hostname) {
	int		cnxWait = 0;

	flashLed(1);
	WiFi.mode(WIFI_STA);											// Set atation mode only (not AP)
	delay(150);
	//WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);			// Reset all address
	delay(150);
	WiFi.hostname(Hostname);										// Set the hostname (for dhcp server)
	delay(150);
	WiFi.begin(Ssid, Password);										// Connect to the network
	debug(String() + Hostname + " connecting to " + Ssid + "... "); 

	while (WiFi.status() != WL_CONNECTED) {						// Wait (4min max) for the Wi-Fi to connect
		delay(500);
		debug(String(++cnxWait) + "."); 
		if (cnxWait > 500) {ESP.restart();}							// Reboot if no wifi connection 
	}
	flashLed(0);
	debugln();
	debugln(String() + "IP address  :\t" + WiFi.localIP()[0] + "." + WiFi.localIP()[1] + "." + WiFi.localIP()[2] + "." + WiFi.localIP()[3]);
	return cnxWait;
}

//------------------------ T E M P E R A T U R E   --------------------------------------

int getAnalogReading(int samples) {
	int 	analogData = 0;

	for(int i = 0; i < samples; i++) {
		analogData += analogRead(A0);
		delay(10);
	}

	analogData = analogData / samples;
	return analogData;
}

float getTemperature(int analogData) {
  
	int		R0 = 10000;			// Thermistor resistance at room temp  ????????????????????
	int		Rs = 110000;		// Bridge 2nd resistor ????????????????????
	int		T0_deg = 25;		// Room Temp
	int		T0_Coeff = 3950;	// Thermistor coefficient at room temp
	float	K = 273.15;			// Kelvin convertion
	//float   Vcc = 3.3;
	float	Va;					// Analog Volt reading
	float	Rfactor;			// Steinhart formula Value
	float	RBase;				// Steinhart formula Value
	float	steinhart;			// Steinhart formula Value
	float	T;					// Temperature
	float	R;					// Thermistor resistance

	Va = (float) analogData;
	R = (Va * Rs) / (1023 - Va);
	Rfactor = R/R0;
	RBase =  (1/(K+T0_deg));
	steinhart = 1/(log(Rfactor) / T0_Coeff + RBase); 
	T = steinhart - K;   

	return T; 
}

//------------------------ P Z E M  --------------------------------------
float getMeasure(char unit) {
	byte 	i = 0;
	float 	r = -1.0;

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
	delay(10);
}

void readSensor() {
	byte	sensor;
	float	measure;

	for (sensor = 0; sensor < PZEM004T_COUNT; sensor++) {
		flashLed();
		selectDevice(sensor);						// Select the sensor

		if (sensor == 0) {
			measure = getMeasure('V');			// Reading pzem Volt 
			if (measure >= 0) {
				volt.count++;
				volt.measure = measure;
				volt.sum += measure;
			}
		}
		measure = getMeasure('W');				// Reading pzem Watt
		if (measure >= 0) {
			watt[sensor].count++;
			watt[sensor].measure = measure;
			watt[sensor].sum += measure;
		}
	}
	
	measure = getTemperature(getAnalogReading(4));
	temp.count++;
	temp.measure = measure;
	temp.sum += measure;
}

void sendSensor() {
	byte	sensor;
	float	measure;

	if (volt.count >= VOLT_AVG) {
		measure = volt.sum / volt.count;
		measure = round( measure * pow(10, VOLT_PRECISION) ) / pow(10, VOLT_PRECISION);
		mqttCnx.sendSensor("sonde", 0, "V", measure);
		volt.count	= 0;
		volt.sum	= 0;
	}

	if (temp.count >= TEMP_AVG) {
		measure = temp.sum / temp.count;
		measure = round( measure * pow(10, TEMP_PRECISION) ) / pow(10, VOLT_PRECISION);
		mqttCnx.sendSensor("temp", 0, "C", measure);
		temp.count	= 0;
		temp.sum	= 0;
	}

	for (sensor = 0; sensor < PZEM004T_COUNT; sensor++) {
		if (watt[sensor].count >= WATT_AVG) {
			measure = watt[sensor].sum / watt[sensor].count;
			measure = round( measure * pow(10, WATT_PRECISION) ) / pow(10, WATT_PRECISION);
			mqttCnx.sendSensor("sonde", sensor, "W", measure);
			watt[sensor].count	= 0;
			watt[sensor].sum	= 0;
		}

	}
}


//------------------------ N E O   P I X E L  --------------------------------------
Rgb convertToRgb(float watt) {

	Rgb		color;
	int		red;
	int		green;
	int		blue;
	int 	iWatt;

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

	if (iWatt < 0) {	// PZEM return (-1) when read error.
		red   = 30;
		green = 30;
		blue  = 30;
	}
	if (iWatt == 0) {
		red   = 0;
		green = 30;
		blue  = 0;
	} 
	if (iWatt > 0) {	// Dark Blue -> Blue
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
	Rgb		color;
	byte	bright;
	byte	led;

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

void updateLed() {
	byte	sensor;
	Rgb		color;

	// Setting the Watt corresponding color on the led 
	for (sensor = 0; sensor < PZEM004T_COUNT; sensor++) {
		color = convertToRgb(watt[sensor].measure);
		setColor(sensor, color);
	}
		strip.show();
}

//------------------------ S E T U P --------------------------------------
void setup() {

	// SERIAL
	Serial.begin(115200);
	debugln("Starting " DEVICE_TYPE " " DEVICE_ID " v" VERSION);

	//PIN
	pinMode(LED_BUILTIN, OUTPUT);										// set led pin as output
	pinMode(ADDRESS_PIN1, OUTPUT);
	pinMode(ADDRESS_PIN2, OUTPUT);
	pinMode(ADDRESS_PIN3, OUTPUT);
	digitalWrite(LED_BUILTIN, LOW);										// keep LED off

	// WIFI Connection
	wifiConnection(SECRET_WIFI_SSID, SECRET_WIFI_PASSWORD, DEVICE_TYPE DEVICE_ID);
	mqttCnx.setDevice(DEVICE_TYPE, DEVICE_ID);
	mqttCnx.setTime(SECRET_NTP_SERVERNAME, SECRET_NTP_TIMEZONE);
	mqttCnx.setCacheExpire(120);
	mqttCnx.start(WiFi.localIP(), WiFi.macAddress());

	mqttCnx.send("device", "version",  "node", VERSION);	

	// Multiplexer
	debugln("initiating multiplexer to 0 ");   
	selectDevice(0);

	//PZEM
	pzem.setAddress(ip);
	delay(1000);

	strip.begin();
	strip.show(); // Initialize all pixels to 'off'							
	initLed();
	strip.setBrightness(LED_BRIGHTNESS);

}

//------------------------ L O O P --------------------------------------

void loop() {

	mqttCnx.loop();

	// Reading pzem watts and sending mqtt measure
	readSensor();
	sendSensor();
	updateLed();

	// If Wifi lost then reset the ESP
	if (WiFi.status() != WL_CONNECTED) {
		Serial.println("Wifi connexion lost : Rebooting ESP");
		delay(5000);
		ESP.restart();
	}
	delay(150);
}
