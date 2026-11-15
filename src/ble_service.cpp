#include "ble_service.h"

#include "thermostat_state.h"

#include <NimBLEDevice.h>
#include <cstring>

namespace {

NimBLEServer *server = nullptr;
NimBLECharacteristic *tempChar = nullptr;
NimBLECharacteristic *setpointChar = nullptr;
NimBLECharacteristic *modeChar = nullptr;
NimBLECharacteristic *dutyChar = nullptr;
NimBLECharacteristic *rpmChar = nullptr;
bool deviceConnected = false;
bool oldDeviceConnected = false;

void writeFloatLe(NimBLECharacteristic *c, float value) {
  uint8_t buf[4];
  memcpy(buf, &value, 4);
  c->setValue(buf, 4);
}

float readFloatLe(NimBLECharacteristic *c) {
  std::string v = c->getValue();
  if (v.size() < 4) {
    return NAN;
  }
  float out;
  memcpy(&out, v.data(), 4);
  return out;
}

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *pServer) override {
    deviceConnected = true;
  }

  void onDisconnect(NimBLEServer *pServer) override {
    deviceConnected = false;
  }
};

class SetpointCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *c) override {
    float v = readFloatLe(c);
    if (isnan(v)) {
      return;
    }
    gState.setpointC.store(clampf(v, 15.0f, 40.0f));
    writeFloatLe(c, gState.setpointC.load());
  }
};

class ModeCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *c) override {
    std::string v = c->getValue();
    if (v.empty()) {
      return;
    }
    uint8_t m = static_cast<uint8_t>(v[0]);
    if (m > 1) {
      m = 1;
    }
    gState.mode.store(m);
    c->setValue(&m, 1);
  }
};

class DutyCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *c) override {
    std::string v = c->getValue();
    if (v.empty()) {
      return;
    }
    uint8_t d = static_cast<uint8_t>(v[0]);
    if (d > 100) {
      d = 100;
    }
    gState.fanDuty.store(d);
    // Only applied by control loop when mode == Manual
    c->setValue(&d, 1);
  }
};

}  // namespace

void bleBegin() {
  NimBLEDevice::init(BleIds::kDeviceName);
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);

  server = NimBLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());

  NimBLEService *svc = server->createService(BleIds::kService);

  tempChar = svc->createCharacteristic(
      BleIds::kTemperature,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  writeFloatLe(tempChar, gState.temperatureC.load());

  setpointChar = svc->createCharacteristic(
      BleIds::kSetpoint,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  setpointChar->setCallbacks(new SetpointCallbacks());
  writeFloatLe(setpointChar, gState.setpointC.load());

  modeChar = svc->createCharacteristic(
      BleIds::kMode,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  modeChar->setCallbacks(new ModeCallbacks());
  uint8_t mode = gState.mode.load();
  modeChar->setValue(&mode, 1);

  dutyChar = svc->createCharacteristic(
      BleIds::kFanDuty,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR |
          NIMBLE_PROPERTY::NOTIFY);
  dutyChar->setCallbacks(new DutyCallbacks());
  uint8_t duty = gState.fanDuty.load();
  dutyChar->setValue(&duty, 1);

  rpmChar = svc->createCharacteristic(
      BleIds::kRpm, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  uint16_t rpm = gState.rpm.load();
  rpmChar->setValue(reinterpret_cast<uint8_t *>(&rpm), 2);

  svc->start();

  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(BleIds::kService);
  adv->setScanResponse(true);
  adv->start();
}

void bleNotifyStatus() {
  if (!deviceConnected) {
    if (oldDeviceConnected) {
      delay(100);
      server->startAdvertising();
      oldDeviceConnected = deviceConnected;
    }
    return;
  }
  oldDeviceConnected = deviceConnected;

  writeFloatLe(tempChar, gState.temperatureC.load());
  tempChar->notify();

  writeFloatLe(setpointChar, gState.setpointC.load());

  uint8_t mode = gState.mode.load();
  modeChar->setValue(&mode, 1);

  uint8_t duty = gState.fanDuty.load();
  dutyChar->setValue(&duty, 1);
  dutyChar->notify();

  uint16_t rpm = gState.rpm.load();
  rpmChar->setValue(reinterpret_cast<uint8_t *>(&rpm), 2);
  rpmChar->notify();
}

bool bleIsConnected() {
  return deviceConnected;
}
