#pragma once

#include <Arduino.h>

namespace BoardConfig {

// Restored wiring requested by the owner.
constexpr uint8_t MOTOR_PIN = 4;
// Both wires are fixed on D11/D12. Firmware detects which one carries the
// JDY-31 TX signal before constructing SoftwareSerial, removing RX/TX naming
// ambiguity without requiring a wiring change.
constexpr uint8_t BLUETOOTH_WIRE_D11 = 11;
constexpr uint8_t BLUETOOTH_WIRE_D12 = 12;

constexpr uint8_t RTC_DAT_PIN = 7;
constexpr uint8_t RTC_CLK_PIN = 8;
constexpr uint8_t RTC_RST_PIN = 6;

constexpr uint8_t ACS712_PIN = A0;
constexpr uint8_t BATTERY_PIN = A1;
constexpr uint8_t STATUS_LED_PIN = LED_BUILTIN;

constexpr uint16_t BLUETOOTH_BAUD_SCAN_MS = 900;

// The existing electrical circuit has no battery divider. Reporting a voltage
// from a floating A1 input would be unsafe, so the feature stays disabled.
constexpr bool VOLTAGE_SENSOR_ENABLED = false;

constexpr uint16_t DEFAULT_CURRENT_LIMIT_MA = 4000;
constexpr uint16_t DEFAULT_CURRENT_DELAY_MS = 200;
constexpr uint16_t ACS_ABSOLUTE_MIN_ADC = 409; // 0.40 * 1023
constexpr uint16_t ACS_ABSOLUTE_MAX_ADC = 614; // 0.60 * 1023
constexpr uint8_t ACS_MAX_SAMPLE_RANGE_ADC = 25;
constexpr uint8_t ACS_MAX_BLOCK_SPREAD_ADC = 8;
constexpr uint16_t ACS_DEFAULT_ZERO_ADC = 512;
constexpr uint32_t DEFAULT_MAX_MOTOR_MS = 180000UL;
// An autonomous feed may start slightly late after a short reset or temporary
// RTC read failure. Older entries are never caught up to avoid a large,
// unexpected delayed feeding.
constexpr uint16_t SCHEDULE_GRACE_SECONDS = 120;

constexpr uint16_t SENSOR_SAMPLE_PERIOD_US = 1000;
constexpr uint16_t TRACE_SAMPLE_PERIOD_MS = 200;
constexpr uint8_t TRACE_SAMPLE_COUNT = 50;

constexpr uint16_t PROTOCOL_RX_CAPACITY = 220;
constexpr uint16_t PROTOCOL_TX_CAPACITY = 240;

} // namespace BoardConfig
