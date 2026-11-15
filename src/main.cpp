#include <Arduino.h>
#include <cmath>
#include <cstring>

#include "ble_service.h"
#include "thermostat_state.h"

ThermostatState gState;

// Pins (ESP32-C3-WROOM-02)
static constexpr int kPinNtc = 3;    // ADC1_CH3
static constexpr int kPinPwm = 6;    // Fan PWM
static constexpr int kPinTach = 7;   // Fan tach (open-collector)

static constexpr float kVref = 3.3f;
static constexpr float kSeriesOhm = 10000.0f;
static constexpr float kNtcR0 = 10000.0f;
static constexpr float kNtcT0 = 298.15f;  // 25 °C

static constexpr int kPwmFreqHz = 25000;
static constexpr int kPwmResolution = 10;  // 0–1023

static constexpr float kHysteresisC = 0.5f;
static constexpr float kAutoRampSpanC = 4.0f;
static constexpr uint8_t kAutoMinDuty = 0;
static constexpr uint8_t kAutoStartDuty = 30;

static volatile uint32_t tachPulses = 0;
static String serialLine;

static void IRAM_ATTR onTach() {
  tachPulses = tachPulses + 1;
}

static float readNtcCelsius() {
  const int samples = 16;
  uint32_t sumMv = 0;
  for (int i = 0; i < samples; i++) {
    sumMv += analogReadMilliVolts(kPinNtc);
    delayMicroseconds(200);
  }
  float v = (sumMv / static_cast<float>(samples)) / 1000.0f;
  if (v < 0.01f) {
    v = 0.01f;
  }
  if (v > kVref - 0.01f) {
    v = kVref - 0.01f;
  }

  // Divider: 3V3 — Rfixed — sense — NTC — GND
  float rNtc = kSeriesOhm * v / (kVref - v);
  float beta = gState.ntcBeta.load();
  float invT = (1.0f / kNtcT0) + (1.0f / beta) * logf(rNtc / kNtcR0);
  return (1.0f / invT) - 273.15f;
}

static uint8_t computeAutoDuty(float tempC, float setpointC) {
  if (tempC <= setpointC - kHysteresisC) {
    return kAutoMinDuty;
  }
  if (tempC >= setpointC + kAutoRampSpanC) {
    return 100;
  }
  // Ramp from start duty at setpoint to 100% at setpoint + ramp span
  float t0 = setpointC;
  float t1 = setpointC + kAutoRampSpanC;
  float x = (tempC - t0) / (t1 - t0);
  x = clampf(x, 0.0f, 1.0f);
  float duty = kAutoStartDuty + x * (100.0f - kAutoStartDuty);
  return static_cast<uint8_t>(lroundf(duty));
}

static void applyFanDuty(uint8_t dutyPercent) {
  if (dutyPercent > 100) {
    dutyPercent = 100;
  }
  uint32_t maxDuty = (1u << kPwmResolution) - 1u;
  uint32_t ticks = (dutyPercent * maxDuty) / 100u;
  ledcWrite(kPinPwm, ticks);
}

static void updateRpm(uint32_t elapsedMs) {
  noInterrupts();
  uint32_t pulses = tachPulses;
  tachPulses = 0;
  interrupts();
  if (elapsedMs == 0) {
    return;
  }
  // 2 pulses per revolution for PC PWM fans
  float rps = (pulses / 2.0f) * (1000.0f / elapsedMs);
  uint16_t rpm = static_cast<uint16_t>(lroundf(rps * 60.0f));
  gState.rpm.store(rpm);
}

static void printStatus() {
  Serial.printf(
      "temp=%.2fC set=%.2fC mode=%s duty=%u%% rpm=%u ble=%s beta=%.0f\n",
      gState.temperatureC.load(), gState.setpointC.load(),
      gState.mode.load() == 0 ? "auto" : "manual", gState.fanDuty.load(),
      gState.rpm.load(), bleIsConnected() ? "yes" : "no", gState.ntcBeta.load());
}

static void handleSerialLine(String line) {
  line.trim();
  line.toLowerCase();
  if (line.length() == 0) {
    return;
  }

  if (line == "status") {
    printStatus();
    return;
  }

  if (line.startsWith("set ")) {
    float v = line.substring(4).toFloat();
    gState.setpointC.store(clampf(v, 15.0f, 40.0f));
    Serial.printf("setpoint -> %.2f C\n", gState.setpointC.load());
    return;
  }

  if (line.startsWith("mode ")) {
    String m = line.substring(5);
    m.trim();
    if (m == "auto") {
      gState.mode.store(static_cast<uint8_t>(ThermoMode::Auto));
    } else if (m == "manual") {
      gState.mode.store(static_cast<uint8_t>(ThermoMode::Manual));
    } else {
      Serial.println("usage: mode auto|manual");
      return;
    }
    Serial.printf("mode -> %s\n", m.c_str());
    return;
  }

  if (line.startsWith("duty ")) {
    int d = line.substring(5).toInt();
    if (d < 0) d = 0;
    if (d > 100) d = 100;
    gState.fanDuty.store(static_cast<uint8_t>(d));
    Serial.printf("duty -> %u%% (applied in manual)\n", gState.fanDuty.load());
    return;
  }

  if (line.startsWith("beta ")) {
    float b = line.substring(5).toFloat();
    if (b > 1000.0f && b < 6000.0f) {
      gState.ntcBeta.store(b);
      Serial.printf("beta -> %.0f\n", b);
    } else {
      Serial.println("beta out of range (1000-6000)");
    }
    return;
  }

  Serial.println("commands: status | set <C> | mode auto|manual | duty <0-100> | beta <n>");
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("taarm-thermo ESP32-C3 starting...");

  analogReadResolution(12);
  pinMode(kPinNtc, INPUT);

  ledcAttach(kPinPwm, kPwmFreqHz, kPwmResolution);
  applyFanDuty(0);

  pinMode(kPinTach, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(kPinTach), onTach, FALLING);

  bleBegin();
  Serial.printf("BLE advertising as %s\n", BleIds::kDeviceName);
  printStatus();
}

void loop() {
  static uint32_t lastSensorMs = 0;
  static uint32_t lastRpmMs = 0;
  static uint32_t lastNotifyMs = 0;

  uint32_t now = millis();

  while (Serial.available()) {
    char c = static_cast<char>(Serial.read());
    if (c == '\n' || c == '\r') {
      if (serialLine.length() > 0) {
        handleSerialLine(serialLine);
        serialLine = "";
      }
    } else if (serialLine.length() < 80) {
      serialLine += c;
    }
  }

  if (now - lastSensorMs >= 200) {
    lastSensorMs = now;
    float t = readNtcCelsius();
    gState.temperatureC.store(t);

    uint8_t duty;
    if (gState.mode.load() == static_cast<uint8_t>(ThermoMode::Manual)) {
      duty = gState.fanDuty.load();
    } else {
      duty = computeAutoDuty(t, gState.setpointC.load());
      gState.fanDuty.store(duty);
    }
    applyFanDuty(duty);
  }

  if (now - lastRpmMs >= 1000) {
    updateRpm(now - lastRpmMs);
    lastRpmMs = now;
    printStatus();
  }

  if (now - lastNotifyMs >= 1000) {
    lastNotifyMs = now;
    bleNotifyStatus();
  }
}
