// Library to implement a thermostat on the Arduino. Created by Brian Bustin. 2014
#ifndef Thermostat_h
#define Thermostat_h
#include "Arduino.h"
#include "MemoryFree.h" //for reporting on free memory
// the #include statment and code go here...
class Thermostat 
{
	public:
		Thermostat(int pinCool, int pinHeat, int pinFan, bool heatPump);
		void Control(float temperature, float humidity);
		bool CurrentlyRunning();
		bool StateChangeAllowed();
		int mode;
		int tempHysteresis;
		int humidityHysteresis;
		int humidityOverCooling;
		unsigned long minRunTimeMillis;
		unsigned long minOffTimeMillis;
		int tempSetPoint;
		int humiditySetPoint;
		float averageTemp;
		float averageHumidity;
	private:
		int _pinCool;
		int _pinHeat;
		int _pinFan;
		bool _heatPump;
		float _temperature;
		float _humidity;
		bool _currentlyRunning;
		char* _jsonOutput;
		unsigned long _stateChangeMillis;
		char* _crashReportData; //crash report data to narrow things down
		bool _stateChangeAllowed;
		unsigned long _millisSinceLastStateChange();
		bool _shortCycleProtection();
		void _changePowerState(bool state, bool updateStateChangeMillis);
		char* _jsonCreate(float temperature, float humidity, bool stateChangeAllowed);
};
#endif