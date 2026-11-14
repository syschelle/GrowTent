#include "climate_safety.h"
#include <Arduino.h>
#include <atomic>
namespace {
std::atomic<uint32_t> lastGoodMs{0};
std::atomic<bool> haveGood{false};
std::atomic<bool> validSample{false};
}
void climateRecordSample(bool valid, uint32_t nowMs) {
  if (!valid) { validSample.store(false); return; }
  lastGoodMs.store(nowMs);
  haveGood.store(true);
  validSample.store(true);
}
uint32_t climateReadingAgeMs() {
  return haveGood.load() ? uint32_t(millis() - lastGoodMs.load()) : UINT32_MAX;
}
bool climateReadingsFresh() {
  return validSample.load() && haveGood.load() && climateReadingAgeMs() < CLIMATE_MAX_AGE_MS;
}
const char* climateReadingStatus() {
  if (!haveGood.load()) return "unavailable";
  if (!validSample.load()) return "invalid";
  return climateReadingsFresh() ? "ok" : "stale";
}
