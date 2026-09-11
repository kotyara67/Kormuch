#include "SystemController.h"

#include <string.h>

#include "BoardConfig.h"
#include "SchedulePolicy.h"

SystemController::SystemController(ClockManager& clock, CurrentSensor& current,
                                   VoltageSensor& voltage, MotorDriver& motor,
                                   PersistentStore& store)
    : clock_(clock), current_(current), voltage_(voltage), motor_(motor), store_(store) {}

void SystemController::setState(SystemState value) {
  if (state_ == value) return;
  state_ = value;
  ++stateGeneration_;
}

void SystemController::setIdleState() {
  switch (autonomousStatus()) {
    case AutonomousStatus::READY:
      setState(SystemState::READY);
      break;
    case AutonomousStatus::DISABLED:
      setState(SystemState::STOPPED);
      break;
    case AutonomousStatus::RTC_INVALID:
      setState(SystemState::RTC_ERROR);
      break;
    case AutonomousStatus::FAULT_LATCHED:
      setState(SystemState::ERROR);
      break;
    default:
      setState(SystemState::CONFIG_ERROR);
      break;
  }
}

bool SystemController::feedDurationValid(uint16_t amountGrams) const {
  const uint32_t rate = store_.config().feedRateMgPerSec;
  if (rate == 0UL) return false;
  const uint64_t duration =
      (static_cast<uint64_t>(amountGrams) * 1000000ULL + rate / 2UL) / rate;
  return duration >= 100ULL && duration <= store_.config().maxMotorRunMs;
}

AutonomousStatus SystemController::autonomousStatus() const {
  const ConfigRecord& config = store_.config();
  if (!scheduleEnabled(config)) return AutonomousStatus::DISABLED;
  if (!clock_.valid()) return AutonomousStatus::RTC_INVALID;
  if (fault_ != FaultCode::NONE) return AutonomousStatus::FAULT_LATCHED;
  if (config.feedRateMgPerSec == 0UL) return AutonomousStatus::NOT_CALIBRATED;
  for (uint8_t index = 0; index < config.feedCount; ++index) {
    if (!feedDurationValid(config.feeds[index].amountGrams)) {
      return AutonomousStatus::DURATION_TOO_LONG;
    }
  }
  return AutonomousStatus::READY;
}

void SystemController::begin() {
  motor_.off();
  setState(SystemState::SELF_TEST);

  RuntimeRecord runtime = store_.runtime();
  if (runtime.operationActive != 0U) {
    runtime.operationActive = 0;
    runtime.fault = static_cast<uint8_t>(FaultCode::POWER_LOSS);
    store_.saveRuntime(runtime);
    fault_ = FaultCode::POWER_LOSS;

    EventRecord event{};
    event.timestamp = clock_.timestamp();
    event.type = static_cast<uint8_t>(EventType::FAULT);
    event.reason = static_cast<uint8_t>(FaultCode::POWER_LOSS);
    event.source = runtime.source;
    event.scheduleIndex = runtime.scheduleIndex;
    event.plannedGrams = runtime.plannedGrams;
    event.state = static_cast<uint8_t>(SystemState::ERROR);
    store_.appendEvent(event);
    lastEvent_ = event;
    ++eventGeneration_;
    store_.recordOperation(false, FaultCode::POWER_LOSS, 0, clock_.dayKey());
  } else {
    fault_ = static_cast<FaultCode>(runtime.fault);
  }

  const bool acsOk = current_.diagnose(true, clock_.timestamp());
  if (!acsOk && fault_ == FaultCode::NONE) latchStandaloneFault(FaultCode::ACS_INVALID);

  if (fault_ == FaultCode::OVERCURRENT) setState(SystemState::OVERCURRENT);
  else if (fault_ == FaultCode::ACS_INVALID) setState(SystemState::ACS_ERROR);
  else if (fault_ != FaultCode::NONE) setState(SystemState::ERROR);
  else setIdleState();
}

uint32_t SystemController::elapsedMs(uint32_t nowMs) const {
  return isFeeding() ? static_cast<uint32_t>(nowMs - startedMs_) : 0UL;
}

uint32_t SystemController::remainingMs(uint32_t nowMs) const {
  if (!isFeeding()) return 0;
  const uint32_t elapsed = static_cast<uint32_t>(nowMs - startedMs_);
  return elapsed >= targetDurationMs_ ? 0UL : targetDurationMs_ - elapsed;
}

uint16_t SystemController::estimateGrams(uint32_t durationMs) const {
  const uint32_t rate = store_.config().feedRateMgPerSec;
  if (rate == 0UL) return 0;
  const uint64_t mg = (static_cast<uint64_t>(rate) * durationMs + 500ULL) / 1000ULL;
  const uint64_t grams = (mg + 500ULL) / 1000ULL;
  return grams > 65535ULL ? 65535U : static_cast<uint16_t>(grams);
}

StartResult SystemController::startByMass(FeedSource source, uint16_t amountGrams,
                                          uint8_t scheduleIndex) {
  const uint32_t rate = store_.config().feedRateMgPerSec;
  if (rate == 0UL) return StartResult::NOT_CALIBRATED;
  if (amountGrams < 10U) return StartResult::RANGE_ERROR;
  const uint64_t duration = (static_cast<uint64_t>(amountGrams) * 1000000ULL + rate / 2UL) / rate;
  if (duration == 0ULL || duration > store_.config().maxMotorRunMs) return StartResult::RANGE_ERROR;
  return start(source, static_cast<uint32_t>(duration), amountGrams, scheduleIndex);
}

StartResult SystemController::startTimed(FeedSource source, uint32_t durationMs) {
  return start(source, durationMs, 0, 0xFFU);
}

StartResult SystemController::start(FeedSource source, uint32_t durationMs,
                                    uint16_t amountGrams, uint8_t scheduleIndex) {
  if (isFeeding()) return StartResult::BUSY;
  if (fault_ != FaultCode::NONE) return StartResult::FAULT_LATCHED;
  if (durationMs < 100U || durationMs > store_.config().maxMotorRunMs) return StartResult::RANGE_ERROR;

  if (!current_.diagnose(true, clock_.timestamp())) {
    latchStandaloneFault(FaultCode::ACS_INVALID);
    return StartResult::ACS_INVALID;
  }
  if (voltage_.available() && store_.config().criticalBatteryMv > 0U
      && voltage_.millivolts() < store_.config().criticalBatteryMv) {
    latchStandaloneFault(FaultCode::CRITICAL_BATTERY);
    return StartResult::CRITICAL_BATTERY;
  }

  source_ = source;
  targetDurationMs_ = durationMs;
  plannedGrams_ = amountGrams;
  scheduleIndex_ = scheduleIndex;
  operationDayKey_ = clock_.dayKey();
  peakCurrentMa_ = 0;
  tripCurrentMa_ = 0;
  overcurrentActive_ = false;

  if (source == FeedSource::AUTO && scheduleIndex < store_.config().feedCount) {
    store_.markScheduleToken(
        operationDayKey_, scheduleIndex,
        SchedulePolicy::scheduledTimestamp(
            operationDayKey_, store_.config().feeds[scheduleIndex].minuteOfDay));
  }

  RuntimeRecord runtime = store_.runtime();
  runtime.operationActive = 1;
  runtime.source = static_cast<uint8_t>(source);
  runtime.plannedGrams = amountGrams;
  runtime.scheduleIndex = scheduleIndex;
  runtime.fault = static_cast<uint8_t>(FaultCode::NONE);
  store_.saveRuntime(runtime);

  startedMs_ = millis();
  motor_.on();
  setState(SystemState::FEEDING);
  return StartResult::OK;
}

void SystemController::update(uint32_t nowMs, uint32_t nowMicros) {
  current_.update(nowMicros, nowMs);
  voltage_.update(nowMs);
  if (!isFeeding()) return;

  const uint16_t currentMa = current_.currentMa();
  if (currentMa > peakCurrentMa_) peakCurrentMa_ = currentMa;

  const ConfigRecord& config = store_.config();
  if (currentMa >= config.currentLimitMa) {
    if (!overcurrentActive_) {
      overcurrentActive_ = true;
      overcurrentStartedMs_ = nowMs;
    } else if (static_cast<uint32_t>(nowMs - overcurrentStartedMs_) >= config.currentDelayMs) {
      tripCurrentMa_ = currentMa;
      motor_.off(); // First and unconditional safety action.
      finish(false, false, FaultCode::OVERCURRENT);
      return;
    }
  } else {
    // The delay is valid only while the filtered current is continuously at
    // or above the configured threshold.
    overcurrentActive_ = false;
  }

  const uint32_t elapsed = static_cast<uint32_t>(nowMs - startedMs_);
  if (elapsed >= config.maxMotorRunMs) {
    motor_.off();
    finish(false, false, FaultCode::MAX_RUNTIME);
    return;
  }
  if (voltage_.available() && config.criticalBatteryMv > 0U
      && voltage_.millivolts() < config.criticalBatteryMv && elapsed > 1000UL) {
    motor_.off();
    finish(false, false, FaultCode::CRITICAL_BATTERY);
    return;
  }
  if (elapsed >= targetDurationMs_) {
    motor_.off();
    finish(true, false, FaultCode::NONE);
  }
}

bool SystemController::stopByUser() {
  if (!isFeeding()) return false;
  motor_.off();
  finish(false, true, FaultCode::NONE);
  return true;
}

void SystemController::finish(bool success, bool userStopped, FaultCode fault) {
  motor_.off();
  const uint32_t durationMs = static_cast<uint32_t>(millis() - startedMs_);
  const uint16_t estimated = estimateGrams(durationMs);

  RuntimeRecord runtime = store_.runtime();
  runtime.operationActive = 0;
  runtime.fault = static_cast<uint8_t>(fault);
  store_.saveRuntime(runtime);

  EventRecord event{};
  event.timestamp = clock_.timestamp();
  event.type = static_cast<uint8_t>(fault != FaultCode::NONE ? EventType::FAULT
                             : (userStopped ? EventType::FEED_STOPPED : EventType::FEED_SUCCESS));
  event.reason = static_cast<uint8_t>(fault);
  event.source = static_cast<uint8_t>(source_);
  event.scheduleIndex = scheduleIndex_;
  event.plannedGrams = plannedGrams_;
  event.estimatedGrams = estimated;
  const uint32_t durationSeconds = (durationMs + 500UL) / 1000UL;
  event.durationSeconds = static_cast<uint16_t>(durationSeconds > 65535UL ? 65535UL : durationSeconds);
  event.tripCurrentMa = tripCurrentMa_;
  event.peakCurrentMa = peakCurrentMa_;
  event.voltageMv = voltage_.millivolts();
  event.state = static_cast<uint8_t>(fault == FaultCode::OVERCURRENT ? SystemState::OVERCURRENT
                         : (fault == FaultCode::ACS_INVALID ? SystemState::ACS_ERROR
                         : (fault != FaultCode::NONE ? SystemState::ERROR : SystemState::READY)));
  const uint16_t eventId = store_.appendEvent(event);
  lastEvent_ = event;
  ++eventGeneration_;

  if (fault != FaultCode::NONE) {
    uint8_t trace[BoardConfig::TRACE_SAMPLE_COUNT];
    uint8_t count = 0;
    current_.copyChronologicalTrace(trace, count);
    store_.saveTrace(eventId, trace, count, peakCurrentMa_);
  }

  const bool isRealFeed = source_ == FeedSource::AUTO || source_ == FeedSource::MANUAL;
  if (isRealFeed) store_.recordOperation(success, fault, estimated, operationDayKey_);
  else if (fault != FaultCode::NONE) store_.recordFaultOnly(fault, operationDayKey_);

  fault_ = fault;
  source_ = FeedSource::NONE;
  plannedGrams_ = 0;
  scheduleIndex_ = 0xFFU;
  if (fault == FaultCode::OVERCURRENT) setState(SystemState::OVERCURRENT);
  else if (fault == FaultCode::ACS_INVALID) setState(SystemState::ACS_ERROR);
  else if (fault != FaultCode::NONE) setState(SystemState::ERROR);
  else setIdleState();
}

void SystemController::latchStandaloneFault(FaultCode fault) {
  motor_.off();
  fault_ = fault;
  RuntimeRecord runtime = store_.runtime();
  const bool alreadyLatched = runtime.fault == static_cast<uint8_t>(fault);
  runtime.operationActive = 0;
  runtime.fault = static_cast<uint8_t>(fault);
  store_.saveRuntime(runtime);

  if (!alreadyLatched) {
    EventRecord event{};
    event.timestamp = clock_.timestamp();
    event.type = static_cast<uint8_t>(EventType::FAULT);
    event.reason = static_cast<uint8_t>(fault);
    event.source = static_cast<uint8_t>(FeedSource::NONE);
    event.scheduleIndex = 0xFFU;
    event.tripCurrentMa = current_.currentMa();
    event.peakCurrentMa = current_.currentMa();
    event.voltageMv = voltage_.millivolts();
    event.state = static_cast<uint8_t>(fault == FaultCode::ACS_INVALID ? SystemState::ACS_ERROR : SystemState::ERROR);
    store_.appendEvent(event);
    lastEvent_ = event;
    ++eventGeneration_;
    store_.recordFaultOnly(fault, clock_.dayKey());
  }
  setState(fault == FaultCode::ACS_INVALID ? SystemState::ACS_ERROR : SystemState::ERROR);
}

bool SystemController::resetError() {
  motor_.off();
  if (!current_.diagnose(true, clock_.timestamp())) {
    fault_ = FaultCode::ACS_INVALID;
    RuntimeRecord runtime = store_.runtime();
    runtime.fault = static_cast<uint8_t>(fault_);
    runtime.operationActive = 0;
    store_.saveRuntime(runtime);
    setState(SystemState::ACS_ERROR);
    return false;
  }
  if (voltage_.available() && store_.config().criticalBatteryMv > 0U
      && voltage_.millivolts() < store_.config().criticalBatteryMv) return false;
  fault_ = FaultCode::NONE;
  store_.clearFaultAndOperation();
  setIdleState();
  return true;
}

bool SystemController::runAcsCheck(bool saveZero) {
  if (isFeeding()) return false;
  const bool ok = current_.diagnose(true, clock_.timestamp());
  if (!ok) {
    latchStandaloneFault(FaultCode::ACS_INVALID);
    return false;
  }
  if (saveZero) {
    ConfigRecord candidate = store_.config();
    candidate.acsZeroAdc = current_.zeroAdc();
    if (!store_.saveConfig(candidate)) return false;
  }
  return true;
}

void SystemController::onConfigChanged() {
  if (!isFeeding() && fault_ == FaultCode::NONE) {
    setIdleState();
  }
}

void SystemController::recordMissedFeed(uint8_t index, StartResult reason) {
  EventRecord event{};
  event.timestamp = clock_.timestamp();
  event.type = static_cast<uint8_t>(EventType::MISSED_FEED);
  // For MISSED_FEED, reason stores StartResult rather than FaultCode.
  event.reason = static_cast<uint8_t>(reason);
  event.source = static_cast<uint8_t>(FeedSource::AUTO);
  event.scheduleIndex = index;
  event.plannedGrams = store_.config().feeds[index].amountGrams;
  event.state = static_cast<uint8_t>(state_);
  store_.appendEvent(event);
  lastEvent_ = event;
  ++eventGeneration_;
}

void SystemController::processSchedule() {
  if (!clock_.valid()) {
    if (!isFeeding() && fault_ == FaultCode::NONE) setState(SystemState::RTC_ERROR);
    return;
  }
  if (!isFeeding() && fault_ == FaultCode::NONE && state_ == SystemState::RTC_ERROR) setIdleState();
  if (!scheduleEnabled(store_.config()) || isFeeding()) return;
  const ConfigRecord& config = store_.config();
  const uint16_t day = clock_.dayKey();
  const uint32_t nowTimestamp = clock_.timestamp();
  for (uint8_t index = 0; index < config.feedCount; ++index) {
    if (!SchedulePolicy::isDue(nowTimestamp, day, config.feeds[index].minuteOfDay,
                               store_.runtime().lastScheduleDay,
                               store_.runtime().lastScheduleIndex,
                               store_.runtime().lastScheduleTimestamp, index,
                               BoardConfig::SCHEDULE_GRACE_SECONDS)) continue;

    Serial.print(F("SCHED: due index="));
    Serial.print(index);
    Serial.print(F(" minute="));
    Serial.println(config.feeds[index].minuteOfDay);

    const StartResult result = startByMass(FeedSource::AUTO, config.feeds[index].amountGrams, index);
    lastScheduleIndex_ = index;
    lastScheduleResult_ = result;
    lastScheduleAttemptTimestamp_ = nowTimestamp;
    if (result != StartResult::OK) {
      store_.markScheduleToken(
          day, index,
          SchedulePolicy::scheduledTimestamp(day, config.feeds[index].minuteOfDay));
      recordMissedFeed(index, result);
      Serial.print(F("SCHED: blocked result="));
      Serial.println(static_cast<unsigned>(result));
    } else {
      Serial.println(F("SCHED: motor started"));
    }
    return;
  }
}

int8_t SystemController::nextFeedIndex() const {
  if (!clock_.valid() || !scheduleEnabled(store_.config())) return -1;
  const ConfigRecord& config = store_.config();
  const uint16_t minute = clock_.minuteOfDay();
  const uint16_t day = clock_.dayKey();
  for (uint8_t index = 0; index < config.feedCount; ++index) {
    if (config.feeds[index].minuteOfDay > minute) return index;
    const uint32_t scheduled =
        SchedulePolicy::scheduledTimestamp(day, config.feeds[index].minuteOfDay);
    const bool processed = store_.runtime().lastScheduleDay == day
        && store_.runtime().lastScheduleIndex == index
        && store_.runtime().lastScheduleTimestamp / 60UL == scheduled / 60UL;
    if (config.feeds[index].minuteOfDay == minute && !processed) return index;
  }
  return config.feedCount > 0U ? 0 : -1;
}

uint32_t SystemController::nextFeedTimestamp() const {
  const int8_t index = nextFeedIndex();
  if (index < 0) return 0;
  const uint16_t minute = store_.config().feeds[index].minuteOfDay;
  const uint32_t dayStart = static_cast<uint32_t>(clock_.dayKey()) * 86400UL;
  uint32_t result = dayStart + static_cast<uint32_t>(minute) * 60UL;
  if (result <= clock_.timestamp()) result += 86400UL;
  return result;
}

const __FlashStringHelper* SystemController::stateName(SystemState value) {
  switch (value) {
    case SystemState::BOOT: return F("BOOT");
    case SystemState::SELF_TEST: return F("SELF_TEST");
    case SystemState::READY: return F("READY");
    case SystemState::FEEDING: return F("FEEDING");
    case SystemState::STOPPED: return F("STOPPED");
    case SystemState::OVERCURRENT: return F("OVERCURRENT");
    case SystemState::ACS_ERROR: return F("ACS_ERROR");
    case SystemState::CRITICAL_BATTERY: return F("CRITICAL_BATTERY");
    case SystemState::RTC_ERROR: return F("RTC_ERROR");
    case SystemState::CONFIG_ERROR: return F("CONFIG_ERROR");
    default: return F("ERROR");
  }
}

const __FlashStringHelper* SystemController::faultName(FaultCode value) {
  switch (value) {
    case FaultCode::NONE: return F("NONE");
    case FaultCode::OVERCURRENT: return F("OVERCURRENT");
    case FaultCode::ACS_INVALID: return F("ACS_INVALID");
    case FaultCode::CRITICAL_BATTERY: return F("CRITICAL_BATTERY");
    case FaultCode::POWER_LOSS: return F("POWER_LOSS");
    case FaultCode::MAX_RUNTIME: return F("MAX_RUNTIME");
    default: return F("UNKNOWN");
  }
}

const __FlashStringHelper* SystemController::startResultName(StartResult value) {
  switch (value) {
    case StartResult::OK: return F("OK");
    case StartResult::BUSY: return F("BUSY");
    case StartResult::FAULT_LATCHED: return F("FAULT_LATCHED");
    case StartResult::ACS_INVALID: return F("ACS_INVALID");
    case StartResult::NOT_CALIBRATED: return F("NOT_CALIBRATED");
    case StartResult::RANGE_ERROR: return F("RANGE_ERROR");
    case StartResult::CRITICAL_BATTERY: return F("CRITICAL_BATTERY");
    default: return F("ERROR");
  }
}
