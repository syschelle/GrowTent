#pragma once
#include <stdint.h>
#include <cmath>
namespace gtfan {
constexpr uint32_t kRefreshMs = 3600000UL;
// Keep deadlines within signed millis() arithmetic, including rollover.
inline uint32_t durationMs(double seconds) {
  if (!std::isfinite(seconds) || seconds <= 0 || seconds > 2147483.0) return 0;
  return static_cast<uint32_t>(seconds * 1000.0);
}
inline bool expired(uint32_t now, uint32_t deadline) { return int32_t(now - deadline) >= 0; }
inline bool refreshDue(bool attempted, uint32_t now, uint32_t last) {
  return !attempted || uint32_t(now - last) >= kRefreshMs;
}
struct Timer {
  bool running = false;
  uint32_t deadline = 0;
  void command(bool on, bool success, uint32_t started, uint32_t duration) {
    if (!success) return;
    running = on && duration > 0;
    if (running) deadline = started + duration;
  }
  bool tick(uint32_t now) {
    if (running && expired(now, deadline)) running = false;
    return running;
  }
};
}
