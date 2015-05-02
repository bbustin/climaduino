// Combined code from:
// http://learn.adafruit.com/tmp36-temperature-sensor/using-a-temp-sensor
// Arduino Projects book p. 119-121, and others
// Lots of Googling :-)
// http://www.engblaze.com/microcontroller-tutorial-avr-and-arduino-timer-interrupts/
// http://www.milesburton.com/?title=Dallas_Temperature_Control_Library
#include <DHT.h>
#include <EEPROM.h>
#include "Thermostat.h" // using " " instead of < > because importing from the sketch folder
#include <avr/wdt.h> //for WatchDog timer
#include <Bridge.h> //for Arduino Yun bridge
#include <Console.h> //for Arduino Yun console (like Serial over WiFi)
#include <Process.h>

// =============================================================== //
// statically defined variables - these can not be changed later   //
// =============================================================== //
const int DEVICEID = 1; //Device identifier so it can be distinguished from other zones

// Arduino pins to trip relays
const int pinCool = 7; //Relay1 on Seeeduino v2.0 shield
const int pinHeat = 6; //Relay2 on Seeeduino v2.0 shield
const int pinFan = 5; //Relay3 on Seeeduino v2.0 shield
const int pinOther = 4; //Relay1 on Seeeduino v2.0 shield - have it here to make sure not to use this pin for anything else as it will keep tripping relay and clicking

// hysteresis settings (the amount above or below a threshold that is allowed)
const int tempHysteresis = 1;
const int humidityHysteresis = 2;
// limits
const int humidityOverCooling = 2; // degrees cooler than setpoint allowed to dehumidify
const unsigned long minRunTimeMillis = 360000; // compressor minimum runtime allowed (prevent short cycles) - unsigned long to match millis datatype
const unsigned long minOffTimeMillis = 180000; //compressor minimum off time before can run again (protect compressor) - unsigned long to match millis datatype
// parameters for averaging readings
const int numberOfReadings = 2; // how many readings to average
const int delayBetweenReadingsMillis = 2000; // how long to wait between readings (DHT22 needs 2 seconds)
// pins to use
const int pinSensor = 9; // pin for temperature and humidity (DHT-22)

// =============================================================== //
// Global variables                                                //
// =============================================================== //
int tempSetPointF; // temperature set point.
int humiditySetPoint; // percent relative humidity
float averageTemp = NAN; // average temperature
float averageHumidity = NAN; // average humidity
boolean currentlyRunning = false; // track whether system is currently running
unsigned long stateChangeMillis; // time in millis when system either turned on or off
int operationMode = 0; // 0 cooling + humidity, 1 humidity control, 5 heating, 9 off
boolean fanMode = false; // 0 fan auto, 1 fan on
String inputString; // input from Serial

// =============================================================== //
// Global objects                                                  //
// =============================================================== //
DHT dht(pinSensor, DHT22); // set up object for DHT22 temperature sensor
Thermostat thermostat(pinCool, pinHeat, pinFan, false); // Climaduino thermostat object

// =============================================================== //
// Helper functions                                                //
// =============================================================== //
// Sets up watchdog timer
void watchdogSetup(void) {
  cli();
  wdt_reset();
  /*
  WDTCSR configuration:
  WDIE = 1: Interrupt Enable 
  WDE = 1 :Reset Enable
  See table for time-out variations: 
  WDP3 = 1 :For 8000ms Time-out 
  WDP2 = 0 :For 8000ms Time-out 
  WDP1 = 0 :For 8000ms Time-out 
  WDP0 = 1 :For 8000ms Time-out
  */
  // Enter Watchdog Configuration mode: WDTCSR |= (1<<WDCE) | (1<<WDE);
  // Set Watchdog settings:
  WDTCSR = (1<<WDIE) | (1<<WDE) |
  (0<<WDP3) | (1<<WDP2) | (1<<WDP1) | (0<<WDP0);
  sei(); }

// Updates current parameters from the Arduino Yun's key/value store
//// If they differ from the current parameters
void updateParametersFromYun() {
  char _charTempSetPointF[3]; // temperature set point in Yun Key/Value store.
  char _charHumiditySetPoint[3]; // percent relative humidity set point in in Yun Key/Value store
  char _charOperationMode[2]; // mode setting in Yun Key/Value store
  char _charFanMode[2]; // fan mode setting in Yun Key/Value store
  Bridge.get("settings/tempSetPoint", _charTempSetPointF, 3);
  Bridge.get("settings/humiditySetPoint", _charHumiditySetPoint, 3);
  Bridge.get("settings/mode", _charOperationMode, 2);
  Bridge.get("settings/fanMode", _charFanMode, 2);
  int _tempSetPointF = atoi(_charTempSetPointF);
  int _humiditySetPoint = atoi(_charHumiditySetPoint);
  int _operationMode = atoi(_charOperationMode);
  bool _fanMode = atoi(_charFanMode) != 0;
  if (operationMode != _operationMode) {
    operationMode = _operationMode;
  }
  if (fanMode != _fanMode) {
    fanMode = _fanMode;
  }
  if (tempSetPointF != _tempSetPointF) {
    tempSetPointF = _tempSetPointF;
  }
  if (humiditySetPoint != _humiditySetPoint) {
    humiditySetPoint = _humiditySetPoint;
  }
}

// Gets average temperature and humidity readings
//// takes numberOfReadings spaced out by delayBetweenReadings
////
//// updates the averageTemp and averageHumidity global variables
float averageReadings(){
  int index; //keeps track of which reading we are on
  float sumOfReadingsTemp = 0; // all temperature readings will be added together here
  float sumOfReadingsHumidity = 0; // all humidity readings will be added together here

  for (index=0; index < numberOfReadings; index++){ //used less than and not less than or equal
    // since index starts at 0, but
    // number of readings starts at 1
    //get temperature reading and humidity reading
    float readingTemp = dht.readTemperature(true); //get temperature reading - true indicates degrees fahrenheight
    float readingHumidity = dht.readHumidity(); //get humidity reading

    // check if either temperature or humidity reading is NAN
    // if that is the case, it indicates a failure reading, so we will break out of the loop
    if (isnan(readingTemp) || isnan(readingHumidity)) {
      sumOfReadingsTemp = NAN; //set to NAN since we do not have a valid value
      sumOfReadingsHumidity = NAN; //set to NAN since we do not have a valid value
      break; //no need to take more measurements as the average will now not be accurate
    }
    sumOfReadingsTemp += readingTemp; //add temperature returned to sumOfReadingsTemp
    sumOfReadingsHumidity += readingHumidity; //add humidity returned to sumOfReadingsHumidity
    delay(delayBetweenReadingsMillis);
  }
  // If either sumOfReadingsTemp or sumOfReadingsHumidity
  // are NAN, set averageTemp and averageHumidity to NAN,
  // otherwise take the average.
  if ( isnan(sumOfReadingsTemp) || isnan(sumOfReadingsHumidity)) {
    averageTemp = NAN;
    averageHumidity = NAN;
  }
  else {
    averageTemp = sumOfReadingsTemp/numberOfReadings;
    averageHumidity = sumOfReadingsHumidity/numberOfReadings;
  }
}

// =============================================================== //
// Setup                                                           //
// =============================================================== //
void setup()
{
  // Bridge startup
  Bridge.begin();
  Console.begin(); // Start the console over WiFi connection
  Serial.begin(9600);  //Start the Serial connection with the computer
  //Taking the values defined above in the sketch and applying them to the Thermostat object
  thermostat.tempHysteresis = tempHysteresis;
  thermostat.humidityHysteresis = humidityHysteresis;
  thermostat.humidityOverCooling = humidityOverCooling;
  thermostat.minRunTimeMillis = minRunTimeMillis;
  thermostat.minOffTimeMillis = minOffTimeMillis;
  dht.begin(); //start up DHT library;
}

// =============================================================== //
// Main program loop                                               //
// =============================================================== //
void loop(){
  wdt_reset(); //reset watchdog timer
  updateParametersFromYun();
  thermostat.mode = operationMode;
  thermostat.tempSetPoint = tempSetPointF;
  thermostat.humiditySetPoint = humiditySetPoint;
  averageReadings(); // get the average readings
  thermostat.FanControl(fanMode); // turn fan on or off as appropriate
  thermostat.Control(averageTemp, averageHumidity);//run thermostat logic
  wdt_reset(); //reset watchdog timer
  // put data in the Arduino Yun key/value store
  Bridge.put("readings/temperature", String(averageTemp));
  Bridge.put("readings/humidity", String(averageHumidity));
  Bridge.put("status/currentlyRunning", String(thermostat.CurrentlyRunning()));
  Bridge.put("status/stateChangeAllowed", String(thermostat.StateChangeAllowed()));
  Process p;
  p.runShellCommand("python /root/send_readings.py");
  wdt_reset(); //reset watchdog timer
}

ISR(WDT_vect)
{ // what happens when the watchdog interrupt is triggered
  
}

// Unused legacy code that may be useful at a later point
// Reads back values for our settings from EEPROM and
//// populates the proper global variables. If The EEPROM locations have
//// not yet been set, then returns default values.
////
//// memory locations:
////// 0 - operationMode (default 0)
////// 1 - tempSetPointF (default 78)
////// 2 - humiditySetPoint (default 55)
//void readEEPROMValues() {
//  // get operationMode from 0, tempSetPointF from 1, and humiditySetPoint from 2
//  operationMode = EEPROM.read(0);
//  tempSetPointF = EEPROM.read(1);
//  humiditySetPoint = EEPROM.read(2);
//  
//  // NOTE: ON THE ARDUINO YUN, EEPROM VALUES ARE CLEARED WHEN A SKETCH IS UPLOADED
//  
//  // if any values are 255, it means the address space in EEPROM
//  //// has not been written yet. In that case, let's set some sane
//  //// defaults which will eventually be written to EEPROM by another function.
//  if (operationMode == 255) {
//    operationMode = 9;
//  }
//  if (tempSetPointF == 255) {
//    tempSetPointF = 77;
//  }
//  if (humiditySetPoint == 255) {
//    humiditySetPoint = 55;
//  }
//}

// Examines current global variables and if different from the values
//// in EEPROM, will update the appropriate memory address in EEPROM
////
//// memory locations:
////// 0 - operationMode
////// 1 - tempSetPointF
////// 2 - humiditySetPoint
//void updateEEPROMValues() {
//  if (operationMode != EEPROM.read(0)) {
//    EEPROM.write(0, operationMode);
//  }
//  if (tempSetPointF != EEPROM.read(1)) {
//    EEPROM.write(1, tempSetPointF);
//  }
//  if (humiditySetPoint != EEPROM.read(2)) {
//    EEPROM.write(2, humiditySetPoint);
//  }
//}
//// Updates current parameters to the Arduino Yun's key/value store
////// If it differs from the current local parameters
//void updateParametersToYun() {
//  char _charTempSetPointF[3]; // temperature set point in Yun Key/Value store.
//  char _charHumiditySetPoint[3]; // percent relative humidity set point in Yun Key/Value store
//  char _charOperationMode[2]; // mode setting in Yun Key/Value store
//  char _charFanMode[2]; // fan mode setting in Yun Key/Value store
//  Bridge.get("settings/tempSetPoint", _charTempSetPointF, 3);
//  Bridge.get("settings/humiditySetPoint", _charHumiditySetPoint, 3);
//  Bridge.get("settings/mode", _charOperationMode, 2);
//  Bridge.get("settings/fanMode", _charFanMode, 2);
//  int _tempSetPointF = atoi(_charTempSetPointF);
//  int _humiditySetPoint = atoi(_charHumiditySetPoint);
//  int _operationMode = atoi(_charOperationMode);
//  bool _fanMode = atoi(_charFanMode) !=0;
//  if (operationMode != _operationMode) {
//    Bridge.put("mode", String(operationMode));
//  }
//  if (fanMode != _fanMode) {
//    Bridge.put("fanMode", String(fanMode));
//  }
//  if (tempSetPointF != _tempSetPointF) {
//    Bridge.put("tempSetPoint",  String(tempSetPointF));
//  }
//  if (humiditySetPoint != _humiditySetPoint) {
//    Bridge.put("humiditySetPoint", String(humiditySetPoint));
//  }
//}

///*
//  SerialEvent occurs whenever a new data comes in the
// hardware serial RX.  This routine is run between each
// time loop() runs, so using delay inside loop can delay
// response.  Multiple bytes of data may be available.
// */
// // Adapted from SerialEvent example sketch  Created 9 May 2011 by Tom Igoe
//void serialEvent() {
//  // THIS REALLY SHOULD SANITIZE INPUT!!
//  while (Serial.available()) {
//    // get the new byte:
//    char inChar = (char)Serial.read(); 
//    // add it to the inputString:
//    inputString += inChar;
//    // if the incoming character is a newline, set a flag
//    // so the main loop can do something about it:
//    if (inChar == 'F') { // input to change temperature
//      if (inputString.length() <= 3){ // 2 digits and an F
//        // remove last character and turn into a float
//        inputString = inputString.substring(0, 2);
//        char inputCharArray[3]; //only allow values with 2 digits (and 1 null for atoi)
//        inputString.toCharArray(inputCharArray, 3);
//        tempSetPointF = (float)atoi(inputCharArray);
//      }
//      inputString = ""; // clear inputString
//    }
//    if (inChar == '%') { // input to change humidity set point
//      if (inputString.length() <= 3){ // 2 digits and a %
//        // remove last character and turn into an integer
//        inputString = inputString.substring(0, inputString.length() - 1);
//        //char inputCharArray[inputString.length() + 1];
//        char inputCharArray[3]; //only allow values with 2 digits (and 1 null for atoi)
//        inputString.toCharArray(inputCharArray, 3);
//        humiditySetPoint = atoi(inputCharArray);
//      }
//      inputString = ""; // clear inputString
//    }
//    if (inChar == 'M') { // input to change operation mode
//      if (inputString.length() <= 2){ // 1 digits and an M
//        // remove last character and turn into an integer
//        inputString = inputString.substring(0, inputString.length() - 1);
//        //char inputCharArray[inputString.length() + 1];
//        char inputCharArray[2]; //only allow values with 1 digit (and 1 null for atoi)
//        inputString.toCharArray(inputCharArray, 2);
//        operationMode = atoi(inputCharArray);
//       }
//      inputString = ""; // clear inputString
//    }
//  }
//}
