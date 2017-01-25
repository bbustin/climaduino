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
const unsigned long windowSize = 60000; //do not set over 60,000 because we set PID window size integer to half of this and integer is up to 32768
const double minimumRuntime = 20000; //minimum runtime allowed

// =============================================================== //
// Global variables                                                //
// =============================================================== //
double tempSetPointF = 0; // temperature set point.
double averageTemp = NAN; // average temperature
double averageHumidity = NAN; // average humidity
double heatOutput = 0; // duty cycle for heater

boolean currentlyRunning = false; // track whether system is currently running
int operationMode = 9; // 5 heating, 9 off
String inputString; // input from Serial
unsigned long windowStartTime; //for PID output

// used so that we do not need a delay in the averageReadings function
unsigned long lastReadingTime;
int readingNumber;
double sumOfReadingsTemp = 0; // all temperature readings will be added together here
double sumOfReadingsHumidity = 0; // all humidity readings will be added together here

// =============================================================== //
// Global objects                                                  //
// =============================================================== //
DHT dht(pinSensor, DHT22); // set up object for DHT22 temperature sensor
PID myPID(&averageTemp, &heatOutput, &tempSetPointF, 0, 0, 0, DIRECT);

// Updates current parameters from the Arduino Yun's key/value store
//// If they differ from the current parameters
void updateParametersFromYun() {
  char _charTempSetPointF[3]; // temperature set point in Yun Key/Value store.
  char _charOperationMode[2]; // mode setting in Yun Key/Value store
  Bridge.get("settings/tempSetPoint", _charTempSetPointF, 4);
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
  long now = millis();
  
  if (now - lastReadingTime>=delayBetweenReadingsMillis) {
    lastReadingTime = now;

    readingNumber++;
    if (readingNumber > numberOfReadings) { //reset readingNumber counter if needed
      readingNumber = 1;
    }

    double readingTemp = dht.readTemperature(true); //get temperature reading - true indicates degrees fahrenheight
    double readingHumidity = dht.readHumidity(); //get humidity reading

    if (isnan(readingTemp) || isnan(readingHumidity)) {
      sumOfReadingsTemp = NAN; //set to NAN since we do not have a valid value
      sumOfReadingsHumidity = NAN; //set to NAN since we do not have a valid value
      //no need to take more measurements as the average will now not be accurate, jump to last readingNumber so we update the variable used by PID
      readingNumber = numberOfReadings;
    } else {
      sumOfReadingsTemp += readingTemp; //add temperature returned to sumOfReadingsTemp
      sumOfReadingsHumidity += readingHumidity; //add humidity returned to sumOfReadingsHumidity
    }

    // if we are on the last reading, calculate the averages and update the global variables
    if (readingNumber == numberOfReadings) {
      if ( isnan(sumOfReadingsTemp) || isnan(sumOfReadingsHumidity)) {
        averageTemp = NAN;
        averageHumidity = NAN;
      }
      else {
        averageTemp = sumOfReadingsTemp/numberOfReadings;
        averageHumidity = sumOfReadingsHumidity/numberOfReadings;
        // reset sum variables
        sumOfReadingsTemp = 0;
        sumOfReadingsHumidity = 0;
      }
    }
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

  //Only need to sample once per window, but we are sampling twice
  //Why twice? Because WindowSize is larger than int can hold on Arduino
  myPID.SetSampleTime(windowSize/2);
  myPID.SetOutputLimits(minimumRuntime-1, windowSize);
  myPID.SetMode(AUTOMATIC);
}

void driveOutput()
{
  long now = millis();
  
  if(now - windowStartTime>windowSize)
  { //time to shift the Relay Window
     windowStartTime += windowSize;
  }
  //never burn for less than 20 seconds... trying to limit amount of inefficiency
  if((heatOutput > minimumRuntime) && (heatOutput > (now - windowStartTime)))
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
  
  if (operationMode == 5) //heating mode
  {
    if (!isnan(averageTemp) && !isnan(tempSetPointF)) {
      //run PID control with different tunings depending on if close or far from the setpoint
      // SetTunings(P, I, D)
      // P = proportional: how it reacts to the present error
      // I = integral: how it uses past error values to "smooth out" the response
      // D = derivative: "dampens" the response from large changes of between previous and present error
      // Helpful article: http://blog.nikmartin.com/2012/11/process-control-for-dummies-like-me.html
      if (((tempSetPointF - averageTemp) >= -0.5) && ((tempSetPointF - averageTemp) <= 0.5)) {
        myPID.SetTunings(50000,25,0.1);
      } else {
        myPID.SetTunings(100000,100,1);
      }
      myPID.Compute();
    }
    driveOutput();
  }
  else
  {
    digitalWrite(pinHeat, LOW);  // make sure relay is off
    heatOutput = 0;
    currentlyRunning = false;
  }
  
  // put data in the Arduino Yun key/value store
  Bridge.put("readings/temperature", String(averageTemp));
  Bridge.put("readings/humidity", String(averageHumidity));
  Bridge.put("status/currentlyRunning", String(currentlyRunning));

  double outputPercent = 0;
  if (heatOutput > minimumRuntime) {
    float pct = map(heatOutput, 0, windowSize, 0, 1000);
    outputPercent = pct/10;
  }
  
  Bridge.put("status/outputPercent", String(outputPercent));
  
  Process p;
  p.runShellCommand("python /root/send_readings.py");
  delay(100);
}
