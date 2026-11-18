#pragma once

#include <Arduino.h>

// Custom 128-bit UUID base: a1b3XXXX-c3d4-4e5f-8a9b-0c1d2e3f4a5b
// (bumped a1b2→a1b3 to invalidate phone GATT caches after firmware changes)
namespace BleIds {
constexpr const char *kDeviceName = "taarm-thermo";
constexpr const char *kService = "a1b30001-c3d4-4e5f-8a9b-0c1d2e3f4a5b";
constexpr const char *kTemperature = "a1b30002-c3d4-4e5f-8a9b-0c1d2e3f4a5b";
constexpr const char *kSetpoint = "a1b30003-c3d4-4e5f-8a9b-0c1d2e3f4a5b";
constexpr const char *kMode = "a1b30004-c3d4-4e5f-8a9b-0c1d2e3f4a5b";
constexpr const char *kFanDuty = "a1b30005-c3d4-4e5f-8a9b-0c1d2e3f4a5b";
constexpr const char *kRpm = "a1b30006-c3d4-4e5f-8a9b-0c1d2e3f4a5b";
}  // namespace BleIds

void bleBegin();
void bleNotifyStatus();
bool bleIsConnected();
