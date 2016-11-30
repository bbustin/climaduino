// Combined code from:
// http://learn.adafruit.com/tmp36-temperature-sensor/using-a-temp-sensor
// Arduino Projects book p. 119-121, and others
// Lots of Googling :-)
// http://www.engblaze.com/microcontroller-tutorial-avr-and-arduino-timer-interrupts/
// SousViduino code by Bill Earl - for Adafruit Industries
// http://www.milesburton.com/?title=Dallas_Temperature_Control_Library
#include <DHT.h>
#include <EEPROM.h>
#include <avr/wdt.h> //for WatchDog timer
#include <Bridge.h> //for Arduino Yun bridge
#include <Console.h> //for Arduino Yun console (like Serial over WiFi)
#include <Process.h>
#include <PID_v1.h>

// =============================================================== //
// statically defined variables - these can not be changed later   //
// =============================================================== //
const int DEVICEID = 1; //Device identifier so it can be distinguished from other zones

// Arduino pins to trip relays
const int pinHeat = 6; //Relay2 on Seeeduino v2.0 shield

// parameters for averaging readings
const int numberOfReadings = 2; // how many readings to average
const int delayBetweenReadingsMillis = 2000; // how long to wait between readings (DHT22 needs 2 seconds)

// pins to use
const int pinSensor = 9; // pin for temperature and humidity (DHT-22)

// time Proportional Output window
unsigned long windowSize = 60000;
unsigned long windowStartTime;

// =============================================================== //
// Global variables                                                //
// =============================================================== //
double tempSetPointF = NAN; // temperature set point.
double averageTemp = NAN; // average temperature
double averageHumidity = NAN; // average humidity
volatile double heatOutput = 0; // duty cycle for heater
boolean currentlyRunning = false; // track whether system is currently running
int operationMode = 9; // 5 heating, 9 off
String inputString; // input from Serial

// =============================================================== //
// Global objects                                                  //
// =============================================================== //
DHT dht(pinSensor, DHT22); // set up object for DHT22 temperature sensor
PID myPID(&averageTemp, &heatOutput, &tempSetPointF, 0, 0, 0, DIRECT);

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
  char _charOperationMode[2]; // mode setting in Yun Key/Value store
  Bridge.get("settings/tempSetPoint", _charTempSetPointF, 3);
  Bridge.get("settings/mode", _charOperationMode, 2);
  double _tempSetPointF = atof(_charTempSetPointF);
  int _operationMode = atoi(_charOperationMode);
  if (operationMode != _operationMode) {
    operationMode = _operationMode;
  }
  if (tempSetPointF != _tempSetPointF) {
    tempSetPointF = _tempSetPointF;
  }
}

// Gets average temperature and humidity readings
//// takes numberOfReadings spaced out by delayBetweenReadings
////
//// updates the averageTemp and averageHumidity global variables
float averageReadings(){
  int index; //keeps track of which reading we are on
  double sumOfReadingsTemp = 0; // all temperature readings will be added together here
  double sumOfReadingsHumidity = 0; // all humidity readings will be added together here

  for (index=0; index < numberOfReadings; index++){ //used less than and not less than or equal
    // since index starts at 0, but
    // number of readings starts at 1
    //get temperature reading and humidity reading
    double readingTemp = dht.readTemperature(true); //get temperature reading - true indicates degrees fahrenheight
    double readingHumidity = dht.readHumidity(); //get humidity reading

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

  pinMode(pinHeat, OUTPUT);

  dht.begin(); //start up DHT library;

  myPID.SetSampleTime(2000);
  myPID.SetOutputLimits(0, windowSize);
  myPID.SetMode(AUTOMATIC);
}

void driveOutput()
{
  long now = millis();
  Console.print("heatOutput: ");
  Console.println(heatOutput);
  // Set the output
  // "on time" is proportional to the PID output
  if(now - windowStartTime>windowSize)
  { //time to shift the Relay Window
     windowStartTime += windowSize;
  }
  if((heatOutput > 2000) && (heatOutput > (now - windowStartTime)))
  {
     digitalWrite(pinHeat,HIGH);
     currentlyRunning = true;
  }
  else
  {
     digitalWrite(pinHeat,LOW);
     currentlyRunning = false;
  }
}

// =============================================================== //
// Main program loop                                               //
// =============================================================== //
void loop(){
  updateParametersFromYun();
  averageReadings(); // get the average readings
  if (operationMode == 9)
  {
    digitalWrite(pinHeat, LOW);  // make sure relay is off
    currentlyRunning = false;
  }
  else
  {
    if (((tempSetPointF - averageTemp) >= -0.5) || ((tempSetPointF - averageTemp) <= 1)) {
      Console.println("less aggressive tuning");
      myPID.SetTunings(1000,10,0.1);
    } else {
      Console.println("aggressive tuning");
      myPID.SetTunings(10000,100,0.1);
    }
    myPID.Compute();
    driveOutput();
  }
  
  // put data in the Arduino Yun key/value store
  Bridge.put("readings/temperature", String(averageTemp));
  Bridge.put("readings/humidity", String(averageHumidity));
  Bridge.put("status/currentlyRunning", String(currentlyRunning));
  Process p;
  p.runShellCommand("python /root/send_readings.py");
}
