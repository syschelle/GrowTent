//check ever 30 seconds the current vpd with target vpd, if current vpd higer than taget vpd then power on the humidifyer shelly
//after 11 second the shelly for the humidifyer turns automaticly off. Configure that in the Webinterface oft the shelly (auto off).
#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <Adafruit_BME280.h>
#include <config.h>

extern Preferences preferences;
extern bool bmeAvailable;
extern Adafruit_BME280 bme;

void taskCheckBMESensor(void *parameter){
  for (;;) {
    // Read sensor temperatur, humidity and vpd
    readSensorData();
    // delay  10 seconds
    delay(10000); 
  }
}