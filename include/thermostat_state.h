#pragma once

#include <Arduino.h>
#include <atomic>

enum class ThermoMode : uint8_t {
  Auto = 0,
  Manual = 1,
};

struct ThermostatState {
  std::atomic<float> temperatureC{25.0f};
  std::atomic<float> setpointC{28.0f};
  std::atomic<uint8_t> mode{static_cast<uint8_t>(ThermoMode::Auto)};
  std::atomic<uint8_t> fanDuty{50};      // 0–100, applied in Manual; reported always
  std::atomic<uint16_t> rpm{0};
  std::atomic<float> ntcBeta{3950.0f};
};

extern ThermostatState gState;

inline float clampf(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}
