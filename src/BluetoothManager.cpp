#include "BluetoothManager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <new>

#include "BoardConfig.h"
#include "Crc.h"

BluetoothManager::BluetoothManager(SystemController& controller, ClockManager& clock,
                                   CurrentSensor& current, VoltageSensor& voltage,
                                   PersistentStore& store)
    : controller_(controller), clock_(clock), current_(current), voltage_(voltage), store_(store) {}

void BluetoothManager::begin() {
  // Leave both UART wires as inputs until activity from the module identifies
  // its TXD wire. This prevents ever driving the JDY-31 TX output by mistake.
  pinMode(BoardConfig::BLUETOOTH_WIRE_D11, INPUT_PULLUP);
  pinMode(BoardConfig::BLUETOOTH_WIRE_D12, INPUT_PULLUP);
  lastD11Level_ = digitalRead(BoardConfig::BLUETOOTH_WIRE_D11) == HIGH;
  lastD12Level_ = digitalRead(BoardConfig::BLUETOOTH_WIRE_D12) == HIGH;
  serial_ = nullptr;
  rxLength_ = 0;
  txLength_ = 0;
  txPosition_ = 0;
  baudLocked_ = false;
  cachedResponseLength_ = 0;
  Serial.println(F("BT: waiting for JDY TX activity on D11/D12"));
}

uint32_t BluetoothManager::baudForIndex(uint8_t index) {
  switch (index) {
    case 0: return 9600UL;
    case 1: return 19200UL;
    case 2: return 38400UL;
    case 3: return 57600UL;
    default: return 115200UL;
  }
}

void BluetoothManager::startSerial(uint8_t receivePin, uint8_t transmitPin) {
  detectedRxPin_ = receivePin;
  detectedTxPin_ = transmitPin;
  serial_ = new (serialStorage_) SoftwareSerial(receivePin, transmitPin);
  baudIndex_ = 0;
  serial_->begin(baudForIndex(baudIndex_));
  lastBaudSwitchMs_ = millis();
  rxLength_ = 0;
  Serial.print(F("BT: detected JDY TXD -> D"));
  Serial.print(receivePin);
  Serial.print(F(", JDY RXD <- D"));
  Serial.println(transmitPin);
  Serial.println(F("BT: scanning baud, start 9600"));
}

void BluetoothManager::detectSerialPins() {
  const bool d11Level = digitalRead(BoardConfig::BLUETOOTH_WIRE_D11) == HIGH;
  const bool d12Level = digitalRead(BoardConfig::BLUETOOTH_WIRE_D12) == HIGH;
  const bool d11Changed = d11Level != lastD11Level_;
  const bool d12Changed = d12Level != lastD12Level_;
  lastD11Level_ = d11Level;
  lastD12Level_ = d12Level;
  if (d11Changed == d12Changed) return;
  if (d11Changed) startSerial(BoardConfig::BLUETOOTH_WIRE_D11, BoardConfig::BLUETOOTH_WIRE_D12);
  else startSerial(BoardConfig::BLUETOOTH_WIRE_D12, BoardConfig::BLUETOOTH_WIRE_D11);
}

void BluetoothManager::updateBaudScan(uint32_t nowMs) {
  if (serial_ == nullptr || baudLocked_
      || static_cast<uint32_t>(nowMs - lastBaudSwitchMs_) < BoardConfig::BLUETOOTH_BAUD_SCAN_MS) {
    return;
  }
  while (serial_->available()) serial_->read();
  serial_->end();
  baudIndex_ = static_cast<uint8_t>((baudIndex_ + 1U) % 5U);
  serial_->begin(baudForIndex(baudIndex_));
  lastBaudSwitchMs_ = nowMs;
  rxLength_ = 0;
  Serial.print(F("BT: scanning baud "));
  Serial.println(baudForIndex(baudIndex_));
}

void BluetoothManager::updateRx() {
  if (serial_ == nullptr) {
    detectSerialPins();
    return;
  }
  const uint32_t nowMs = millis();
  updateBaudScan(nowMs);
  // SoftwareSerial is effectively half-duplex while transmitting. A command
  // is only parsed when the response channel is free, so its response cannot
  // be silently discarded by queueFrame().
  if (txLength_ != 0U) return;
  if (rxLength_ > 0U && static_cast<uint32_t>(nowMs - lastRxByteMs_) > 500UL) rxLength_ = 0;
  uint8_t budget = 24;
  while (budget-- > 0U && serial_->available()) {
    const int input = serial_->read();
    if (input < 0) break;
    ++receivedByteCount_;
    lastRxByteMs_ = millis();
    const char value = static_cast<char>(input);
    if (value == '\r') continue;
    if (value == '\n') {
      if (rxLength_ > 0U) {
        rx_[rxLength_] = '\0';
        handleLine();
      }
      rxLength_ = 0;
      continue;
    }
    if (value == '@') {
      rxLength_ = 0;
      rx_[rxLength_++] = value;
      continue;
    }
    if (rxLength_ == 0U) continue;
    if (rxLength_ < sizeof(rx_) - 1U) rx_[rxLength_++] = value;
    else rxLength_ = 0;
  }
}

bool BluetoothManager::canQueueEvent() const {
  // Android polls state. Unsolicited transmission is deliberately disabled on
  // the half-duplex software UART so it can never collide with a request.
  return false;
}

void BluetoothManager::updateTx() {
  if (serial_ == nullptr || txLength_ == 0U) return;
  uint8_t budget = 2; // At 9600 baud this blocks for about 2 ms at most.
  while (budget-- > 0U && txPosition_ < txLength_) serial_->write(tx_[txPosition_++]);
  if (txPosition_ >= txLength_) {
    txLength_ = 0;
    txPosition_ = 0;
  }
}

uint8_t BluetoothManager::hexNibble(char value) {
  if (value >= '0' && value <= '9') return static_cast<uint8_t>(value - '0');
  if (value >= 'A' && value <= 'F') return static_cast<uint8_t>(value - 'A' + 10);
  if (value >= 'a' && value <= 'f') return static_cast<uint8_t>(value - 'a' + 10);
  return 0xFFU;
}

char* BluetoothManager::splitField(char*& cursor, char separator) {
  if (cursor == nullptr) return nullptr;
  char* result = cursor;
  char* end = strchr(cursor, separator);
  if (end != nullptr) {
    *end = '\0';
    cursor = end + 1;
  } else {
    cursor = nullptr;
  }
  return result;
}

void BluetoothManager::handleLine() {
  if (rx_[0] != '@') {
    ++invalidFrameCount_;
    return;
  }
  char* star = strrchr(rx_, '*');
  if (star == nullptr || strlen(star + 1) != 4U) {
    ++invalidFrameCount_;
    return;
  }
  uint16_t receivedCrc = 0;
  for (uint8_t i = 0; i < 4; ++i) {
    const uint8_t nibble = hexNibble(star[1 + i]);
    if (nibble == 0xFFU) {
      ++invalidFrameCount_;
      return;
    }
    receivedCrc = static_cast<uint16_t>((receivedCrc << 4) | nibble);
  }
  *star = '\0';
  const uint16_t actualCrc = crc16Ccitt(reinterpret_cast<const uint8_t*>(rx_ + 1), strlen(rx_ + 1));
  if (actualCrc != receivedCrc) {
    ++invalidFrameCount_;
    return;
  }

  char* cursor = rx_ + 1;
  char* version = splitField(cursor, '|');
  char* sequenceText = splitField(cursor, '|');
  char* type = splitField(cursor, '|');
  char* command = splitField(cursor, '|');
  char* payload = cursor == nullptr ? const_cast<char*>("") : cursor;
  if (version == nullptr || sequenceText == nullptr || type == nullptr || command == nullptr
      || version[0] != '1' || version[1] != '\0' || strlen(sequenceText) != 4U
      || type[0] != 'Q' || type[1] != '\0') {
    ++invalidFrameCount_;
    return;
  }
  const uint16_t sequence = static_cast<uint16_t>(strtoul(sequenceText, nullptr, 16));
  if (cachedResponseLength_ != 0U && sequence == cachedResponseSequence_
      && receivedCrc == cachedRequestCrc_ && strcmp(command, cachedCommand_) == 0) {
    txLength_ = cachedResponseLength_;
    txPosition_ = 0;
    Serial.print(F("BT: repeat seq="));
    Serial.println(sequence, HEX);
    return;
  }
  baudLocked_ = true;
  processingRequestCrc_ = receivedCrc;
  ++validFrameCount_;
  Serial.print(F("BT: OK baud="));
  Serial.print(baudForIndex(baudIndex_));
  Serial.print(F(" rx=D"));
  Serial.print(detectedRxPin_);
  Serial.print(F(" cmd="));
  Serial.println(command);
  handleCommand(sequence, command, payload);
}

bool BluetoothManager::queueFrame(uint16_t sequence, char type, const char* command,
                                  const char* payload) {
  if (txLength_ != 0U) return false;
  const int bodyLength = snprintf_P(tx_ + 1, sizeof(tx_) - 1U, PSTR("1|%04X|%c|%s|%s"),
                                    sequence, type, command, payload == nullptr ? "" : payload);
  if (bodyLength <= 0 || static_cast<size_t>(bodyLength) + 8U >= sizeof(tx_)) return false;
  tx_[0] = '@';
  const uint16_t crc = crc16Ccitt(reinterpret_cast<const uint8_t*>(tx_ + 1), bodyLength);
  const int suffix = snprintf_P(tx_ + 1 + bodyLength, sizeof(tx_) - 1U - bodyLength,
                                PSTR("*%04X\r\n"), crc);
  if (suffix != 7) return false;
  txLength_ = static_cast<uint16_t>(1 + bodyLength + suffix);
  txPosition_ = 0;
  if (sequence != 0U && (type == 'R' || type == 'E')) {
    cachedRequestCrc_ = processingRequestCrc_;
    cachedResponseSequence_ = sequence;
    cachedResponseLength_ = txLength_;
    strncpy(cachedCommand_, command, sizeof(cachedCommand_) - 1U);
    cachedCommand_[sizeof(cachedCommand_) - 1U] = '\0';
  }
  return true;
}

bool BluetoothManager::queueOk(uint16_t sequence, const char* command, const char* payload) {
  return queueFrame(sequence, 'R', command, payload);
}

bool BluetoothManager::queueError(uint16_t sequence, const char* command, ProtocolError error) {
  char payload[20];
  snprintf_P(payload, sizeof(payload), PSTR("code=%u"), static_cast<unsigned>(error));
  return queueFrame(sequence, 'E', command, payload);
}

bool BluetoothManager::readUnsigned(const char* payload, PGM_P key, uint32_t& value) {
  const size_t keyLength = strlen_P(key);
  const char* cursor = payload;
  while (cursor != nullptr && *cursor != '\0') {
    if (strncmp_P(cursor, key, keyLength) == 0 && cursor[keyLength] == '=') {
      char* end = nullptr;
      const unsigned long parsed = strtoul(cursor + keyLength + 1U, &end, 10);
      if (end == cursor + keyLength + 1U || (*end != ';' && *end != '\0')) return false;
      value = parsed;
      return true;
    }
    cursor = strchr(cursor, ';');
    if (cursor != nullptr) ++cursor;
  }
  return false;
}

BluetoothManager::ProtocolError BluetoothManager::mapStartError(StartResult result) {
  switch (result) {
    case StartResult::BUSY: return ProtocolError::BUSY;
    case StartResult::FAULT_LATCHED: return ProtocolError::FAULT_LATCHED;
    case StartResult::ACS_INVALID: return ProtocolError::ACS_INVALID;
    case StartResult::NOT_CALIBRATED: return ProtocolError::NOT_CALIBRATED;
    case StartResult::CRITICAL_BATTERY: return ProtocolError::CRITICAL_BATTERY;
    default: return ProtocolError::RANGE_ERROR;
  }
}

ConfigRecord& BluetoothManager::editableConfig() {
  if (!transactionActive_) stagedConfig_ = store_.config();
  return stagedConfig_;
}

bool BluetoothManager::applyConfig(uint16_t sequence, const char* command,
                                   const ConfigRecord& candidate) {
  if (transactionActive_) {
    stagedConfig_ = candidate;
    return queueOk(sequence, command);
  }
  if (!store_.saveConfig(candidate)) return queueError(sequence, command, ProtocolError::CONFIG_ERROR);
  controller_.onConfigChanged();
  return queueOk(sequence, command);
}

void BluetoothManager::handleCommand(uint16_t sequence, char* command, char* payload) {
  char output[210];
  if (strcmp_P(command, PSTR("PING")) == 0) {
    strcpy_P(output, PSTR("pong=1"));
    queueOk(sequence, command, output);
  } else if (strcmp_P(command, PSTR("GET_INFO")) == 0) {
    snprintf_P(output, sizeof(output),
             PSTR("device=POND_FEEDER;fw=1.0.4;protocol=1;board=NANO_ATMEGA328P;rx=%u;tx=%u;baud=%lu"),
             detectedRxPin_, detectedTxPin_, static_cast<unsigned long>(baudForIndex(baudIndex_)));
    queueOk(sequence, command, output);
  } else if (strcmp_P(command, PSTR("GET_STATUS")) == 0) {
    snprintf_P(output, sizeof(output),
             PSTR("st=%u;fault=%u;m=%u;i=%u;v=%u;va=%u;acs=%u;z=%u;acst=%lu;rtc=%u;now=%lu;next=%lu;rem=%lu;peak=%u;cal=%u;logs=%u;rev=%lu;aut=%u;lsi=%u;lsr=%u;lst=%lu"),
             static_cast<unsigned>(controller_.state()), static_cast<unsigned>(controller_.fault()),
             controller_.motorOn() ? 1U : 0U, current_.displayCurrentMa(), voltage_.millivolts(),
             voltage_.available() ? 1U : 0U, static_cast<unsigned>(current_.state()), current_.zeroAdc(),
             static_cast<unsigned long>(current_.lastCheckTimestamp()), clock_.valid() ? 1U : 0U,
             static_cast<unsigned long>(clock_.timestamp()),
             static_cast<unsigned long>(controller_.nextFeedTimestamp()),
             static_cast<unsigned long>(controller_.remainingMs(millis())), controller_.peakCurrentMa(),
             store_.config().feedRateMgPerSec > 0UL ? 1U : 0U, store_.eventCount(),
             static_cast<unsigned long>(store_.config().revision),
             static_cast<unsigned>(controller_.autonomousStatus()),
             controller_.lastScheduleIndex(),
             static_cast<unsigned>(controller_.lastScheduleResult()),
             static_cast<unsigned long>(controller_.lastScheduleAttemptTimestamp()));
    queueOk(sequence, command, output);
  } else if (strcmp_P(command, PSTR("GET_CURRENT")) == 0) {
    snprintf_P(output, sizeof(output), PSTR("current_ma=%u;raw_adc=%u;zero_adc=%u;acs=%u"),
             current_.displayCurrentMa(), current_.rawAdc(), current_.zeroAdc(),
             static_cast<unsigned>(current_.state()));
    queueOk(sequence, command, output);
  } else if (strcmp_P(command, PSTR("GET_VOLTAGE")) == 0) {
    snprintf_P(output, sizeof(output), PSTR("voltage_mv=%u;available=%u;vcc_mv=%u"),
             voltage_.millivolts(), voltage_.available() ? 1U : 0U, voltage_.vccMillivolts());
    queueOk(sequence, command, output);
  } else if (strcmp_P(command, PSTR("GET_SETTINGS")) == 0) {
    const ConfigRecord& config = store_.config();
    snprintf_P(output, sizeof(output),
             PSTR("schedule=%u;count=%u;limit_ma=%u;delay_ms=%u;rate_mg_s=%lu;zero_adc=%u;max_motor_ms=%lu;low_mv=%u;critical_mv=%u;voltage_available=%u;revision=%lu"),
             scheduleEnabled(config) ? 1U : 0U, config.feedCount, config.currentLimitMa,
             config.currentDelayMs, static_cast<unsigned long>(config.feedRateMgPerSec),
             config.acsZeroAdc, static_cast<unsigned long>(config.maxMotorRunMs),
             config.lowBatteryWarningMv, config.criticalBatteryMv,
             voltage_.available() ? 1U : 0U, static_cast<unsigned long>(config.revision));
    queueOk(sequence, command, output);
  } else if (strcmp_P(command, PSTR("GET_SCHEDULE")) == 0) {
    const ConfigRecord& config = store_.config();
    int used = snprintf_P(output, sizeof(output), PSTR("count=%u"), config.feedCount);
    for (uint8_t i = 0; i < config.feedCount && used > 0 && used < static_cast<int>(sizeof(output)); ++i) {
      used += snprintf_P(output + used, sizeof(output) - used, PSTR(";e%u=%u,%u"), i,
                       config.feeds[i].minuteOfDay, config.feeds[i].amountGrams);
    }
    queueOk(sequence, command, output);
  } else if (strcmp_P(command, PSTR("GET_STATISTICS")) == 0) {
    uint16_t dayFeeds = 0, dayFaults = 0, weekFeeds = 0, weekFaults = 0, monthFeeds = 0, monthFaults = 0;
    uint32_t dayGrams = 0, weekGrams = 0, monthGrams = 0;
    store_.aggregateDays(clock_.dayKey(), 1, dayFeeds, dayFaults, dayGrams);
    store_.aggregateDays(clock_.dayKey(), 7, weekFeeds, weekFaults, weekGrams);
    store_.aggregateDays(clock_.dayKey(), 31, monthFeeds, monthFaults, monthGrams);
    const StatisticsRecord& stats = store_.statistics();
    snprintf_P(output, sizeof(output),
             PSTR("df=%u;da=%u;dg=%lu;wf=%u;wa=%u;wg=%lu;mf=%u;ma=%u;mg=%lu;ta=%lu;ts=%lu;tf=%lu;tg=%lu;oc=%u;af=%u"),
             dayFeeds, dayFaults, static_cast<unsigned long>(dayGrams), weekFeeds, weekFaults,
             static_cast<unsigned long>(weekGrams), monthFeeds, monthFaults,
             static_cast<unsigned long>(monthGrams), static_cast<unsigned long>(stats.totalAttempts),
             static_cast<unsigned long>(stats.totalSuccessful), static_cast<unsigned long>(stats.totalFaults),
             static_cast<unsigned long>(stats.totalEstimatedGrams), stats.overcurrentFaults, stats.acsFaults);
    queueOk(sequence, command, output);
  } else if (strcmp_P(command, PSTR("GET_LOG")) == 0) {
    uint32_t index = 0;
    if (!readUnsigned(payload, PSTR("index"), index) || index > 9U) {
      queueError(sequence, command, ProtocolError::INVALID_PARAM);
      return;
    }
    EventRecord event{};
    if (!store_.readEventNewest(static_cast<uint8_t>(index), event)) {
      queueError(sequence, command, ProtocolError::NOT_FOUND);
      return;
    }
    snprintf_P(output, sizeof(output),
             PSTR("id=%u;ts=%lu;type=%u;reason=%u;source=%u;si=%u;pg=%u;eg=%u;dur=%u;trip=%u;peak=%u;v=%u;st=%u"),
             event.id, static_cast<unsigned long>(event.timestamp), event.type, event.reason, event.source,
             event.scheduleIndex, event.plannedGrams, event.estimatedGrams, event.durationSeconds,
             event.tripCurrentMa, event.peakCurrentMa, event.voltageMv, event.state);
    queueOk(sequence, command, output);
  } else if (strcmp_P(command, PSTR("GET_FAULT_TRACE")) == 0) {
    TraceRecord trace{};
    if (!store_.readTrace(trace)) {
      queueError(sequence, command, ProtocolError::NOT_FOUND);
      return;
    }
    int used = snprintf_P(output, sizeof(output),
                        PSTR("event_id=%u;period_ms=%u;peak_ma=%u;count=%u;data="),
                        trace.eventId, trace.samplePeriod10Ms * 10U, trace.peakCurrentMa, trace.count);
    for (uint8_t i = 0; i < trace.count && used + 2 < static_cast<int>(sizeof(output)); ++i) {
      used += snprintf_P(output + used, sizeof(output) - used, PSTR("%02X"), trace.samples[i]);
    }
    queueOk(sequence, command, output);
  } else if (strcmp_P(command, PSTR("GET_CLOCK")) == 0) {
    const DateTime& now = clock_.now();
    snprintf_P(output, sizeof(output), PSTR("valid=%u;y=%u;mo=%u;d=%u;h=%u;mi=%u;s=%u;w=%u;ts=%lu"),
             clock_.valid() ? 1U : 0U, now.year, now.month, now.day, now.hour, now.minute,
             now.second, now.weekday, static_cast<unsigned long>(clock_.timestamp()));
    queueOk(sequence, command, output);
  } else if (strcmp_P(command, PSTR("SET_CLOCK")) == 0) {
    uint32_t y, mo, d, h, mi, s, w;
    if (!readUnsigned(payload, PSTR("y"), y) || !readUnsigned(payload, PSTR("mo"), mo)
        || !readUnsigned(payload, PSTR("d"), d) || !readUnsigned(payload, PSTR("h"), h)
        || !readUnsigned(payload, PSTR("mi"), mi) || !readUnsigned(payload, PSTR("s"), s)
        || !readUnsigned(payload, PSTR("w"), w)) {
      queueError(sequence, command, ProtocolError::INVALID_PARAM);
      return;
    }
    DateTime value{static_cast<uint16_t>(y), static_cast<uint8_t>(mo), static_cast<uint8_t>(d),
                   static_cast<uint8_t>(h), static_cast<uint8_t>(mi), static_cast<uint8_t>(s),
                   static_cast<uint8_t>(w)};
    if (!clock_.set(value)) queueError(sequence, command, ProtocolError::RTC_INVALID);
    else {
      controller_.onConfigChanged();
      queueOk(sequence, command);
    }
  } else if (strcmp_P(command, PSTR("START_FEED")) == 0) {
    uint32_t grams = 0;
    if (!readUnsigned(payload, PSTR("amount_g"), grams) || grams > 60000UL) {
      queueError(sequence, command, ProtocolError::INVALID_PARAM);
      return;
    }
    const StartResult result = controller_.startByMass(FeedSource::MANUAL, static_cast<uint16_t>(grams));
    if (result == StartResult::OK) queueOk(sequence, command);
    else queueError(sequence, command, mapStartError(result));
  } else if (strcmp_P(command, PSTR("STOP_FEED")) == 0) {
    if (controller_.stopByUser()) queueOk(sequence, command);
    else queueError(sequence, command, ProtocolError::BUSY);
  } else if (strcmp_P(command, PSTR("RESET_ERROR")) == 0) {
    if (controller_.resetError()) queueOk(sequence, command);
    else queueError(sequence, command, ProtocolError::ACS_INVALID);
  } else if (strcmp_P(command, PSTR("CALIBRATE_ACS")) == 0) {
    if (controller_.runAcsCheck(true)) {
      snprintf_P(output, sizeof(output), PSTR("ok=1;zero_adc=%u;range=%u;spread=%u"), current_.zeroAdc(),
               current_.sampleRange(), current_.blockSpread());
      queueOk(sequence, command, output);
    } else queueError(sequence, command, ProtocolError::ACS_INVALID);
  } else if (strcmp_P(command, PSTR("CALIBRATE_FEED")) == 0) {
    uint32_t duration = 0, grams = 0;
    if (!readUnsigned(payload, PSTR("duration_ms"), duration)
        || !readUnsigned(payload, PSTR("actual_g"), grams)
        || duration < 1000UL || duration > 600000UL || grams < 1UL || grams > 60000UL) {
      queueError(sequence, command, ProtocolError::INVALID_PARAM);
      return;
    }
    ConfigRecord candidate = store_.config();
    candidate.feedRateMgPerSec = static_cast<uint32_t>((static_cast<uint64_t>(grams) * 1000000ULL + duration / 2UL) / duration);
    if (!store_.saveConfig(candidate)) queueError(sequence, command, ProtocolError::CONFIG_ERROR);
    else {
      controller_.onConfigChanged();
      snprintf_P(output, sizeof(output), PSTR("ok=1;rate_mg_s=%lu"),
                 static_cast<unsigned long>(candidate.feedRateMgPerSec));
      queueOk(sequence, command, output);
    }
  } else if (strcmp_P(command, PSTR("TEST_MOTOR")) == 0
             || strcmp_P(command, PSTR("TEST_RELAY")) == 0) {
    uint32_t duration = 0;
    if (!readUnsigned(payload, PSTR("duration_ms"), duration)) {
      queueError(sequence, command, ProtocolError::INVALID_PARAM);
      return;
    }
    const StartResult result = controller_.startTimed(FeedSource::TEST, duration);
    if (result == StartResult::OK) queueOk(sequence, command);
    else queueError(sequence, command, mapStartError(result));
  } else if (strcmp_P(command, PSTR("CFG_BEGIN")) == 0) {
    if (transactionActive_) queueError(sequence, command, ProtocolError::BUSY);
    else {
      stagedConfig_ = store_.config();
      transactionActive_ = true;
      queueOk(sequence, command);
    }
  } else if (strcmp_P(command, PSTR("CFG_ABORT")) == 0) {
    transactionActive_ = false;
    queueOk(sequence, command);
  } else if (strcmp_P(command, PSTR("CFG_COMMIT")) == 0) {
    if (!transactionActive_) queueError(sequence, command, ProtocolError::CONFIG_ERROR);
    else if (!store_.saveConfig(stagedConfig_)) queueError(sequence, command, ProtocolError::CONFIG_ERROR);
    else {
      transactionActive_ = false;
      controller_.onConfigChanged();
      queueOk(sequence, command);
    }
  } else if (strcmp_P(command, PSTR("SET_FEED_COUNT")) == 0) {
    uint32_t count = 0;
    if (!readUnsigned(payload, PSTR("count"), count) || count < 1U || count > MAX_FEEDS) {
      queueError(sequence, command, ProtocolError::INVALID_PARAM);
      return;
    }
    ConfigRecord candidate = editableConfig();
    candidate.feedCount = static_cast<uint8_t>(count);
    applyConfig(sequence, command, candidate);
  } else if (strcmp_P(command, PSTR("SET_FEED_TIME")) == 0) {
    uint32_t index = 0, minute = 0;
    if (!readUnsigned(payload, PSTR("index"), index)
        || !readUnsigned(payload, PSTR("minute"), minute)
        || index >= MAX_FEEDS || minute >= 1440U) {
      queueError(sequence, command, ProtocolError::INVALID_PARAM);
      return;
    }
    ConfigRecord candidate = editableConfig();
    candidate.feeds[index].minuteOfDay = static_cast<uint16_t>(minute);
    applyConfig(sequence, command, candidate);
  } else if (strcmp_P(command, PSTR("SET_FEED_AMOUNT")) == 0) {
    uint32_t index = 0, grams = 0;
    if (!readUnsigned(payload, PSTR("index"), index)
        || !readUnsigned(payload, PSTR("amount_g"), grams)
        || index >= MAX_FEEDS || grams < 10U || grams > 60000U) {
      queueError(sequence, command, ProtocolError::INVALID_PARAM);
      return;
    }
    ConfigRecord candidate = editableConfig();
    candidate.feeds[index].amountGrams = static_cast<uint16_t>(grams);
    applyConfig(sequence, command, candidate);
  } else if (strcmp_P(command, PSTR("SET_CURRENT_LIMIT")) == 0) {
    uint32_t value = 0;
    if (!readUnsigned(payload, PSTR("ma"), value) || value < 500U || value > 30000U) {
      queueError(sequence, command, ProtocolError::INVALID_PARAM);
      return;
    }
    ConfigRecord candidate = editableConfig();
    candidate.currentLimitMa = static_cast<uint16_t>(value);
    applyConfig(sequence, command, candidate);
  } else if (strcmp_P(command, PSTR("SET_CURRENT_DELAY")) == 0) {
    uint32_t value = 0;
    if (!readUnsigned(payload, PSTR("ms"), value) || value < 20U || value > 5000U) {
      queueError(sequence, command, ProtocolError::INVALID_PARAM);
      return;
    }
    ConfigRecord candidate = editableConfig();
    candidate.currentDelayMs = static_cast<uint16_t>(value);
    applyConfig(sequence, command, candidate);
  } else if (strcmp_P(command, PSTR("SET_SCHEDULE_ENABLED")) == 0) {
    uint32_t enabled = 0;
    if (!readUnsigned(payload, PSTR("enabled"), enabled) || enabled > 1U) {
      queueError(sequence, command, ProtocolError::INVALID_PARAM);
      return;
    }
    ConfigRecord candidate = editableConfig();
    if (enabled) candidate.flags |= 0x01U;
    else candidate.flags &= static_cast<uint8_t>(~0x01U);
    applyConfig(sequence, command, candidate);
  } else if (strcmp_P(command, PSTR("SET_MAX_RUNTIME")) == 0) {
    uint32_t value = 0;
    if (!readUnsigned(payload, PSTR("ms"), value) || value < 1000UL || value > 600000UL) {
      queueError(sequence, command, ProtocolError::INVALID_PARAM);
      return;
    }
    ConfigRecord candidate = editableConfig();
    candidate.maxMotorRunMs = value;
    applyConfig(sequence, command, candidate);
  } else {
    queueError(sequence, command, ProtocolError::UNKNOWN_COMMAND);
  }
}

bool BluetoothManager::queueStateEvent() {
  char output[120];
  snprintf_P(output, sizeof(output),
           PSTR("state=%u;fault=%u;motor=%u;current_ma=%u;remaining_ms=%lu"),
           static_cast<unsigned>(controller_.state()), static_cast<unsigned>(controller_.fault()),
           controller_.motorOn() ? 1U : 0U, current_.displayCurrentMa(),
           static_cast<unsigned long>(controller_.remainingMs(millis())));
  return queueFrame(0, 'V', "STATE", output);
}

bool BluetoothManager::queueOperationEvent(const EventRecord& event) {
  char output[180];
  snprintf_P(output, sizeof(output),
           PSTR("id=%u;ts=%lu;type=%u;reason=%u;source=%u;planned_g=%u;estimated_g=%u;duration_s=%u;trip_ma=%u;peak_ma=%u;voltage_mv=%u;state=%u"),
           event.id, static_cast<unsigned long>(event.timestamp), event.type, event.reason, event.source,
           event.plannedGrams, event.estimatedGrams, event.durationSeconds, event.tripCurrentMa,
           event.peakCurrentMa, event.voltageMv, event.state);
  return queueFrame(0, 'V', "EVENT", output);
}
