//check every 30 seconds the current vpd with target vpd, if current vpd higher than target vpd then power on the humidifier shelly
//after 11 seconds the shelly for the humidifier turns automatically off. Configure that in the Webinterface of the shelly (auto off).
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
    // Read sensor temperatur, humidity and vpd every 10 seconds
    readSensorData();
    // delay  10 seconds
    delay(10000); 
  }
}