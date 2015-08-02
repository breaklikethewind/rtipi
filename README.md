
sump pump monitoring software written by Eric Nelson

This program uses a raspberry pi to take temperature, humidity, and distance
measurements of a sump pit. That data is sent to an RTI XP processor for 
display to the user.

The files are:

beep.c
This is a driver to activate a piezo electric buzzer on the raspberry pi.
This driver can also send morse code using the pi buzzer.

dht_read.c
This is a driver to read a AM2302, or DHT22, temperature/humidity
sensor.

range.c
This is a driver to read an HC-SR04 ultrasonic range module.

sump.c
This is the main program entry point.

transport.c
This controls the communication to the RTI processor. The communciation
uses the RTI driver "two way strings".


Each driver, and transport.c are designed to be self contained re-usable
modules for other programs. 

The drivers init functions identify the pins the sensor is connected to. These
drivers require the wiring pi library be installed.

The transport.c module contains the mechanism to manage the communication
to the RTI processor. Programs can re-use the transport module by defining a
command table (commandlist_t), and a push table (pushlist_t). The command
table defines what variables, or functions, are called when an XP processor request
arrives. The push table defines what periodic data is sent to the processor.

On the RTI processor two way strings driver, you must define the tag strings from
the push list, and the command strings from the tags in the command list. The 
full command list consists of tags defined in your application (sump.c in this case),
and tags defined in transport.c.


