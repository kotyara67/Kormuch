#define MOTOR_PIN 4

#define RTC_DAT 7
#define RTC_CLK 8
#define RTC_RST 6

#define ACS712_PIN A0

#define RX_PIN 12 // идет в TX модуля
#define TX_PIN 11 // идет в RX модуля

#define LED_PIN 13

#include <Arduino.h>
// Память
#include <EEPROM.h>
// RTC
#include <RtcDS1302.h>

#include <SoftwareSerial.h>

SoftwareSerial mySerial(RX_PIN, TX_PIN); // RX, TX

ThreeWire myWire(RTC_DAT, RTC_CLK, RTC_RST); // DAT, CLK, RST
RtcDS1302<ThreeWire> Rtc(myWire);

bool motorActive = false;
bool motorError = false;

int motorKilos = 0;

unsigned long motorStartTime = 0;
unsigned long overcurrentStartTime = 0;
unsigned long lastCurrentReadTime = 0;

const float ACS_SENSITIVITY = 0.066; // 66 mV/A
const float ADC_REFERENCE = 5.0;
const int CURRENT_SAMPLES = 40;              // усреднение
const float OVERCURRENT_LIMIT = 6;           // аварийный порог
const unsigned long OVERCURRENT_DELAY = 300; // мс

float acsZeroVoltage = 2.5;
bool motorOvercurrent = false;
unsigned long overcurrentStart = 0;

float readCurrentAverage()
{
    long sum = 0;

    for (int i = 0; i < CURRENT_SAMPLES; i++)
    {
        sum += analogRead(ACS712_PIN);
    }

    float averageADC = (float)sum / CURRENT_SAMPLES;
    float voltage = averageADC * (ADC_REFERENCE / 1023.0);

    return (voltage - acsZeroVoltage) / ACS_SENSITIVITY;
}

void calibrateACS712()
{
    const int calibrationSamples = 200;
    long sum = 0;

    digitalWrite(MOTOR_PIN, LOW);
    delay(300);

    for (int i = 0; i < calibrationSamples; i++)
    {
        sum += analogRead(ACS712_PIN);
        delay(2);
    }

    float averageADC = (float)sum / calibrationSamples;
    acsZeroVoltage = averageADC * (ADC_REFERENCE / 1023.0);

    Serial.print("ACS712 zero voltage: ");
    Serial.println(acsZeroVoltage, 4);
    mySerial.print("ACS712 zero voltage: ");
    mySerial.println(acsZeroVoltage, 4);
}

void updateMotor()
{
    if (!motorActive)
        return;

    unsigned long nowMillis = millis();

    // Проверяем, не закончилось ли запланированное время работы.
    if (nowMillis - motorStartTime >= feedDuration)
    {
        finishFeed();
        return;
    }
}

void setup()
{
    // Мотор отключен по умолчанию
    pinMode(MOTOR_PIN, OUTPUT);
    digitalWrite(MOTOR_PIN, LOW);

    motorTimer = millis();

    pinMode(ACS712_PIN, INPUT);

    // set the data rate for the SoftwareSerial port
    mySerial.begin(9600);

    Serial.begin(9600); // Установка последовательной связи на скорости 9600
    Serial.print("Data: ");
    Serial.println(__DATE__);
    Serial.print("Time: ");
    Serial.println(__TIME__);
    // Инициализация RTC
    Rtc.Begin();
    // RtcDateTime compiled = RtcDateTime(__DATE__, __TIME__); // Копирование даты и времени в compiled
    Serial.println(); // Отправка данных на последовательный порт

    Serial.println("ACS712 calibration: MOTOR OFF");
    mySerial.println("ACS712 calibration: MOTOR OFF");
    calibrateACS712();

    Serial.println("System ready.");
    mySerial.println("System ready.");
}

void loop()
{
    RtcDateTime rtcTime = Rtc.GetDateTime();
    if (millis() % 5000 == 0)
    {
        mySerial.print("time#");
        mySerial.print(rtcTime.Hour());
        mySerial.print(":");
        int min = rtcTime.Minute();
        if (min <= 9)
        {
            mySerial.print("0");
            mySerial.println(min);
        }
        else
        {
            mySerial.println(min);
        }
    }
    if (mySerial.available())
    {
        String recivedData = mySerial.readStringUntil('\n');
        Serial.println(recivedData);
        Serial.println(recivedData.length());
        Serial.println("-------------");
        if (recivedData.startsWith("get#ver"))
        {
            mySerial.println("ver#2.10");
        }
    }
}