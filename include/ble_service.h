#pragma once

#include <Arduino.h>

// Custom 128-bit UUID base: a1b2XXXX-c3d4-4e5f-8a9b-0c1d2e3f4a5b
namespace BleIds {
constexpr const char *kDeviceName = "taarm-thermo";
constexpr const char *kService = "a1b20001-c3d4-4e5f-8a9b-0c1d2e3f4a5b";
constexpr const char *kTemperature = "a1b20002-c3d4-4e5f-8a9b-0c1d2e3f4a5b";
constexpr const char *kSetpoint = "a1b20003-c3d4-4e5f-8a9b-0c1d2e3f4a5b";
constexpr const char *kMode = "a1b20004-c3d4-4e5f-8a9b-0c1d2e3f4a5b";
constexpr const char *kFanDuty = "a1b20005-c3d4-4e5f-8a9b-0c1d2e3f4a5b";
constexpr const char *kRpm = "a1b20006-c3d4-4e5f-8a9b-0c1d2e3f4a5b";
}  // namespace BleIds

void bleBegin();
void bleNotifyStatus();
bool bleIsConnected();
