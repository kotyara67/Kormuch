#include "ClockManager.h"

#include "BoardConfig.h"

namespace {
constexpr uint8_t CMD_CLOCK_BURST_WRITE = 0xBE;
constexpr uint8_t CMD_CLOCK_BURST_READ = 0xBF;
constexpr uint8_t CMD_WRITE_PROTECT_WRITE = 0x8E;
}

void ClockManager::begin() {
  pinMode(BoardConfig::RTC_RST_PIN, OUTPUT);
  pinMode(BoardConfig::RTC_CLK_PIN, OUTPUT);
  pinMode(BoardConfig::RTC_DAT_PIN, INPUT);
  digitalWrite(BoardConfig::RTC_RST_PIN, LOW);
  digitalWrite(BoardConfig::RTC_CLK_PIN, LOW);
  readNow();
}

bool ClockManager::update(uint32_t nowMs) {
  if (static_cast<uint32_t>(nowMs - lastReadMs_) < 500U) return false;
  lastReadMs_ = nowMs;
  readNow();
  return true;
}

void ClockManager::startTransfer() {
  digitalWrite(BoardConfig::RTC_CLK_PIN, LOW);
  digitalWrite(BoardConfig::RTC_RST_PIN, HIGH);
  delayMicroseconds(1);
}

void ClockManager::endTransfer() {
  digitalWrite(BoardConfig::RTC_RST_PIN, LOW);
  delayMicroseconds(1);
}

void ClockManager::writeByte(uint8_t value) {
  pinMode(BoardConfig::RTC_DAT_PIN, OUTPUT);
  for (uint8_t bit = 0; bit < 8; ++bit) {
    digitalWrite(BoardConfig::RTC_DAT_PIN, (value >> bit) & 0x01U);
    delayMicroseconds(1);
    digitalWrite(BoardConfig::RTC_CLK_PIN, HIGH);
    delayMicroseconds(1);
    digitalWrite(BoardConfig::RTC_CLK_PIN, LOW);
  }
}

uint8_t ClockManager::readByte() {
  uint8_t value = 0;
  pinMode(BoardConfig::RTC_DAT_PIN, INPUT);
  for (uint8_t bit = 0; bit < 8; ++bit) {
    delayMicroseconds(1);
    if (digitalRead(BoardConfig::RTC_DAT_PIN)) value |= static_cast<uint8_t>(1U << bit);
    digitalWrite(BoardConfig::RTC_CLK_PIN, HIGH);
    delayMicroseconds(1);
    digitalWrite(BoardConfig::RTC_CLK_PIN, LOW);
  }
  return value;
}

uint8_t ClockManager::readRegister(uint8_t command) {
  startTransfer();
  writeByte(command | 0x01U);
  const uint8_t value = readByte();
  endTransfer();
  return value;
}

void ClockManager::writeRegister(uint8_t command, uint8_t value) {
  startTransfer();
  writeByte(command & 0xFEU);
  writeByte(value);
  endTransfer();
}

bool ClockManager::readNow() {
  uint8_t raw[8];
  startTransfer();
  writeByte(CMD_CLOCK_BURST_READ);
  for (uint8_t i = 0; i < sizeof(raw); ++i) raw[i] = readByte();
  endTransfer();

  const bool clockHalted = (raw[0] & 0x80U) != 0;
  const bool twelveHourMode = (raw[2] & 0x80U) != 0;
  if (clockHalted || twelveHourMode) {
    valid_ = false;
    return false;
  }

  DateTime candidate{};
  candidate.second = bcdToDec(raw[0] & 0x7FU);
  candidate.minute = bcdToDec(raw[1] & 0x7FU);
  candidate.hour = bcdToDec(raw[2] & 0x3FU);
  candidate.day = bcdToDec(raw[3] & 0x3FU);
  candidate.month = bcdToDec(raw[4] & 0x1FU);
  candidate.weekday = bcdToDec(raw[5] & 0x07U);
  candidate.year = static_cast<uint16_t>(2000U + bcdToDec(raw[6]));

  valid_ = isValid(candidate);
  if (valid_) now_ = candidate;
  return valid_;
}

bool ClockManager::set(const DateTime& value) {
  if (!isValid(value)) return false;

  writeRegister(CMD_WRITE_PROTECT_WRITE, 0x00);
  startTransfer();
  writeByte(CMD_CLOCK_BURST_WRITE);
  writeByte(decToBcd(value.second) & 0x7FU);
  writeByte(decToBcd(value.minute));
  writeByte(decToBcd(value.hour));
  writeByte(decToBcd(value.day));
  writeByte(decToBcd(value.month));
  writeByte(decToBcd(value.weekday));
  writeByte(decToBcd(static_cast<uint8_t>(value.year - 2000U)));
  writeByte(0x00);
  endTransfer();

  now_ = value;
  valid_ = true;
  return readNow();
}

uint8_t ClockManager::bcdToDec(uint8_t value) {
  return static_cast<uint8_t>((value >> 4) * 10U + (value & 0x0FU));
}

uint8_t ClockManager::decToBcd(uint8_t value) {
  return static_cast<uint8_t>(((value / 10U) << 4) | (value % 10U));
}

bool ClockManager::leapYear(uint16_t year) {
  return (year % 4U == 0U && year % 100U != 0U) || (year % 400U == 0U);
}

uint8_t ClockManager::daysInMonth(uint16_t year, uint8_t month) {
  static const uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month == 2U && leapYear(year)) return 29;
  return (month >= 1U && month <= 12U) ? days[month - 1U] : 0;
}

bool ClockManager::isValid(const DateTime& value) {
  if (value.year < 2020U || value.year > 2099U) return false;
  if (value.month < 1U || value.month > 12U) return false;
  if (value.day < 1U || value.day > daysInMonth(value.year, value.month)) return false;
  if (value.hour > 23U || value.minute > 59U || value.second > 59U) return false;
  return value.weekday >= 1U && value.weekday <= 7U;
}

uint16_t ClockManager::toDayKey(const DateTime& value) {
  uint32_t days = 0;
  for (uint16_t year = 2000; year < value.year; ++year) days += leapYear(year) ? 366U : 365U;
  for (uint8_t month = 1; month < value.month; ++month) days += daysInMonth(value.year, month);
  days += static_cast<uint32_t>(value.day - 1U);
  return static_cast<uint16_t>(days);
}

uint32_t ClockManager::toTimestamp(const DateTime& value) {
  return static_cast<uint32_t>(toDayKey(value)) * 86400UL
      + static_cast<uint32_t>(value.hour) * 3600UL
      + static_cast<uint32_t>(value.minute) * 60UL
      + value.second;
}

uint32_t ClockManager::timestamp() const { return valid_ ? toTimestamp(now_) : 0UL; }
uint16_t ClockManager::dayKey() const { return valid_ ? toDayKey(now_) : 0xFFFFU; }

uint32_t ClockManager::nextTimestamp(uint16_t minute) const {
  if (!valid_ || minute >= 1440U) return 0;
  const uint32_t dayStart = static_cast<uint32_t>(dayKey()) * 86400UL;
  uint32_t result = dayStart + static_cast<uint32_t>(minute) * 60UL;
  if (result <= timestamp()) result += 86400UL;
  return result;
}

