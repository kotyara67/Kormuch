#include <Arduino.h>

#define MOTOR_PIN 4

void setup()
{
    pinMode(MOTOR_PIN, OUTPUT);
}

void loop()
{
    digitalWrite(MOTOR_PIN, HIGH);
    delay(1000);
    digitalWrite(MOTOR_PIN, LOW);
    delay(1000);
}