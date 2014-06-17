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

// =============================================================== //
// statically defined variables - these can not be changed later   //
// =============================================================== //
const int DEVICEID = 1; //Device identifier so it can be distinguished from other zones

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
const int pinRelay = 8; // pin for relay
const int pinSensor = 9; // pin for temperature and humidity (DHT-22)

byte mac[] = { 0x48, 0xC2, 0xA1, 0xF3, 0x8D, 0xB7 }; // if you have multiple climaduino zones, they must have unique MAC addresses
IPAddress ip(192, 168, 1, 150);
IPAddress controller_ip(192, 168, 2, 5); // ip address of the web controller, note: you may want to statically assign the Controller's address.
const int controller_port = 80;

// =============================================================== //
// Global variables                                                //
// =============================================================== //
volatile int tempSetPointF; // temperature set point. Volatile so it can be changed from within an
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

// =============================================================== //
// Helper functions                                                //
// =============================================================== //

// Reads back values for our settings from EEPROM and
//// populates the proper global variables. If The EEPROM locations have
//// not yet been set, then returns default values.
////
//// memory locations:
////// 0 - operationMode (default 0)
////// 1 - tempSetPointF (default 78)
////// 2 - humiditySetPoint (default 55)
void readEEPROMValues() {
  // get operationMode from 0
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

// Changes the power state (if needed)
//// updates the stateChangeMillis and currentlyRunning global variables when state is changed
////
//// passing in true means turn on
//// passing in false means turn off
void powerState(boolean state){
  switch(state){
        case false:
          if (currentlyRunning == true) {
            digitalWrite(pinRelay, LOW);
            currentlyRunning = false; //set global variable that keeps track of system status
            stateChangeMillis = millis();
          }
          break;
        case true:
          if (currentlyRunning == false) {
            digitalWrite(pinRelay, HIGH);
            currentlyRunning = true; //set global variable that keeps track of system status
            stateChangeMillis = millis();
          }
          break;
  }
}

// Reports total time since last state change in seconds
// REPLACING WITH MILLIS SINCE LAST STATE CHANGE
//int secondsSinceLastStateChange() {
//  // millis rolls over after a while if a rollover occurs,
//  //// reset the stateChangeMillis variable to the current millis()
//  //// count.
//  ////
//  //// This may lead the program to have the unit run for longer
//  //// or keep off for longer than the minRunTimeSeconds and minOffTimeSeconds
//  //// but this is probably ok since an overflow should only occur
//  //// once every 50 days according to http://arduino.cc/en/Reference/millis.
//  if (millis() < stateChangeMillis) {
//   stateChangeMillis = millis();
//  }
//  float seconds = (millis() - stateChangeMillis) / 1000;
//  return seconds;
//}

// Reports total time since last state change in millis
unsigned long millisSinceLastStateChange() {
  // millis rolls over after a while if a rollover occurs,
  //// reset the stateChangeMillis variable to the current millis()
  //// count.
  ////
  //// This may lead the program to have the unit run for longer
  //// or keep off for longer than the minRunTimeSeconds and minOffTimeSeconds
  //// but this is probably ok since an overflow should only occur
  //// once every 50 days according to http://arduino.cc/en/Reference/millis.
  if (millis() < stateChangeMillis) {
   stateChangeMillis = millis();
  }
  unsigned long millis_in_state = millis() - stateChangeMillis;
  return millis_in_state;
}

// Short cycle protection
//// Protects compressor by not turning it back on until it has been off for minOffTimeSeconds
//// Keeps compressor run cycles from being too short by running at least minRunTimeSeconds
////
//// returns true if state change is ok, returns false if it is not
boolean shortCycleProtection(){
  unsigned long totalTimeInState = millisSinceLastStateChange();
  switch(currentlyRunning){
    case false:
      if (totalTimeInState > minOffTimeMillis){
        return true;
      }
      else {
        return false;
      }
      break;
    case true:
      if (totalTimeInState > minRunTimeMillis){
        return true;
      }
      else {
        return false;
      }
      break;
    default:
      return false;
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

// Thermostat
//// implements thermostat logic
////
//// uses the following static variables:
//// humiditySetPoint, tempHysteresis, humidityHysteresis, humidityOverCooling
////
//// uses the following variables uodated by helper functions and ISRs:
//// averageTemp, averageHumidity, currentlyRunning, operationMode
void thermostat(){
  // create local variables for temp and humidity set points so we can modify temporarily
  int _setPointF = tempSetPointF; 
  int _humiditySetPoint = humiditySetPoint;
  
  // hysteresis considerations if system is not running
  if (!currentlyRunning) {
    _humiditySetPoint += humidityHysteresis; // humidity is allowed to be over by humidity hysteresis
    // for temperature hysteresis settings we need to check whether heating or cooling
    switch(operationMode){
    case 0: //cooling mode
      _setPointF += tempHysteresis; // temperature is allowed to be over setPoint by tempHysteresis
      break;
    case 5: //heating mode
      _setPointF -= tempHysteresis; // temperature is allowed to be under setPoint by tempHysteresis
      break;
    }
  }
  // Actions to take for each mode
  switch (operationMode){
    case 0: // coolimg mode
      // first deal with humidity if too high, adjust _setPointF by humidityOverCooling
      if (averageHumidity > _humiditySetPoint) {
       _setPointF -= humidityOverCooling; 
      }
      // check if temperature is higher than tempSetPoint
      if (averageTemp > _setPointF) {
        powerState(true);
      }
      else {
       powerState(false); 
      }
      break;
    case 1: // humidity control mode
      // check if humidity is higher than setpoint
      if (averageHumidity > _humiditySetPoint) {
        powerState(true);
      }
      else {
       powerState(false); 
      }
      break;
     default: //if mode is unrecognized, power should be off
       powerState(false);
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
  Serial.println("Connecting to ethernet network using DHCP");
  if (Ethernet.begin(mac) == 1) { //try to connect first using DHCP. If that fails, use a fixed IP.
    Serial.print("SUCCESS. IP: ");
    Serial.println(Ethernet.localIP());
  }
  else {
    // DHCP failed, so use a fixed IP address:
    Serial.print("FAILED. Using IP: ");
    Serial.println(ip);
    Ethernet.begin(mac, ip);
  }
  dht.begin(); //start up DHT library;
  pinMode(pinRelay, OUTPUT); //setting up pin for relay
  stateChangeMillis = millis(); // so that we automatically wait before turning on
    // useful for cases where the power goes out while the compressor is running
}

// =============================================================== //
// Main program loop                                               //
// =============================================================== //
void loop(){
  boolean stateChangeAllowed;
  averageReadings(); // get the average readings
  
  if (operationMode == 0 || operationMode == 1){ // operation modes that involve the compressor
    stateChangeAllowed = shortCycleProtection();
  }
  else { // all other operation modes
    stateChangeAllowed = true;
  }
  // Need to build a string of JSON so we can get the content-length. To ad the floats in we need
  // to convert them th char and then to string...
  // convert readings to char
  char char_averageTemp[6];
  dtostrf(averageTemp, 1, 2, char_averageTemp);
  
  char char_averageHumidity[6];
    dtostrf(averageHumidity, 1, 2, char_averageHumidity);
  
  String json_string;
  json_string = "{\"readings\":{\"temp\":" + String(char_averageTemp) + ",\"humidity\":" + String(char_averageHumidity) + "}," +
                  "\"parameters\":{\"temp\":" + tempSetPointF + ",\"humidity\":" + humiditySetPoint + ",\"mode\":" + operationMode + "}," +
                  "\"status\":{" + "\"state_change_allowed\":\"";
  if (stateChangeAllowed){
     json_string += "Y";
      thermostat(); //runs the thermostat logic
  }
  else {
      json_string += "N";
  }
  json_string += "\",\"system_running\":\"";
  if (currentlyRunning){
      json_string += "Y";
  }
  else {
      json_string += "N";
  }
  json_string += "\",\"lastStateChange\":\"" + String(stateChangeMillis) + "\",\"millis\":\"" + String(millis()) + "\"}}";
  Serial.println(json_string);
  // connect to controller
  if (client.connect(controller_ip, controller_port)) {
    Serial.println("connected to controller");
    client.print("POST /settings/climaduino/");
    client.print(DEVICEID);
    client.println(" HTTP/1.0");
    client.print("User-Agent: Climaduino Remote Zone ");
    client.println(DEVICEID);
    client.print("Content-Length: "); 
    client.println(json_string.length());
    client.println("Connection: close");
    client.println("Content-Type: application/json");
    client.println(); //blank line between headers and body of request needed
    client.println(json_string);
    delay(500); //give the controller enough time to formulate a response
    Serial.println("Controller response: ");
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
            Serial.print("Humidity ");
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
            Serial.print("Mode ");
            Serial.print(operationMode);
            Serial.print(" => ");
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
    Serial.println("connection to controller failed");
  }
  // Update EEPROM with any changes to operating parameters
  updateEEPROMValues();
}
