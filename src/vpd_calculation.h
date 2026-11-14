#pragma once
#include <cmath>

// Pure calculation: raw telemetry never reads or advances control filter state.
inline float calcVPD(float temperatureC, float leafOffset, float humidityPct) {
  if (!std::isfinite(temperatureC) || !std::isfinite(leafOffset) ||
      !std::isfinite(humidityPct)) return NAN;
  const float humidity = fmaxf(0.0f, fminf(100.0f, humidityPct));
  const float leafTemperature = temperatureC + leafOffset;
  const float leafPressure = 610.7f * powf(10.0f,
      7.5f * leafTemperature / (237.3f + leafTemperature)) / 1000.0f;
  const float airPressure = 610.7f * powf(10.0f,
      7.5f * temperatureC / (237.3f + temperatureC)) / 1000.0f;
  const float vpd = leafPressure - humidity / 100.0f * airPressure;
  return std::isfinite(vpd) ? fmaxf(0.0f, vpd) : NAN;
}

struct VpdControlFilter {
  float temperatureC = NAN;
  float humidityPct = NAN;

  float update(float temperature, float leafOffset, float humidity) {
    if (!std::isfinite(temperature) || !std::isfinite(leafOffset) ||
        !std::isfinite(humidity)) return NAN;
    humidity = fmaxf(0.0f, fminf(100.0f, humidity));
    if (std::isnan(temperatureC)) temperatureC = temperature;
    if (std::isnan(humidityPct)) humidityPct = humidity;
    // Preserve the existing additional control smoothing (alpha 0.10).
    temperatureC = 0.10f * temperature + 0.90f * temperatureC;
    humidityPct = 0.10f * humidity + 0.90f * humidityPct;
    return calcVPD(temperatureC, leafOffset, humidityPct);
  }
};

inline float calcControlVPD(float temperature, float leafOffset, float humidity) {
  static VpdControlFilter filter;
  return filter.update(temperature, leafOffset, humidity);
}
