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
  NimBLEAttValue v = c->getValue();
  if (v.size() < 4) {
    return NAN;
  }
  float out;
  memcpy(&out, v.data(), 4);
  return out;
}

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo) override {
    (void)pServer;
    (void)connInfo;
    deviceConnected = true;
  }

  void onDisconnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo, int reason) override {
    (void)pServer;
    (void)connInfo;
    (void)reason;
    deviceConnected = false;
  }
};

class SetpointCallbacks : public NimBLECharacteristicCallbacks {
  void onRead(NimBLECharacteristic *c, NimBLEConnInfo &connInfo) override {
    (void)connInfo;
    Serial.printf("BLE setpoint read -> %.2f C\n", gState.setpointC.load());
    writeFloatLe(c, gState.setpointC.load());
  }

  void onWrite(NimBLECharacteristic *c, NimBLEConnInfo &connInfo) override {
    (void)connInfo;
    NimBLEAttValue raw = c->getValue();
    Serial.printf("BLE setpoint write len=%u data=", (unsigned)raw.size());
    for (size_t i = 0; i < raw.size(); i++) {
      Serial.printf("%02X", raw.data()[i]);
    }
    Serial.println();
    if (raw.size() == 0) {
      return;
    }

    bool looksAscii = true;
    for (size_t i = 0; i < raw.size(); i++) {
      const char ch = static_cast<char>(raw.data()[i]);
      if (!(isdigit(static_cast<unsigned char>(ch)) || ch == '.' || ch == '-' ||
            ch == '+' || ch == ' ')) {
        looksAscii = false;
        break;
      }
    }

    float v = NAN;
    if (looksAscii) {
      // nRF Connect TEXT: "30" or "30.5"
      char tmp[16];
      size_t n = raw.size() < sizeof(tmp) - 1 ? raw.size() : sizeof(tmp) - 1;
      memcpy(tmp, raw.data(), n);
      tmp[n] = '\0';
      char *end = nullptr;
      float parsed = strtof(tmp, &end);
      if (end != tmp) {
        v = parsed;
      }
    } else if (raw.size() == 1) {
      // nRF Connect BYTE: 0x1E = 30°C
      v = static_cast<float>(raw.data()[0]);
    } else if (raw.size() == 4) {
      // App path: 4-byte little-endian float.
      memcpy(&v, raw.data(), 4);
    }

    if (isnan(v)) {
      Serial.println("BLE setpoint parse failed");
      return;
    }
    gState.setpointC.store(clampf(v, 15.0f, 40.0f));
    writeFloatLe(c, gState.setpointC.load());
    Serial.printf("BLE setpoint -> %.2f C\n", gState.setpointC.load());
  }
};

class ModeCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *c, NimBLEConnInfo &connInfo) override {
    (void)connInfo;
    NimBLEAttValue v = c->getValue();
    if (v.size() < 1) {
      return;
    }
    uint8_t m = v.data()[0];
    if (m > 1) {
      m = 1;
    }
    gState.mode.store(m);
    c->setValue(&m, 1);
  }
};

class DutyCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *c, NimBLEConnInfo &connInfo) override {
    (void)connInfo;
    NimBLEAttValue v = c->getValue();
    if (v.size() < 1) {
      return;
    }
    uint8_t d = v.data()[0];
    if (d > 100) {
      d = 100;
    }
    gState.fanDuty.store(d);
    c->setValue(&d, 1);
  }
};

}  // namespace

void bleBegin() {
  NimBLEDevice::init(BleIds::kDeviceName);
  // Lower TX power to reduce brownout risk on weak 3.3V supplies (e.g. CH340 regulator).
  NimBLEDevice::setPower(ESP_PWR_LVL_P3);

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

  // 31-byte adv limit: name in primary packet, 128-bit service UUID in scan response.
  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  NimBLEAdvertisementData advData;
  advData.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
  advData.setName(BleIds::kDeviceName, true);

  NimBLEAdvertisementData scanData;
  scanData.addServiceUUID(BleIds::kService);

  adv->setAdvertisementData(advData);
  adv->setScanResponseData(scanData);
  adv->enableScanResponse(true);
  adv->start();
}

void bleNotifyStatus() {
  if (!deviceConnected) {
    if (oldDeviceConnected) {
      delay(100);
      NimBLEDevice::startAdvertising();
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
