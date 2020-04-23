# esp8266pzem
Up to 8 pzem004T connected to a ESP8266

Complete projet (code & schema) to connect multiple PZEM004T to ESP8266.

It use 2 multiplexer/demultipler (74HC4051) for TX and RX to access up to 8 devices
and collect volt,ampere & power.
Data are sent via mqtt to my home server (jeedom) every few seconds.

8 RGB led (WS2812) are used to indicate actual power on each line
(colors varying from blue->cyan->yellow->red)

I added a cheap temp monitoring because the board and the 8 pzem are in a box in a closed closet

5V come from a hi-Link 5V/3W but you can use an external adapter

I used an old RJ45 to extend the Coil
all 8 coil are placed directly in the switchboard and i'm monitoring 8 appliances (oven, boiler, fridge, washer, dryer, ...)

project include
* the sketch for nodeMcu
* eagle circuit schema
* some picture of the prototype.