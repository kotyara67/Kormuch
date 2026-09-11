#pragma once

#include <Arduino.h>

constexpr uint8_t MAX_FEEDS = 10;

enum class SystemState : uint8_t {
  BOOT = 0,
  SELF_TEST = 1,
  READY = 2,
  FEEDING = 3,
  STOPPED = 4,
  OVERCURRENT = 5,
  ACS_ERROR = 6,
  CRITICAL_BATTERY = 7,
  RTC_ERROR = 8,
  CONFIG_ERROR = 9,
  ERROR = 10
};

enum class AcsState : uint8_t { UNKNOWN = 0, CHECKING = 1, OK = 2, ERROR = 3 };
enum class FeedSource : uint8_t { NONE = 0, AUTO = 1, MANUAL = 2, TEST = 3, CALIBRATION = 4 };
enum class FaultCode : uint8_t {
  NONE = 0,
  OVERCURRENT = 1,
  ACS_INVALID = 2,
  CRITICAL_BATTERY = 3,
  POWER_LOSS = 4,
  MAX_RUNTIME = 5
};

enum class EventType : uint8_t {
  FEED_SUCCESS = 1,
  FEED_STOPPED = 2,
  FAULT = 3,
  ACS_CHECK = 4,
  CONFIG_CHANGED = 5,
  SYSTEM_RESET = 6,
  MISSED_FEED = 7
};

enum class StartResult : uint8_t {
  OK = 0,
  BUSY = 1,
  FAULT_LATCHED = 2,
  ACS_INVALID = 3,
  NOT_CALIBRATED = 4,
  RANGE_ERROR = 5,
  CRITICAL_BATTERY = 6
};

enum class AutonomousStatus : uint8_t {
  READY = 0,
  DISABLED = 1,
  RTC_INVALID = 2,
  NOT_CALIBRATED = 3,
  FAULT_LATCHED = 4,
  DURATION_TOO_LONG = 5
};

struct DateTime {
  uint16_t year;
  uint8_t month;
  uint8_t day;
  uint8_t hour;
  uint8_t minute;
  uint8_t second;
  uint8_t weekday;
};

#pragma pack(push, 1)

struct FeedEntry {
  uint16_t minuteOfDay;
  uint16_t amountGrams;
};

struct ConfigRecord {
  uint16_t magic;
  uint8_t version;
  uint8_t size;
  uint32_t revision;
  uint8_t flags;
  uint8_t feedCount;
  uint16_t currentLimitMa;
  uint16_t currentDelayMs;
  uint32_t feedRateMgPerSec;
  uint16_t acsZeroAdc;
  uint16_t lowBatteryWarningMv;
  uint16_t criticalBatteryMv;
  uint32_t maxMotorRunMs;
  uint32_t voltageScaleQ16;
  FeedEntry feeds[MAX_FEEDS];
  uint8_t reserved[22];
  uint16_t crc;
};

struct StatisticsRecord {
  uint16_t magic;
  uint16_t sequence;
  uint32_t totalAttempts;
  uint32_t totalSuccessful;
  uint32_t totalFaults;
  uint32_t totalEstimatedGrams;
  uint16_t overcurrentFaults;
  uint16_t acsFaults;
  uint16_t reserved;
  uint16_t crc;
};

struct DailyRecord {
  uint16_t dayKey;
  uint8_t successful;
  uint8_t faults;
  uint16_t estimatedGrams;
  uint16_t crc;
};

struct EventRecord {
  uint16_t id;
  uint32_t timestamp;
  uint8_t type;
  uint8_t reason;
  uint8_t source;
  uint8_t scheduleIndex;
  uint16_t plannedGrams;
  uint16_t estimatedGrams;
  uint16_t durationSeconds;
  uint16_t tripCurrentMa;
  uint16_t peakCurrentMa;
  uint16_t voltageMv;
  uint8_t state;
  uint8_t reserved;
  uint16_t crc;
};

struct TraceRecord {
  uint16_t magic;
  uint16_t eventId;
  uint8_t count;
  uint8_t samplePeriod10Ms;
  uint8_t samples[50];
  uint16_t peakCurrentMa;
  uint8_t reserved[4];
  uint16_t crc;
};

struct RuntimeRecord {
  uint16_t magic;
  uint8_t operationActive;
  uint8_t source;
  // When no operation is active this identifies the exact schedule minute
  // already processed. It replaces the unused operation start timestamp while
  // preserving the EEPROM layout.
  uint32_t lastScheduleTimestamp;
  uint16_t plannedGrams;
  uint8_t scheduleIndex;
  uint8_t fault;
  uint16_t lastScheduleDay;
  uint8_t lastScheduleIndex;
  uint8_t crc;
};

#pragma pack(pop)

static_assert(sizeof(FeedEntry) == 4, "FeedEntry EEPROM layout changed");
static_assert(sizeof(ConfigRecord) == 96, "ConfigRecord EEPROM layout changed");
static_assert(sizeof(StatisticsRecord) == 28, "StatisticsRecord EEPROM layout changed");
static_assert(sizeof(DailyRecord) == 8, "DailyRecord EEPROM layout changed");
static_assert(sizeof(EventRecord) == 26, "EventRecord EEPROM layout changed");
static_assert(sizeof(TraceRecord) == 64, "TraceRecord EEPROM layout changed");
static_assert(sizeof(RuntimeRecord) == 16, "RuntimeRecord EEPROM layout changed");

inline bool scheduleEnabled(const ConfigRecord& config) { return (config.flags & 0x01U) != 0; }
