#pragma once

#include <Arduino.h>
#include <SoftwareSerial.h>

#include "ClockManager.h"
#include "PersistentStore.h"
#include "Sensors.h"
#include "SystemController.h"

class BluetoothManager {
 public:
  BluetoothManager(SystemController& controller, ClockManager& clock,
                   CurrentSensor& current, VoltageSensor& voltage,
                   PersistentStore& store);

  void begin();
  void updateRx();
  void updateTx();
  bool queueStateEvent();
  bool queueOperationEvent(const EventRecord& event);
  bool txBusy() const { return txLength_ != 0U; }
  bool canQueueEvent() const;

 private:
  enum class ProtocolError : uint8_t {
    BAD_FRAME = 1,
    BAD_CRC = 2,
    UNSUPPORTED_VERSION = 3,
    UNKNOWN_COMMAND = 4,
    INVALID_PARAM = 5,
    RANGE_ERROR = 6,
    BUSY = 7,
    FAULT_LATCHED = 8,
    ACS_INVALID = 9,
    CRITICAL_BATTERY = 10,
    RTC_INVALID = 11,
    NOT_CALIBRATED = 12,
    CONFIG_ERROR = 13,
    NOT_FOUND = 14
  };

  alignas(SoftwareSerial) uint8_t serialStorage_[sizeof(SoftwareSerial)]{};
  SoftwareSerial* serial_ = nullptr;
  SystemController& controller_;
  ClockManager& clock_;
  CurrentSensor& current_;
  VoltageSensor& voltage_;
  PersistentStore& store_;
  char rx_[220]{};
  uint16_t rxLength_ = 0;
  uint32_t lastRxByteMs_ = 0;
  char tx_[240]{};
  uint16_t txLength_ = 0;
  uint16_t txPosition_ = 0;
  uint8_t detectedRxPin_ = 0xFFU;
  uint8_t detectedTxPin_ = 0xFFU;
  bool lastD11Level_ = true;
  bool lastD12Level_ = true;
  uint8_t baudIndex_ = 0;
  bool baudLocked_ = false;
  uint32_t lastBaudSwitchMs_ = 0;
  uint32_t receivedByteCount_ = 0;
  uint16_t validFrameCount_ = 0;
  uint16_t invalidFrameCount_ = 0;
  uint16_t processingRequestCrc_ = 0;
  uint16_t cachedRequestCrc_ = 0;
  uint16_t cachedResponseSequence_ = 0;
  uint16_t cachedResponseLength_ = 0;
  char cachedCommand_[24]{};
  bool transactionActive_ = false;
  ConfigRecord stagedConfig_{};

  void detectSerialPins();
  void startSerial(uint8_t receivePin, uint8_t transmitPin);
  void updateBaudScan(uint32_t nowMs);
  static uint32_t baudForIndex(uint8_t index);
  void handleLine();
  void handleCommand(uint16_t sequence, char* command, char* payload);
  bool queueFrame(uint16_t sequence, char type, const char* command, const char* payload);
  bool queueOk(uint16_t sequence, const char* command, const char* payload = nullptr);
  bool queueError(uint16_t sequence, const char* command, ProtocolError error);

  static bool readUnsigned(const char* payload, PGM_P key, uint32_t& value);
  static char* splitField(char*& cursor, char separator);
  static uint8_t hexNibble(char value);
  static ProtocolError mapStartError(StartResult result);
  bool applyConfig(uint16_t sequence, const char* command, const ConfigRecord& candidate);
  ConfigRecord& editableConfig();
};
