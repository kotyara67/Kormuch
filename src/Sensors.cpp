#include "Sensors.h"

#include "BoardConfig.h"

uint16_t CurrentSensor::adcDeltaToMa(uint16_t delta) {
  // ACS712-30A is ratiometric: one ADC count is approximately 74.02 mA
  // independent of the exact shared 5 V supply voltage.
  const uint32_t value = static_cast<uint32_t>(delta) * 7402UL + 50UL;
  return static_cast<uint16_t>(value / 100UL);
}

void CurrentSensor::begin(uint16_t storedZeroAdc) {
  pinMode(BoardConfig::ACS712_PIN, INPUT);
  zeroAdc_ = storedZeroAdc;
  rawAdc_ = analogRead(BoardConfig::ACS712_PIN);
  fastFilterQ3_ = static_cast<int32_t>(rawAdc_) << 3;
  slowFilterQ4_ = static_cast<int32_t>(rawAdc_) << 4;
  state_ = AcsState::UNKNOWN;
  resetTrace();
}

void CurrentSensor::update(uint32_t nowMicros, uint32_t nowMillis) {
  if (static_cast<uint32_t>(nowMicros - lastSampleMicros_) >= BoardConfig::SENSOR_SAMPLE_PERIOD_US) {
    lastSampleMicros_ = nowMicros;
    rawAdc_ = analogRead(BoardConfig::ACS712_PIN);
    const int32_t targetQ3 = static_cast<int32_t>(rawAdc_) << 3;
    fastFilterQ3_ += (targetQ3 - fastFilterQ3_) >> 3;
    const int32_t targetQ4 = static_cast<int32_t>(rawAdc_) << 4;
    slowFilterQ4_ += (targetQ4 - slowFilterQ4_) >> 4;

    const uint16_t fastAdc = static_cast<uint16_t>((fastFilterQ3_ + 4) >> 3);
    const uint16_t slowAdc = static_cast<uint16_t>((slowFilterQ4_ + 8) >> 4);
    currentMa_ = adcDeltaToMa(fastAdc > zeroAdc_ ? fastAdc - zeroAdc_ : zeroAdc_ - fastAdc);
    displayCurrentMa_ = adcDeltaToMa(slowAdc > zeroAdc_ ? slowAdc - zeroAdc_ : zeroAdc_ - slowAdc);
  }

  if (static_cast<uint32_t>(nowMillis - lastTraceMs_) >= BoardConfig::TRACE_SAMPLE_PERIOD_MS) {
    lastTraceMs_ = nowMillis;
    uint16_t scaled = static_cast<uint16_t>((currentMa_ + 62U) / 125U);
    if (scaled > 255U) scaled = 255U;
    trace_[traceHead_] = static_cast<uint8_t>(scaled);
    traceHead_ = static_cast<uint8_t>((traceHead_ + 1U) % BoardConfig::TRACE_SAMPLE_COUNT);
    if (traceCount_ < BoardConfig::TRACE_SAMPLE_COUNT) ++traceCount_;
  }
}

bool CurrentSensor::diagnose(bool motorIsOff, uint32_t checkedTimestamp) {
  if (!motorIsOff) return false;
  state_ = AcsState::CHECKING;

  uint16_t blockAverage[4]{};
  uint16_t overallMin = 1023;
  uint16_t overallMax = 0;
  uint32_t total = 0;
  for (uint8_t block = 0; block < 4; ++block) {
    uint32_t blockSum = 0;
    for (uint8_t sample = 0; sample < 32; ++sample) {
      const uint16_t value = analogRead(BoardConfig::ACS712_PIN);
      if (value < overallMin) overallMin = value;
      if (value > overallMax) overallMax = value;
      blockSum += value;
    }
    blockAverage[block] = static_cast<uint16_t>((blockSum + 16UL) / 32UL);
    total += blockSum;
  }

  const uint16_t mean = static_cast<uint16_t>((total + 64UL) / 128UL);
  uint16_t blockMin = blockAverage[0];
  uint16_t blockMax = blockAverage[0];
  for (uint8_t i = 1; i < 4; ++i) {
    if (blockAverage[i] < blockMin) blockMin = blockAverage[i];
    if (blockAverage[i] > blockMax) blockMax = blockAverage[i];
  }
  const uint16_t sampleRange = overallMax - overallMin;
  const uint16_t blockSpread = blockMax - blockMin;
  sampleRange_ = static_cast<uint8_t>(sampleRange > 255U ? 255U : sampleRange);
  blockSpread_ = static_cast<uint8_t>(blockSpread > 255U ? 255U : blockSpread);
  lastCheckTimestamp_ = checkedTimestamp;

  const bool absoluteOk = mean >= BoardConfig::ACS_ABSOLUTE_MIN_ADC && mean <= BoardConfig::ACS_ABSOLUTE_MAX_ADC;
  const bool noiseOk = sampleRange_ <= BoardConfig::ACS_MAX_SAMPLE_RANGE_ADC
                    && blockSpread_ <= BoardConfig::ACS_MAX_BLOCK_SPREAD_ADC;
  if (!absoluteOk || !noiseOk) {
    state_ = AcsState::ERROR;
    return false;
  }

  zeroAdc_ = mean;
  rawAdc_ = mean;
  fastFilterQ3_ = static_cast<int32_t>(mean) << 3;
  slowFilterQ4_ = static_cast<int32_t>(mean) << 4;
  currentMa_ = 0;
  displayCurrentMa_ = 0;
  state_ = AcsState::OK;
  return true;
}

void CurrentSensor::resetTrace() {
  for (uint8_t& value : trace_) value = 0;
  traceHead_ = 0;
  traceCount_ = 0;
  lastTraceMs_ = millis();
}

void CurrentSensor::copyChronologicalTrace(uint8_t* destination, uint8_t& count) const {
  count = traceCount_;
  const uint8_t start = traceCount_ < BoardConfig::TRACE_SAMPLE_COUNT ? 0U : traceHead_;
  for (uint8_t i = 0; i < count; ++i) {
    destination[i] = trace_[static_cast<uint8_t>((start + i) % BoardConfig::TRACE_SAMPLE_COUNT)];
  }
}

void VoltageSensor::begin() {
  if (BoardConfig::VOLTAGE_SENSOR_ENABLED) pinMode(BoardConfig::BATTERY_PIN, INPUT);
}

bool VoltageSensor::available() const { return BoardConfig::VOLTAGE_SENSOR_ENABLED; }

uint16_t VoltageSensor::readVccMillivolts() {
#if defined(__AVR_ATmega328P__) || defined(__AVR_ATmega168__)
  ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
  delayMicroseconds(250);
  ADCSRA |= _BV(ADSC);
  while (bit_is_set(ADCSRA, ADSC)) {}
  const uint16_t result = ADC;
  if (result == 0U) return 5000U;
  return static_cast<uint16_t>(1125300UL / result);
#else
  return 5000U;
#endif
}

void VoltageSensor::update(uint32_t nowMs) {
  if (static_cast<uint32_t>(nowMs - lastReadMs_) < 1000UL) return;
  lastReadMs_ = nowMs;
  vccMillivolts_ = readVccMillivolts();
  if (!BoardConfig::VOLTAGE_SENSOR_ENABLED) {
    millivolts_ = 0;
    return;
  }
  analogRead(BoardConfig::BATTERY_PIN);
  uint32_t sum = 0;
  for (uint8_t i = 0; i < 16; ++i) sum += analogRead(BoardConfig::BATTERY_PIN);
  const uint16_t adc = static_cast<uint16_t>((sum + 8UL) / 16UL);
  // Hardware divider 33k/10k: battery = ADC voltage * 4.3.
  millivolts_ = static_cast<uint16_t>((static_cast<uint32_t>(adc) * vccMillivolts_ * 43UL + 5115UL) / 10230UL);
}
