Climaduino Thermostat
=====================

The Climaduino is a DIY Arduino Yún-based thermostat. It uses the DHT22 temperature and humidity sensor in order to optimize comfort and reduce energy usage. Using a relay shield, the Climaduino can interface with a central AC. It can also be used with wall AC units just like the previous version (see link in more information section below).

The Climaduino Controller component (which can run on a Raspberry Pi, interfaces with the thermostat using the MQTT protocol. Since this project using the MQTT protocol, other software besides the Climaduino Controller could also be used to control it such as OpenHAB.

Living in South Florida, humidity is a huge issue and the Climaduino was designed to take this into account. In Cooling mode the area is allowed to be overcooled by a set amount of degrees if it is too humid. The default is 2 degrees. In Humidity Control mode, temperature is not taken into account and the unit runs whenever humidity is too high. This mode is ideal for when no one is home (and there are no pets in the house) or when on vacation. It can save a significant amount of power and prevent coming home to a mold-infested house.

To protect the compressor and be more energy efficient, there is a minimum off time and minimum run time to prevent short cycling. The minimum run time can sometimes lead to a slight bit of overcooling or can lead to humidity going a little below the setpoint, but this appears to be pretty minimal.

Setup
=====
Load the climaduino sketch onto the Arduino Yún, then please refer to the README file in the mqtt_yun_bridge folder.


More information
----------------

There is an Instructable about the previous version of the Climaduino here: http://www.instructables.com/id/Introducing-Climaduino-The-Arduino-Based-Thermosta/
