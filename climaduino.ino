// Combined code from:
// http://learn.adafruit.com/tmp36-temperature-sensor/using-a-temp-sensor
// Arduino Projects book p. 119-121, and others
// Lots of Googling :-)
// http://www.engblaze.com/microcontroller-tutorial-avr-and-arduino-timer-interrupts/
// http://www.milesburton.com/?title=Dallas_Temperature_Control_Library
#include <DHT.h>
#include <EEPROM.h>
#include <SPI.h>
#include <Ethernet.h>
#include "Thermostat.h" //using " " instead of < > because in the same folder as sketch
#include <aJSON.h>
#include <MemoryFree.h>
#include <avr/wdt.h> //for WatchDog timer

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
const int tempHysteresis = 2;
const int humidityHysteresis = 2;
// limits
const int humidityOverCooling = 5; // degrees cooler than setpoint allowed to dehumidify
const unsigned long minRunTimeMillis = 600000; // cooling minimum runtime allowed (prevent short cycles) - unsigned long to match millis datatype
const unsigned long minOffTimeMillis = 180000; //cooling minimum off time before can run again (protect compressor) - unsigned long to match millis datatype
// parameters for averaging readings
const int numberOfReadings = 2; // how many readings to average
const int delayBetweenReadingsMillis = 2000; // how long to wait between readings (DHT22 needs 2 seconds)
// pins to use
const int pinSensor = 9; // pin for temperature and humidity (DHT-22)

byte mac[] = { 0x48, 0xC2, 0xA1, 0xF3, 0x8D, 0xB7 }; // if you have multiple climaduino zones, they must have unique MAC addresses
IPAddress ip(192, 168, 1, 150);
IPAddress controller_ip(192, 168, 2, 5); // ip address of the web controller, note: you may want to statically assign the Controller's address.
const int controller_port = 80;

// =============================================================== //
// Global variables                                                //
// =============================================================== //
int tempSetPointF; // temperature set point.
//// Interrupt Service Routine (ISR)
int humiditySetPoint; // percent relative humidity
float averageTemp = NAN; // average temperature
float averageHumidity = NAN; // average humidity
boolean currentlyRunning = false; // track whether system is currently running
unsigned long stateChangeMillis; // time in millis when system either turned on or off
int operationMode = 0; // 0 cooling + humidity, 1 humidity control, 5 heating, 9 off
String inputString; // input from Serial

// =============================================================== //
// Global objects                                                  //
// =============================================================== //
DHT dht(pinSensor, DHT22); // set up object for DHT22 temperature sensor
EthernetClient client; // set up object for ethernet client
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

// Reads back values for our settings from EEPROM and
//// populates the proper global variables. If The EEPROM locations have
//// not yet been set, then returns default values.
////
//// memory locations:
////// 0 - operationMode (default 0)
////// 1 - tempSetPointF (default 78)
////// 2 - humiditySetPoint (default 55)
void readEEPROMValues() {
  // get operationMode from 0, tempSetPointF from 1, and humiditySetPoint from 2
  operationMode = EEPROM.read(0);
  tempSetPointF = EEPROM.read(1);
  humiditySetPoint = EEPROM.read(2);
  
  // if any values are 255, it means the address space in EEPROM
  //// has not been written yet. In that case, let's set some sane
  //// defaults which will eventually be written to EEPROM by another function.
  if (operationMode == 255) {
    operationMode = 0;
  }
  if (tempSetPointF == 255) {
    tempSetPointF = 78;
  }
  if (humiditySetPoint == 255) {
    humiditySetPoint = 55;
  }
}

// Examines current global variables and if different from the values
//// in EEPROM, will update the appropriate memory address in EEPROM
////
//// memory locations:
////// 0 - operationMode
////// 1 - tempSetPointF
////// 2 - humiditySetPoint
void updateEEPROMValues() {
  if (operationMode != EEPROM.read(0)) {
    EEPROM.write(0, operationMode);
  }
  if (tempSetPointF != EEPROM.read(1)) {
    EEPROM.write(1, tempSetPointF);
  }
  if (humiditySetPoint != EEPROM.read(2)) {
    EEPROM.write(2, humiditySetPoint);
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

void sendJson(char *outputJson){
    // connect to controller, send JSON, get response back and deal with it 
  if (client.connect(controller_ip, controller_port)) {
    Serial.println(F("connected to controller"));
    client.print(F("POST /settings/climaduino/"));
    client.print(DEVICEID);
    client.println(F(" HTTP/1.0"));
    client.print(F("User-Agent: Climaduino Remote Zone "));
    client.println(DEVICEID);
    client.print(F("Content-Length: ")); 
    client.println(strlen(outputJson));
    client.println(F("Connection: close"));
    client.println(F("Content-Type: application/json"));
    client.println(); //blank line between headers and body of request needed
    client.println(outputJson);
    delay(500); //give the controller enough time to formulate a response
    Serial.println(F("Controller response: "));
    boolean capture_response = false; //used to know when to start capturing the response data. We want to ignore the headers
    while (client.available() > 0) {
      char c = client.read();
      if (capture_response == true) {
        // add it to the inputString:
        inputString += c;
         if (c == 'F') { // input to change temperature
          if (inputString.length() <= 3){ // 2 digits and an F
            // remove last character and turn into a float
            inputString = inputString.substring(0, 2);
            char inputCharArray[3]; //only allow values with 2 digits (and 1 null for atoi)
            inputString.toCharArray(inputCharArray, 3);
            // provide some output on Serial
            Serial.print("Temp ");
            Serial.print(tempSetPointF);
            Serial.print(" => ");
            Serial.println(inputCharArray);
            // change the set point
            tempSetPointF = (float)atoi(inputCharArray);
          }
          inputString = ""; // clear inputString
        }
        if (c == '%') { // input to change humidity set point
          if (inputString.length() <= 3){ // 2 digits and a %
            // remove last character and turn into an integer
            inputString = inputString.substring(0, inputString.length() - 1);
            //char inputCharArray[inputString.length() + 1];
            char inputCharArray[3]; //only allow values with 2 digits (and 1 null for atoi)
            inputString.toCharArray(inputCharArray, 3);
            // provide some output on Serial
            Serial.print(F("Humidity "));
            Serial.print(humiditySetPoint);
            Serial.print(" => ");
            Serial.println(inputCharArray);
            // change the set point
            humiditySetPoint = atoi(inputCharArray);
          }
          inputString = ""; // clear inputString
        }
        if (c == 'M') { // input to change operation mode
          if (inputString.length() <= 2){ // 1 digits and an M
            // remove last character and turn into an integer
            inputString = inputString.substring(0, inputString.length() - 1);
            //char inputCharArray[inputString.length() + 1];
            char inputCharArray[2]; //only allow values with 1 digit (and 1 null for atoi)
            inputString.toCharArray(inputCharArray, 2);
            // provide some output on Serial
            Serial.print(F("Mode "));
            Serial.print(operationMode);
            Serial.print(F(" => "));
            Serial.println(inputCharArray);
            // change the mode
            operationMode = atoi(inputCharArray);
           }
          inputString = ""; // clear inputString
        }
      } 
      if (capture_response == false) {
        if (c == '^') {
          capture_response = true;
        }
      }         
    }
    client.stop();
  }
  else {
    Serial.println(F("connection to controller failed"));
  }
}

/*
  SerialEvent occurs whenever a new data comes in the
 hardware serial RX.  This routine is run between each
 time loop() runs, so using delay inside loop can delay
 response.  Multiple bytes of data may be available.
 */
 // Adapted from SerialEvent example sketch  Created 9 May 2011 by Tom Igoe
void serialEvent() {
  // THIS REALLY SHOULD SANITIZE INPUT!!
  while (Serial.available()) {
    // get the new byte:
    char inChar = (char)Serial.read(); 
    // add it to the inputString:
    inputString += inChar;
    // if the incoming character is a newline, set a flag
    // so the main loop can do something about it:
    if (inChar == 'F') { // input to change temperature
      if (inputString.length() <= 3){ // 2 digits and an F
        // remove last character and turn into a float
        inputString = inputString.substring(0, 2);
        char inputCharArray[3]; //only allow values with 2 digits (and 1 null for atoi)
        inputString.toCharArray(inputCharArray, 3);
        tempSetPointF = (float)atoi(inputCharArray);
      }
      inputString = ""; // clear inputString
    }
    if (inChar == '%') { // input to change humidity set point
      if (inputString.length() <= 3){ // 2 digits and a %
        // remove last character and turn into an integer
        inputString = inputString.substring(0, inputString.length() - 1);
        //char inputCharArray[inputString.length() + 1];
        char inputCharArray[3]; //only allow values with 2 digits (and 1 null for atoi)
        inputString.toCharArray(inputCharArray, 3);
        humiditySetPoint = atoi(inputCharArray);
      }
      inputString = ""; // clear inputString
    }
    if (inChar == 'M') { // input to change operation mode
      if (inputString.length() <= 2){ // 1 digits and an M
        // remove last character and turn into an integer
        inputString = inputString.substring(0, inputString.length() - 1);
        //char inputCharArray[inputString.length() + 1];
        char inputCharArray[2]; //only allow values with 1 digit (and 1 null for atoi)
        inputString.toCharArray(inputCharArray, 2);
        operationMode = atoi(inputCharArray);
       }
      inputString = ""; // clear inputString
    }
  }
}

// =============================================================== //
// Setup                                                           //
// =============================================================== //
void setup()
{
  readEEPROMValues();
  Serial.begin(9600);  //Start the Serial connection with the computer
  //to view the result open the Serial monitor
  Serial.println(F("Connecting to Ethernet network using DHCP"));
  if (Ethernet.begin(mac) == 1) { //try to connect first using DHCP. If that fails, use a fixed IP.
    Serial.print(F("SUCCESS. IP: "));
    Serial.println(Ethernet.localIP());
  }
  else {
    // DHCP failed, so use a fixed IP address:
    Serial.print(F("FAILED. Using IP: "));
    Serial.println(ip);
    Ethernet.begin(mac, ip);
  }
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
  thermostat.mode = operationMode;
  thermostat.tempSetPoint = tempSetPointF;
  thermostat.humiditySetPoint = humiditySetPoint;
  averageReadings(); // get the average readings
  char* outputJson = thermostat.Control(averageTemp, averageHumidity);
  Serial.println(outputJson);
  sendJson(outputJson);
  // Update EEPROM with any changes to operating parameters
  free(outputJson);
  updateEEPROMValues();
  wdt_reset(); //reset watchdog timer
}

ISR(WDT_vect)
{ // what happens when the watchdog interrupt is triggered
  
}
