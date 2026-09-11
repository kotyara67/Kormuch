#include "MotorDriver.h"

#include "BoardConfig.h"

void MotorDriver::begin() {
  digitalWrite(BoardConfig::MOTOR_PIN, LOW);
  pinMode(BoardConfig::MOTOR_PIN, OUTPUT);
  digitalWrite(BoardConfig::MOTOR_PIN, LOW);
  on_ = false;
}

void MotorDriver::on() {
  digitalWrite(BoardConfig::MOTOR_PIN, HIGH);
  on_ = true;
}

void MotorDriver::off() {
  digitalWrite(BoardConfig::MOTOR_PIN, LOW);
  on_ = false;
}

