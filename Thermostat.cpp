#include "Arduino.h"
#include "Thermostat.h"

Thermostat::Thermostat(int pinCool, int pinHeat, int pinFan, bool heatPump)
{
	pinMode(pinCool, OUTPUT);
	pinMode(pinHeat, OUTPUT);
	pinMode(pinFan, OUTPUT);
	_pinCool = pinCool;
	_pinHeat = pinHeat;
	_pinFan = pinFan;
	_heatPump = heatPump;
	_currentlyRunning = false;
	_stateChangeMillis = millis(); //so we automatically wait before turning on. useful for cases where the power goes out while the compressor is running.
	/* Setting some defaults for publically-accessible properties. These can be overridden. */
	mode = 9; //defaults to mode 9 - off (0 cooling + humidity, 1 humidity control, 5 heating, 8 fan, 9 off)
	tempHysteresis = 2; //amount above or below (depending on mode) the threshold allowed
	humidityHysteresis = 2;  //amount above the threshold allowed
	humidityOverCooling = 5; //degrees cooler than setpoint allowed to dehumidify when in cooling mode
	minRunTimeMillis = 600000; //cooling minimum runtime allowed (prevent short cycles) - unsigned long to match millis datatype
	minOffTimeMillis = 180000; //cooling minimum off time before can run again (protect compressor) - unsigned long to match millis datatype
}

/* Calculate how long the system has been in the current state */
unsigned long Thermostat::_millisSinceLastStateChange()
{
	/* millis rolls over after a while if a rollover occurs,
	reset the stateChangeMillis variable to the current millis()
	count. */
	if (millis() < _stateChangeMillis)
	{
		_stateChangeMillis = millis();
	}
	unsigned long millis_in_state = millis() - _stateChangeMillis;
	return millis_in_state;
}

/* Protects compressor by not turning it back on until it has been off for minOffTimeSeconds
 Keeps compressor run cycles from being too short by running at least _minRunTimeSeconds

 returns true if state change is ok, returns false if it is not */
bool Thermostat::_shortCycleProtection()
{
	unsigned long totalTimeInState = _millisSinceLastStateChange();
	switch(_currentlyRunning){
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

/* Changes the power state (if needed) updates the stateChangeMillis and currentlyRunning global variables
when state is changed

passing in true means turn on */
void Thermostat::_changePowerState(bool state, bool updateStateChangeMillis=true){
	switch(state){
		case false:
			if (_currentlyRunning == true) {
				if (updateStateChangeMillis){
					_stateChangeMillis = millis();
				}
			}
			digitalWrite(_pinCool, LOW);
			digitalWrite(_pinHeat, LOW);
			digitalWrite(_pinFan, LOW);
			_currentlyRunning = false;
			break;
		case true:
			if (_currentlyRunning == false) {
				if (updateStateChangeMillis){
					_stateChangeMillis = millis();
				}
			}
			switch(mode){
				case 0: //cooling mode
					digitalWrite(_pinCool, HIGH);
					digitalWrite(_pinHeat, LOW);
					digitalWrite(_pinFan, LOW);
					break;
				case 1: //humidity control mode
					digitalWrite(_pinCool, HIGH);
					digitalWrite(_pinHeat, LOW);
					digitalWrite(_pinFan, LOW);
					break;
				case 5: //heating mode
					digitalWrite(_pinCool, LOW);
					digitalWrite(_pinHeat, HIGH);
					digitalWrite(_pinFan, LOW);
					break;
				case 8: //fan only
					digitalWrite(_pinCool, LOW);
					digitalWrite(_pinHeat, LOW);
					digitalWrite(_pinFan, HIGH);
					break;
			}
			_currentlyRunning = true;
			break;
	}
}

bool Thermostat::CurrentlyRunning()
{
	return _currentlyRunning;
}

bool Thermostat::StateChangeAllowed()
{
	return _stateChangeAllowed;
}

void Thermostat::Control(float temperature, float humidity)
{
	/* Check to make sure we are not short-cycling if in cooling, humidity control, or heating mode
	if a heat pump in use */
	if (mode == 0 || mode == 1){ //operation modes that involve the compressor
		_stateChangeAllowed = _shortCycleProtection();
	}
	else if (mode == 5 && _heatPump) { //heating with a heat pump involves the compressor too and must be protected
		_stateChangeAllowed = _shortCycleProtection();
	}
	else {
		_stateChangeAllowed = true;
	}

	if (_stateChangeAllowed){
		// create local variables for temp and humidity set points so we can modify temporarily
		int _setPointF = tempSetPoint; 
		int _humiditySetPoint = humiditySetPoint;

		// hysteresis considerations if system is not running
		if (!_currentlyRunning) {
			_humiditySetPoint += humidityHysteresis; // humidity is allowed to be over by humidity hysteresis
			// for temperature hysteresis settings we need to check whether heating or cooling
			switch(mode){
				case 0: //cooling mode
					_setPointF += tempHysteresis; // temperature is allowed to be over setPoint by tempHysteresis
					break;
				case 5: //heating mode
					_setPointF -= tempHysteresis; // temperature is allowed to be under setPoint by tempHysteresis
					break;
		  	}
		}
		// Actions to take for each mode
		switch (mode){
			case 0: // coolimg mode
				// first deal with humidity if too high, adjust _setPointF by humidityOverCooling
				if (round(humidity) > _humiditySetPoint) {
					_setPointF -= humidityOverCooling; 
				}
				// check if temperature is higher than tempSetPoint
				if (round(emperature) > _setPointF) {
				 	_changePowerState(true);
				}
				else {
					_changePowerState(false); 
				}
				break;
		 	case 1: // humidity control mode
				// check if humidity is higher than setpoint
				if (round(humidity) > _humiditySetPoint) {
				 	_changePowerState(true);
				}
				else {
					_changePowerState(false); 
				}
				break;
			case 5: // heating mode
				// check if temperature is less than tempSetPoint
				if (round(temperature) < _setPointF) {
					if (_heatPump) {
						_changePowerState(true);
					}
					else {
						_changePowerState(true, false); // if not using a heat pump, no need to record last time system came on
					}
					
				}
				else {
					if (_heatPump) {
							_changePowerState(false);
						}
						else {
							_changePowerState(false, false); // if not using a heat pump, no need to record last time system came on
						}
				}
				break;
			case 8: // fan only mode
				_changePowerState(true, false); //change power state, but do not record last state change
				break;
			default: //if mode is unrecognized, power should be off
				_changePowerState(false); //record when power state was changed as it could have previously been in a mode that used the compressor
		}
	}
	return;
}
