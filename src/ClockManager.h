#pragma once

#include <Arduino.h>
#include "Types.h"

class ClockManager {
 public:
  void begin();
  bool update(uint32_t nowMs);
  bool readNow();
  bool set(const DateTime& value);

  bool valid() const { return valid_; }
  const DateTime& now() const { return now_; }
  uint32_t timestamp() const;
  uint16_t dayKey() const;
  uint16_t minuteOfDay() const { return static_cast<uint16_t>(now_.hour) * 60U + now_.minute; }
  uint32_t nextTimestamp(uint16_t minuteOfDay) const;

  static bool isValid(const DateTime& value);
  static uint32_t toTimestamp(const DateTime& value);
  static uint16_t toDayKey(const DateTime& value);

 private:
  DateTime now_{};
  bool valid_ = false;
  uint32_t lastReadMs_ = 0;

  static uint8_t bcdToDec(uint8_t value);
  static uint8_t decToBcd(uint8_t value);
  static bool leapYear(uint16_t year);
  static uint8_t daysInMonth(uint16_t year, uint8_t month);

  void startTransfer();
  void endTransfer();
  void writeByte(uint8_t value);
  uint8_t readByte();
  uint8_t readRegister(uint8_t command);
  void writeRegister(uint8_t command, uint8_t value);
};

