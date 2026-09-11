// НАСТРОЙКА ПАРАМЕТРОВ ПОВЕДЕНИЯ

int time_morning = 8 * 2; // В получасах. 12 = 6 утра
int time_sleep = 19 * 2;  // Я хз что будет, если выставить time_morning <= time_sleep. Проверять не рекомендуется.

int option_values[2]{
    2, // кол-во кормлений
    1, // килограммы (в теории)
};

#define MOTOR_PIN 3

#define RTC_DAT 7
#define RTC_CLK 8
#define RTC_RST 6

#define ACS712_PIN A0

#define RX_PIN 11 // идет в TX модуля
#define TX_PIN 12 // идет в RX модуля

#define LED_PIN 13

#include <Arduino.h>
// Память
#include <EEPROM.h>
// RTC
#include <RtcDS1302.h>

#include <SoftwareSerial.h>

#include <iarduino_ACS712.h>

SoftwareSerial mySerial(RX_PIN, TX_PIN); // RX, TX

iarduino_ACS712 sensor(ACS712_PIN);

ThreeWire myWire(RTC_DAT, RTC_CLK, RTC_RST); // DAT, CLK, RST
RtcDS1302<ThreeWire> Rtc(myWire);

// Переменные для защиты от повторного срабатывания
int lastFeedMinute = -1;
int lastDate = -1;

const int stepsPerRevolution = 400; // для NEMA23 1.8° (200 шагов/оборот)

String halfHoursToTime(int halfHours)
{
    int totalMinutes = halfHours * 30;
    int hours = (totalMinutes / 60) % 24; // часы в пределах 0..23
    int minutes = totalMinutes % 60;      // всегда 0 или 30
    return String(hours) + ":" + String(minutes);
}

// Переводит получасы в минуты от полуночи
int halfHoursToMinutes(int half)
{
    return half * 30;
}

// Проверяет, нужно ли кормить в текущий момент
// Авторство - Deepseek
bool checkAndFeed(int currentMinutes, int currentDay)
{
    int feedCount = option_values[0];
    // Если кормлений нет — выходим
    if (feedCount <= 0)
        return false;

    int morningMin = halfHoursToMinutes(time_morning);
    int eveningMin = halfHoursToMinutes(time_sleep);

    // Текущее время вне периода?
    if (currentMinutes < morningMin || currentMinutes > eveningMin)
    {
        return false;
    }

    // Особый случай: одно кормление
    if (feedCount == 1)
    {
        if (abs(currentMinutes - morningMin) <= 0.5)
        {
            if (lastFeedMinute != currentMinutes || lastDate != currentDay)
            {
                lastFeedMinute = currentMinutes;
                lastDate = currentDay;
                return true;
            }
        }
        return false;
    }

    // Несколько кормлений: вычисляем ближайшее
    float interval = (float)(eveningMin - morningMin) / (feedCount - 1);
    float t = (currentMinutes - morningMin) / interval;
    int k = round(t); // номер кормления от 0 до feedCount-1

    // Проверяем границы
    if (k < 0 || k >= feedCount)
        return false;

    float feedMin = morningMin + k * interval;
    if (abs(currentMinutes - feedMin) <= 0.5)
    { // допуск 30 секунд
        if (lastFeedMinute != currentMinutes || lastDate != currentDay)
        {
            lastFeedMinute = currentMinutes;
            lastDate = currentDay;
            return true;
        }
    }
    return false;
}

int stepDelay = 80; // микросекунды между шагами (чем меньше, тем быстрее)

void feed()
{
    digitalWrite(MOTOR_PIN, HIGH);
    delay(10 * 1000 * option_values[1]); // секунды (сколько должно работать для 1 кг) * 1000 (в одной секунде 1000 мс) * КГ
    digitalWrite(MOTOR_PIN, LOW);
}

float readVoltage()
{
    float voltage = analogRead(ACS712_PIN) * (5.0 / 1023.0);
    return voltage;
}

float readCurrent()
{
    float voltage = analogRead(ACS712_PIN) * (5.0 / 1023.0);
    float current = (voltage - 2.5) / 0.066;
    return current;
}

void setup()
{
    // Мотор отключен по умолчанию
    pinMode(MOTOR_PIN, OUTPUT);
    digitalWrite(MOTOR_PIN, LOW);

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
    RtcDateTime compiled = RtcDateTime(__DATE__, __TIME__); // Копирование даты и времени в compiled
    // Rtc.SetDateTime(compiled);                              // Установка времени
    Serial.println(); // Отправка данных на последовательный порт
}

void loop()
{

    // Rtc.GetDateTime();
    RtcDateTime now = Rtc.GetDateTime();

    int currentMinutes = now.Hour() * 60 + now.Minute();
    int currentDay = now.Year() * 366 + now.Day();

    // feed(); // Раскоментировать это и закоментировать if ниже, чтоб кормило постоянно

    if (checkAndFeed(currentMinutes, currentDay))
    {
        // feed();
    }

    if (millis() % 100 == 0)
    {
        // float v = sensor.getZeroVDC();

        // mySerial.print("Sensor zero V: ");
        // mySerial.println(v);
        // Serial.println(v);

        // mySerial.print("Voltage: ");
        // mySerial.println(readVoltage());
        // //
        // mySerial.print("Current: ");
        // mySerial.println(readCurrent());
        //
        // mySerial.print("time: ");
        // mySerial.print(now.Hour());
        // mySerial.print(":");
        // mySerial.print(now.Minute());
        // mySerial.print(":");
        // mySerial.println(now.Second());
    }

    // с модуля на комп
    if (mySerial.available())
    {
        byte b = mySerial.read();
        Serial.print(b, HEX);
        Serial.print(" ");
    }
}