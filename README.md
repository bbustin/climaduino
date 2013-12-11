Climaduino Thermostat
=====================

The Climaduino is a DIY Arduino-based thermostat designed to interface with a wall unit A/C. It uses the DHT22 temperature and humidity sensor in order to optimize comfort and reduce energy usage. There is an optional component, the Raspberry Pi-based Climaduino Controller, that interfaces with the thermostat to provide additional functionality.

Living in South Florida, humidity is a huge issue and the Climaduino was designed to take this into account. In Cooling / Humidity Control mode, the area is allowed to be overcooled by a set amount of degrees if it is too humid. The default is 5 degrees. In Humidity Control mode, temperature is not taken into account and the unit runs whenever humidity is too high. This mode is ideal for when no one is home (and there are no pets in the house) or when on vacation. It can save a significant amount of power and prevent coming home to a mold-infested house.

To protect the compressor and be more energy efficient, there is a minimum off time and minimum run time to prevent short cycling. The minimum run time can sometimes lead to a slight bit of overcooling or can lead to humidity going a little below the setpoint, but this appears to be pretty minimal.

There is an LCD display with the current temperature and humidity setpoints. Two buttons allow the temperature to be raised or lowered. There is currently no way to directly change the humidity setpoint or the mode without the Controller component. Tweaking the code and uploading it to the Arduino is one way to change the mode. Another way is to use the Arduino IDE to send commands to the thermostat. The next paragraph provides some more detail.

The thermostat outputs current readings, parameters, and other operational details as JSON over the serial port. It also accepts commands to be sent to it over serial to change certain parameters. This is what allows the Thermostat to be used as part of a larger climate control system. Currently the temperature setpoint (send 77F to change temperature to 77), humidity setpoint (send 55% to change humidity to 55%), and mode can be changed (send 1M to change to Humidity Control Mode). These commands should be sent with no linefeed.

The temperature setpoint, humidity setpoint, and mode are stored in EEPROM. The parameters will persist even if the power goes out, the Arduino reset button is pressed, or the Arduino is reprogrammed.

More information
----------------

See the instructable with all the information here: http://www.instructables.com/id/Introducing-Climaduino-The-Arduino-Based-Thermosta/
