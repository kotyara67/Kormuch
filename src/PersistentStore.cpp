#include "PersistentStore.h"

#include <EEPROM.h>
#include <stddef.h>
#include <string.h>

#include "BoardConfig.h"
#include "Crc.h"

namespace {
constexpr uint16_t CONFIG_MAGIC = 0x5046;
constexpr uint8_t CONFIG_VERSION = 1;
constexpr uint16_t STATS_MAGIC = 0x5354;
constexpr uint16_t TRACE_MAGIC = 0x5452;
constexpr uint16_t RUNTIME_MAGIC = 0x5254;

constexpr int CONFIG_A_ADDRESS = 0;
constexpr int CONFIG_B_ADDRESS = 96;
constexpr int DAILY_ADDRESS = 192;
constexpr uint8_t DAILY_SLOTS = 31;
constexpr int STATS_ADDRESS = DAILY_ADDRESS + DAILY_SLOTS * sizeof(DailyRecord); // 440
constexpr uint8_t STATS_SLOTS = 6;
constexpr int EVENT_ADDRESS = STATS_ADDRESS + STATS_SLOTS * sizeof(StatisticsRecord); // 608
constexpr uint8_t EVENT_SLOTS = 10;
constexpr int TRACE_ADDRESS = EVENT_ADDRESS + EVENT_SLOTS * sizeof(EventRecord); // 868
constexpr int RUNTIME_ADDRESS = TRACE_ADDRESS + sizeof(TraceRecord); // 932

template <typename T>
void eepromWriteVerified(int address, const T& value) {
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&value);
  for (size_t i = 0; i < sizeof(T); ++i) EEPROM.update(address + static_cast<int>(i), bytes[i]);
}

template <typename T>
T eepromRead(int address) {
  T value;
  EEPROM.get(address, value);
  return value;
}

uint16_t saturatingAdd16(uint16_t left, uint16_t right) {
  const uint32_t sum = static_cast<uint32_t>(left) + right;
  return sum > 65535UL ? 65535U : static_cast<uint16_t>(sum);
}
}

ConfigRecord PersistentStore::defaultConfig() {
  ConfigRecord value{};
  value.magic = CONFIG_MAGIC;
  value.version = CONFIG_VERSION;
  value.size = sizeof(ConfigRecord);
  value.revision = 1;
  value.flags = 0x01U;
  value.feedCount = 2;
  value.currentLimitMa = BoardConfig::DEFAULT_CURRENT_LIMIT_MA;
  value.currentDelayMs = BoardConfig::DEFAULT_CURRENT_DELAY_MS;
  value.feedRateMgPerSec = 0; // Calibration is intentionally required.
  value.acsZeroAdc = BoardConfig::ACS_DEFAULT_ZERO_ADC;
  value.lowBatteryWarningMv = 0;
  value.criticalBatteryMv = 0;
  value.maxMotorRunMs = BoardConfig::DEFAULT_MAX_MOTOR_MS;
  value.voltageScaleQ16 = 0;
  value.feeds[0] = {480, 1000};
  value.feeds[1] = {1140, 1000};
  for (uint8_t i = 2; i < MAX_FEEDS; ++i) value.feeds[i] = {0, 1000};
  value.crc = crc16Ccitt(reinterpret_cast<const uint8_t*>(&value), offsetof(ConfigRecord, crc));
  return value;
}

StatisticsRecord PersistentStore::defaultStatistics() {
  StatisticsRecord value{};
  value.magic = STATS_MAGIC;
  value.sequence = 1;
  value.crc = crc16Ccitt(reinterpret_cast<const uint8_t*>(&value), offsetof(StatisticsRecord, crc));
  return value;
}

bool PersistentStore::newer16(uint16_t left, uint16_t right) {
  return static_cast<int16_t>(left - right) > 0;
}

bool PersistentStore::newer32(uint32_t left, uint32_t right) {
  return static_cast<int32_t>(left - right) > 0;
}

bool PersistentStore::configValid(const ConfigRecord& value) const {
  if (value.magic != CONFIG_MAGIC || value.version != CONFIG_VERSION || value.size != sizeof(ConfigRecord)) return false;
  const uint16_t expected = crc16Ccitt(reinterpret_cast<const uint8_t*>(&value), offsetof(ConfigRecord, crc));
  return expected == value.crc && validateConfig(value);
}

bool PersistentStore::validateConfig(const ConfigRecord& value) const {
  if (value.feedCount < 1U || value.feedCount > MAX_FEEDS) return false;
  if (value.currentLimitMa < 500U || value.currentLimitMa > 30000U) return false;
  if (value.currentDelayMs < 20U || value.currentDelayMs > 5000U) return false;
  if (value.feedRateMgPerSec > 10000000UL) return false;
  if (value.acsZeroAdc < BoardConfig::ACS_ABSOLUTE_MIN_ADC || value.acsZeroAdc > BoardConfig::ACS_ABSOLUTE_MAX_ADC) return false;
  if (value.maxMotorRunMs < 1000UL || value.maxMotorRunMs > 600000UL) return false;
  uint16_t previous = 0;
  for (uint8_t i = 0; i < value.feedCount; ++i) {
    if (value.feeds[i].minuteOfDay >= 1440U) return false;
    if (value.feeds[i].amountGrams < 10U || value.feeds[i].amountGrams > 60000U) return false;
    if (i > 0U && value.feeds[i].minuteOfDay <= previous) return false;
    previous = value.feeds[i].minuteOfDay;
  }
  return true;
}

bool PersistentStore::statisticsValid(const StatisticsRecord& value) const {
  if (value.magic != STATS_MAGIC) return false;
  return value.crc == crc16Ccitt(reinterpret_cast<const uint8_t*>(&value), offsetof(StatisticsRecord, crc));
}

bool PersistentStore::dailyValid(const DailyRecord& value) const {
  return value.crc == crc16Ccitt(reinterpret_cast<const uint8_t*>(&value), offsetof(DailyRecord, crc));
}

bool PersistentStore::eventValid(const EventRecord& value) const {
  if (value.id == 0U || value.id == 0xFFFFU) return false;
  return value.crc == crc16Ccitt(reinterpret_cast<const uint8_t*>(&value), offsetof(EventRecord, crc));
}

bool PersistentStore::runtimeValid(const RuntimeRecord& value) const {
  if (value.magic != RUNTIME_MAGIC) return false;
  return value.crc == crc8Dallas(reinterpret_cast<const uint8_t*>(&value), offsetof(RuntimeRecord, crc));
}

void PersistentStore::begin() {
  const ConfigRecord a = eepromRead<ConfigRecord>(CONFIG_A_ADDRESS);
  const ConfigRecord b = eepromRead<ConfigRecord>(CONFIG_B_ADDRESS);
  const bool aValid = configValid(a);
  const bool bValid = configValid(b);
  if (aValid && bValid) {
    if (newer32(b.revision, a.revision)) {
      config_ = b;
      activeConfigBank_ = 1;
    } else {
      config_ = a;
      activeConfigBank_ = 0;
    }
  } else if (aValid) {
    config_ = a;
    activeConfigBank_ = 0;
  } else if (bValid) {
    config_ = b;
    activeConfigBank_ = 1;
  } else {
    config_ = defaultConfig();
    activeConfigBank_ = 0;
    eepromWriteVerified(CONFIG_A_ADDRESS, config_);
  }

  bool foundStats = false;
  for (uint8_t slot = 0; slot < STATS_SLOTS; ++slot) {
    const StatisticsRecord value = eepromRead<StatisticsRecord>(STATS_ADDRESS + slot * sizeof(StatisticsRecord));
    if (statisticsValid(value) && (!foundStats || newer16(value.sequence, statistics_.sequence))) {
      statistics_ = value;
      statisticsSlot_ = slot;
      foundStats = true;
    }
  }
  if (!foundStats) {
    statistics_ = defaultStatistics();
    statisticsSlot_ = 0;
    eepromWriteVerified(STATS_ADDRESS, statistics_);
  }

  eventCount_ = 0;
  latestEventId_ = 0;
  for (uint8_t slot = 0; slot < EVENT_SLOTS; ++slot) {
    const EventRecord value = eepromRead<EventRecord>(EVENT_ADDRESS + slot * sizeof(EventRecord));
    if (!eventValid(value)) continue;
    ++eventCount_;
    if (latestEventId_ == 0U || newer16(value.id, latestEventId_)) latestEventId_ = value.id;
  }

  runtime_ = eepromRead<RuntimeRecord>(RUNTIME_ADDRESS);
  if (!runtimeValid(runtime_)) {
    memset(&runtime_, 0, sizeof(runtime_));
    runtime_.magic = RUNTIME_MAGIC;
    runtime_.lastScheduleDay = 0xFFFFU;
    runtime_.lastScheduleIndex = 0xFFU;
    runtime_.crc = crc8Dallas(reinterpret_cast<const uint8_t*>(&runtime_), offsetof(RuntimeRecord, crc));
    eepromWriteVerified(RUNTIME_ADDRESS, runtime_);
  }
}

bool PersistentStore::saveConfig(const ConfigRecord& candidate) {
  ConfigRecord next = candidate;
  next.magic = CONFIG_MAGIC;
  next.version = CONFIG_VERSION;
  next.size = sizeof(ConfigRecord);
  next.revision = config_.revision + 1UL;
  if (!validateConfig(next)) return false;
  next.crc = crc16Ccitt(reinterpret_cast<const uint8_t*>(&next), offsetof(ConfigRecord, crc));

  const uint8_t nextBank = activeConfigBank_ == 0U ? 1U : 0U;
  const int address = nextBank == 0U ? CONFIG_A_ADDRESS : CONFIG_B_ADDRESS;
  eepromWriteVerified(address, next);
  const ConfigRecord verify = eepromRead<ConfigRecord>(address);
  if (!configValid(verify) || verify.revision != next.revision) return false;
  config_ = verify;
  activeConfigBank_ = nextBank;
  return true;
}

void PersistentStore::saveRuntime(const RuntimeRecord& value) {
  runtime_ = value;
  runtime_.magic = RUNTIME_MAGIC;
  runtime_.crc = crc8Dallas(reinterpret_cast<const uint8_t*>(&runtime_), offsetof(RuntimeRecord, crc));
  eepromWriteVerified(RUNTIME_ADDRESS, runtime_);
}

void PersistentStore::clearFaultAndOperation() {
  runtime_.operationActive = 0;
  runtime_.fault = static_cast<uint8_t>(FaultCode::NONE);
  saveRuntime(runtime_);
}

void PersistentStore::markScheduleToken(uint16_t dayKey, uint8_t index,
                                        uint32_t scheduledTimestamp) {
  runtime_.lastScheduleDay = dayKey;
  runtime_.lastScheduleIndex = index;
  runtime_.lastScheduleTimestamp = scheduledTimestamp;
  saveRuntime(runtime_);
}

void PersistentStore::saveStatistics() {
  statistics_.sequence = static_cast<uint16_t>(statistics_.sequence + 1U);
  statistics_.crc = crc16Ccitt(reinterpret_cast<const uint8_t*>(&statistics_), offsetof(StatisticsRecord, crc));
  statisticsSlot_ = static_cast<uint8_t>((statisticsSlot_ + 1U) % STATS_SLOTS);
  eepromWriteVerified(STATS_ADDRESS + statisticsSlot_ * sizeof(StatisticsRecord), statistics_);
}

void PersistentStore::updateDaily(uint16_t dayKey, bool successful, bool fault, uint16_t grams) {
  if (dayKey == 0xFFFFU) return;
  const uint8_t slot = static_cast<uint8_t>(dayKey % DAILY_SLOTS);
  DailyRecord value = eepromRead<DailyRecord>(DAILY_ADDRESS + slot * sizeof(DailyRecord));
  if (!dailyValid(value) || value.dayKey != dayKey) {
    memset(&value, 0, sizeof(value));
    value.dayKey = dayKey;
  }
  if (successful && value.successful < 255U) ++value.successful;
  if (fault && value.faults < 255U) ++value.faults;
  value.estimatedGrams = saturatingAdd16(value.estimatedGrams, grams);
  value.crc = crc16Ccitt(reinterpret_cast<const uint8_t*>(&value), offsetof(DailyRecord, crc));
  eepromWriteVerified(DAILY_ADDRESS + slot * sizeof(DailyRecord), value);
}

void PersistentStore::recordOperation(bool successful, FaultCode fault, uint16_t grams, uint16_t dayKey) {
  ++statistics_.totalAttempts;
  if (successful) ++statistics_.totalSuccessful;
  if (fault != FaultCode::NONE) {
    ++statistics_.totalFaults;
    if (fault == FaultCode::OVERCURRENT) ++statistics_.overcurrentFaults;
    if (fault == FaultCode::ACS_INVALID) ++statistics_.acsFaults;
  }
  statistics_.totalEstimatedGrams += grams;
  saveStatistics();
  updateDaily(dayKey, successful, fault != FaultCode::NONE, grams);
}

void PersistentStore::recordFaultOnly(FaultCode fault, uint16_t dayKey) {
  ++statistics_.totalFaults;
  if (fault == FaultCode::OVERCURRENT) ++statistics_.overcurrentFaults;
  if (fault == FaultCode::ACS_INVALID) ++statistics_.acsFaults;
  saveStatistics();
  updateDaily(dayKey, false, true, 0);
}

void PersistentStore::aggregateDays(uint16_t todayKey, uint8_t days, uint16_t& successful,
                                    uint16_t& faults, uint32_t& grams) const {
  successful = 0;
  faults = 0;
  grams = 0;
  if (todayKey == 0xFFFFU) return;
  if (days > DAILY_SLOTS) days = DAILY_SLOTS;
  for (uint8_t age = 0; age < days; ++age) {
    const uint16_t key = static_cast<uint16_t>(todayKey - age);
    const uint8_t slot = static_cast<uint8_t>(key % DAILY_SLOTS);
    const DailyRecord value = eepromRead<DailyRecord>(DAILY_ADDRESS + slot * sizeof(DailyRecord));
    if (!dailyValid(value) || value.dayKey != key) continue;
    successful = saturatingAdd16(successful, value.successful);
    faults = saturatingAdd16(faults, value.faults);
    grams += value.estimatedGrams;
  }
}

uint16_t PersistentStore::appendEvent(EventRecord& event) {
  event.id = static_cast<uint16_t>(latestEventId_ + 1U);
  if (event.id == 0U || event.id == 0xFFFFU) event.id = 1U;
  event.crc = crc16Ccitt(reinterpret_cast<const uint8_t*>(&event), offsetof(EventRecord, crc));
  const uint8_t slot = static_cast<uint8_t>(event.id % EVENT_SLOTS);
  eepromWriteVerified(EVENT_ADDRESS + slot * sizeof(EventRecord), event);
  latestEventId_ = event.id;
  if (eventCount_ < EVENT_SLOTS) ++eventCount_;
  return event.id;
}

bool PersistentStore::readEventNewest(uint8_t newestIndex, EventRecord& event) const {
  if (newestIndex >= eventCount_) return false;
  const uint16_t target = static_cast<uint16_t>(latestEventId_ - newestIndex);
  const uint8_t slot = static_cast<uint8_t>(target % EVENT_SLOTS);
  event = eepromRead<EventRecord>(EVENT_ADDRESS + slot * sizeof(EventRecord));
  return eventValid(event) && event.id == target;
}

void PersistentStore::saveTrace(uint16_t eventId, const uint8_t* samples, uint8_t count,
                                uint16_t peakCurrentMa) {
  TraceRecord trace{};
  trace.magic = TRACE_MAGIC;
  trace.eventId = eventId;
  trace.count = count > 50U ? 50U : count;
  trace.samplePeriod10Ms = BoardConfig::TRACE_SAMPLE_PERIOD_MS / 10U;
  for (uint8_t i = 0; i < trace.count; ++i) trace.samples[i] = samples[i];
  trace.peakCurrentMa = peakCurrentMa;
  trace.crc = crc16Ccitt(reinterpret_cast<const uint8_t*>(&trace), offsetof(TraceRecord, crc));
  eepromWriteVerified(TRACE_ADDRESS, trace);
}

bool PersistentStore::readTrace(TraceRecord& trace) const {
  trace = eepromRead<TraceRecord>(TRACE_ADDRESS);
  if (trace.magic != TRACE_MAGIC || trace.count > 50U) return false;
  return trace.crc == crc16Ccitt(reinterpret_cast<const uint8_t*>(&trace), offsetof(TraceRecord, crc));
}
