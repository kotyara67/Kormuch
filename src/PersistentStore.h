#pragma once

#include <Arduino.h>
#include "Types.h"

class PersistentStore {
 public:
  void begin();

  const ConfigRecord& config() const { return config_; }
  const StatisticsRecord& statistics() const { return statistics_; }
  const RuntimeRecord& runtime() const { return runtime_; }

  bool saveConfig(const ConfigRecord& candidate);
  bool validateConfig(const ConfigRecord& candidate) const;

  void saveRuntime(const RuntimeRecord& value);
  void clearFaultAndOperation();
  void markScheduleToken(uint16_t dayKey, uint8_t index, uint32_t scheduledTimestamp);

  void recordOperation(bool successful, FaultCode fault, uint16_t estimatedGrams, uint16_t dayKey);
  void recordFaultOnly(FaultCode fault, uint16_t dayKey);
  void aggregateDays(uint16_t todayKey, uint8_t days, uint16_t& successful,
                     uint16_t& faults, uint32_t& grams) const;

  uint16_t appendEvent(EventRecord& event);
  uint8_t eventCount() const { return eventCount_; }
  bool readEventNewest(uint8_t newestIndex, EventRecord& event) const;

  void saveTrace(uint16_t eventId, const uint8_t* samples, uint8_t count,
                 uint16_t peakCurrentMa);
  bool readTrace(TraceRecord& trace) const;

 private:
  ConfigRecord config_{};
  StatisticsRecord statistics_{};
  RuntimeRecord runtime_{};
  uint8_t activeConfigBank_ = 0;
  uint8_t statisticsSlot_ = 0;
  uint16_t latestEventId_ = 0;
  uint8_t eventCount_ = 0;

  static ConfigRecord defaultConfig();
  static StatisticsRecord defaultStatistics();
  static bool newer16(uint16_t left, uint16_t right);
  static bool newer32(uint32_t left, uint32_t right);

  bool configValid(const ConfigRecord& value) const;
  bool statisticsValid(const StatisticsRecord& value) const;
  bool dailyValid(const DailyRecord& value) const;
  bool eventValid(const EventRecord& value) const;
  bool runtimeValid(const RuntimeRecord& value) const;
  void saveStatistics();
  void updateDaily(uint16_t dayKey, bool successful, bool fault, uint16_t grams);
};
