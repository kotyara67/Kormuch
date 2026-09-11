#include <Arduino.h>
#include <avr/wdt.h>

#include "BluetoothManager.h"
#include "BoardConfig.h"
#include "ClockManager.h"
#include "MotorDriver.h"
#include "PersistentStore.h"
#include "Sensors.h"
#include "SystemController.h"

uint8_t gResetCause __attribute__((section(".noinit")));
void captureResetCause() __attribute__((naked, used, externally_visible, section(".init3")));
void captureResetCause() {
  PORTD &= static_cast<uint8_t>(~_BV(PORTD4));
  DDRD |= _BV(DDD4); // Force the active-high relay output LOW before setup().
  gResetCause = MCUSR;
  MCUSR = 0;
  wdt_disable();
}

PersistentStore persistentStore;
ClockManager clockManager;
CurrentSensor currentSensor;
VoltageSensor voltageSensor;
MotorDriver motorDriver;
SystemController systemController(clockManager, currentSensor, voltageSensor, motorDriver, persistentStore);
BluetoothManager bluetoothManager(systemController, clockManager, currentSensor, voltageSensor, persistentStore);

namespace {
uint32_t sentStateGeneration = 0;
uint32_t sentEventGeneration = 0;
uint32_t lastLedMs = 0;
bool ledOn = false;

void updateStatusLed(uint32_t nowMs) {
  if (systemController.isFeeding()) {
    digitalWrite(BoardConfig::STATUS_LED_PIN, HIGH);
    return;
  }
  const bool fault = systemController.fault() != FaultCode::NONE;
  const uint16_t interval = fault ? 250U : 1000U;
  if (static_cast<uint32_t>(nowMs - lastLedMs) >= interval) {
    lastLedMs = nowMs;
    ledOn = !ledOn;
    digitalWrite(BoardConfig::STATUS_LED_PIN, ledOn ? HIGH : LOW);
  }
}
}

void setup() {
  motorDriver.begin(); // D4 LOW is the first normal setup action.
  pinMode(BoardConfig::STATUS_LED_PIN, OUTPUT);
  digitalWrite(BoardConfig::STATUS_LED_PIN, LOW);

  Serial.begin(9600);
  persistentStore.begin();
  clockManager.begin();
  currentSensor.begin(persistentStore.config().acsZeroAdc);
  voltageSensor.begin();
  bluetoothManager.begin();
  systemController.begin();

  Serial.println(F("Pond Feeder firmware 1.0.4"));
  Serial.print(F("Reset cause: "));
  Serial.println(gResetCause, HEX);
  Serial.print(F("Motor D"));
  Serial.print(BoardConfig::MOTOR_PIN);
  Serial.println(F(", Bluetooth auto RX/TX on D11/D12"));

  wdt_enable(WDTO_2S);
}

void loop() {
  wdt_reset();
  const uint32_t nowMs = millis();
  const uint32_t nowMicros = micros();

  // Protection is serviced before and after communication work.
  systemController.update(nowMs, nowMicros);
  bluetoothManager.updateRx();
  systemController.update(millis(), micros());

  if (clockManager.update(nowMs)) systemController.processSchedule();

  if (bluetoothManager.canQueueEvent()) {
    if (sentEventGeneration != systemController.eventGeneration()) {
      if (bluetoothManager.queueOperationEvent(systemController.lastEvent())) {
        sentEventGeneration = systemController.eventGeneration();
      }
    } else if (sentStateGeneration != systemController.stateGeneration()) {
      if (bluetoothManager.queueStateEvent()) sentStateGeneration = systemController.stateGeneration();
    }
  }
  bluetoothManager.updateTx();
  updateStatusLed(nowMs);
}
