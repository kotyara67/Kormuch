#pragma once

#include <Arduino.h>

uint16_t crc16Ccitt(const uint8_t* data, size_t length);
uint8_t crc8Dallas(const uint8_t* data, size_t length);

