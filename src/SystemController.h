#pragma once

#include <Arduino.h>

#include "ClockManager.h"
#include "MotorDriver.h"
#include "PersistentStore.h"
#include "Sensors.h"
#include "Types.h"

class SystemController {
 public:
  SystemController(ClockManager& clock, CurrentSensor& current, VoltageSensor& voltage,
                   MotorDriver& motor, PersistentStore& store);

  void begin();
  void update(uint32_t nowMs, uint32_t nowMicros);
  void processSchedule();

  StartResult startByMass(FeedSource source, uint16_t amountGrams, uint8_t scheduleIndex = 0xFFU);
  StartResult startTimed(FeedSource source, uint32_t durationMs);
  bool stopByUser();
  bool resetError();
  bool runAcsCheck(bool saveZero);
  void onConfigChanged();

  SystemState state() const { return state_; }
  FaultCode fault() const { return fault_; }
  FeedSource source() const { return source_; }
  bool motorOn() const { return motor_.isOn(); }
  bool isFeeding() const { return state_ == SystemState::FEEDING; }
  uint32_t remainingMs(uint32_t nowMs) const;
  uint32_t elapsedMs(uint32_t nowMs) const;
  uint16_t peakCurrentMa() const { return peakCurrentMa_; }
  uint16_t plannedGrams() const { return plannedGrams_; }
  uint32_t nextFeedTimestamp() const;
  int8_t nextFeedIndex() const;
  AutonomousStatus autonomousStatus() const;
  uint8_t lastScheduleIndex() const { return lastScheduleIndex_; }
  StartResult lastScheduleResult() const { return lastScheduleResult_; }
  uint32_t lastScheduleAttemptTimestamp() const { return lastScheduleAttemptTimestamp_; }
  uint32_t stateGeneration() const { return stateGeneration_; }
  uint32_t eventGeneration() const { return eventGeneration_; }
  const EventRecord& lastEvent() const { return lastEvent_; }

  static const __FlashStringHelper* stateName(SystemState state);
  static const __FlashStringHelper* faultName(FaultCode fault);
  static const __FlashStringHelper* startResultName(StartResult result);

 private:
  ClockManager& clock_;
  CurrentSensor& current_;
  VoltageSensor& voltage_;
  MotorDriver& motor_;
  PersistentStore& store_;

  SystemState state_ = SystemState::BOOT;
  FaultCode fault_ = FaultCode::NONE;
  FeedSource source_ = FeedSource::NONE;
  uint32_t startedMs_ = 0;
  uint32_t targetDurationMs_ = 0;
  uint32_t overcurrentStartedMs_ = 0;
  bool overcurrentActive_ = false;
  uint16_t peakCurrentMa_ = 0;
  uint16_t tripCurrentMa_ = 0;
  uint16_t plannedGrams_ = 0;
  uint8_t scheduleIndex_ = 0xFFU;
  uint16_t operationDayKey_ = 0xFFFFU;
  uint32_t stateGeneration_ = 0;
  uint32_t eventGeneration_ = 0;
  EventRecord lastEvent_{};
  uint8_t lastScheduleIndex_ = 0xFFU;
  StartResult lastScheduleResult_ = StartResult::OK;
  uint32_t lastScheduleAttemptTimestamp_ = 0;

  void setState(SystemState value);
  void setIdleState();
  StartResult start(FeedSource source, uint32_t durationMs, uint16_t amountGrams,
                    uint8_t scheduleIndex);
  void finish(bool success, bool userStopped, FaultCode fault);
  void latchStandaloneFault(FaultCode fault);
  void recordMissedFeed(uint8_t index, StartResult reason);
  uint16_t estimateGrams(uint32_t durationMs) const;
  bool feedDurationValid(uint16_t amountGrams) const;
};
