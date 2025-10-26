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
    if (bmeAvailable) {
      unsigned long now = millis();
      if (now - lastRead >= READ_INTERVAL_MS) {
        lastRead = now;
        lastTemperature = bme.readTemperature();
        lastHumidity = bme.readHumidity();
        float svp = 0.6108f * exp((17.27f * lastTemperature) / (lastTemperature + 237.3f));
        lastVPD = svp - (lastHumidity / 100.0f) * svp;

        Serial.println("[SENSOR] Last Sensorupdate Temperature: " + String(lastTemperature, 1) + " °C, Humidity: " + String(lastHumidity, 0) + " %, VPD: " + String(lastVPD, 1) + " kPa");
      }
    }
    // delay  10 seconds
    delay(10000); 
  }
}