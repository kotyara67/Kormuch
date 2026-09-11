#pragma once

#include <Arduino.h>

#include "Types.h"

class CurrentSensor {
 public:
  void begin(uint16_t storedZeroAdc);
  void update(uint32_t nowMicros, uint32_t nowMillis);
  bool diagnose(bool motorIsOff, uint32_t checkedTimestamp);

  AcsState state() const { return state_; }
  uint16_t zeroAdc() const { return zeroAdc_; }
  uint16_t rawAdc() const { return rawAdc_; }
  uint16_t currentMa() const { return currentMa_; }
  uint16_t displayCurrentMa() const { return displayCurrentMa_; }
  uint32_t lastCheckTimestamp() const { return lastCheckTimestamp_; }
  uint8_t sampleRange() const { return sampleRange_; }
  uint8_t blockSpread() const { return blockSpread_; }

  void resetTrace();
  void copyChronologicalTrace(uint8_t* destination, uint8_t& count) const;

 private:
  AcsState state_ = AcsState::UNKNOWN;
  uint16_t zeroAdc_ = 512;
  uint16_t rawAdc_ = 512;
  uint16_t currentMa_ = 0;
  uint16_t displayCurrentMa_ = 0;
  int32_t fastFilterQ3_ = 4096;
  int32_t slowFilterQ4_ = 8192;
  uint32_t lastSampleMicros_ = 0;
  uint32_t lastTraceMs_ = 0;
  uint32_t lastCheckTimestamp_ = 0;
  uint8_t sampleRange_ = 0;
  uint8_t blockSpread_ = 0;
  uint8_t trace_[50]{};
  uint8_t traceHead_ = 0;
  uint8_t traceCount_ = 0;

  static uint16_t adcDeltaToMa(uint16_t delta);
};

class VoltageSensor {
 public:
  void begin();
  void update(uint32_t nowMs);
  bool available() const;
  uint16_t millivolts() const { return millivolts_; }
  uint16_t vccMillivolts() const { return vccMillivolts_; }

 private:
  uint16_t millivolts_ = 0;
  uint16_t vccMillivolts_ = 5000;
  uint32_t lastReadMs_ = 0;
  uint16_t readVccMillivolts();
};

