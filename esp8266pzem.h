#define VERSION "1.2.4"

#define DEBUG 

#ifdef DEBUG
#define isDebug     1
#define debug(x)    Serial.print(x)
#define debugln(x)  Serial.println(x)
#else
#define isDebug     0
#define debug(x)    {}
#define debugln(x)  {}
#endif


#ifndef esp8266pzem_h
#define esp8266pzem_h
#endif


#define LED_BUILTIN       16
#define ADDRESS_PIN1      12
#define ADDRESS_PIN2      13
#define ADDRESS_PIN3      15
#define LED_PIN           14      //WS2812 led
#define PZEM004T_RX_PIN   4
#define PZEM004T_TX_PIN   5

#define TRIGGER_PIN       0
#define MAX_ATTEMPTS      3
#define PZEM004T_COUNT    8       //Total PZEM004 & Leds
#define LED_BRIGHTNESS    100    


struct rgb {
  byte r;
  byte g;
  byte b;
};

typedef struct rgb Rgb;

/*
#define PIN_WIRE_SDA (4)
#define PIN_WIRE_SCL (5)

static const uint8_t SDA = PIN_WIRE_SDA;
static const uint8_t SCL = PIN_WIRE_SCL;

static const uint8_t LED_BUILTIN = 16;
static const uint8_t BUILTIN_LED = 16;

static const uint8_t D0   = 16;
static const uint8_t D1   = 5;
static const uint8_t D2   = 4;
static const uint8_t D3   = 0;
static const uint8_t D4   = 2;
static const uint8_t D5   = 14;
static const uint8_t D6   = 12;
static const uint8_t D7   = 13;
static const uint8_t D8   = 15;
static const uint8_t D9   = 3;
static const uint8_t D10  = 1;
*/