# esp8266pzem
Up to 8 pzem004T connected to a ESP8266

Complete projet (code & schema) to connect multiple PZEM004T to ESP8266.

It use 2 multiplexer/demultipler (74HC4051) for TX and RX to access up to 8 devices
and collect volt,ampere & power.
Data are sent via mqtt to my home server (jeedom) every few seconds.

8 RGB led (WS2812) are used to indicate actual power on each line
(colors varying from blue->cyan->yellow->red)
