// НАСТРОЙКА ПАРАМЕТРОВ ПОВЕДЕНИЯ

int time_morning = 8 * 2; // В получасах. 8*2 = 08:00
int time_sleep = 19 * 2;  // В получасах. 19*2 = 19:00

int option_values[2]{
    2, // кол-во кормлений
    1, // килограммы (в теории)
};

#define MOTOR_PIN 4

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

// ВАЖНО:
// Для ACS712-30A здесь используется прямое чтение аналогового выхода.
// Библиотека iarduino_ACS712 для этого варианта не используется.

SoftwareSerial mySerial(RX_PIN, TX_PIN); // RX, TX

ThreeWire myWire(RTC_DAT, RTC_CLK, RTC_RST); // DAT, CLK, RST
RtcDS1302<ThreeWire> Rtc(myWire);

// Переменные для защиты от повторного срабатывания
int lastFeedMinute = -1;
int lastDate = -1;

const int stepsPerRevolution = 400; // для NEMA23 1.8° (200 шагов/оборот)

// ============================================================
// НАСТРОЙКИ ACS712-30A И ЗАЩИТЫ МОТОРА
// ============================================================

// Чувствительность типичного ACS712-30A:
// примерно 66 мВ на 1 А.
const float ACS_SENSITIVITY = 0.066;

// Опорное напряжение Arduino.
// Для Uno/Nano обычно около 5 В.
// При питании от другого напряжения это значение нужно изменить.
const float ADC_REFERENCE = 5.0;

// Сколько измерений усреднять.
// Чем больше — тем спокойнее показания, но тем медленнее реакция.
const int CURRENT_SAMPLES = 20;

// Порог перегрузки двигателя.
// ПОСТАВЬ СЮДА СВОЙ ДОПУСТИМЫЙ ТОК.
const float OVERCURRENT_LIMIT = 6;

// Ток должен быть выше порога непрерывно это время,
// прежде чем мотор будет отключён.
const unsigned long OVERCURRENT_TIME = 300;

// Период вывода/измерения тока.
const unsigned long CURRENT_READ_INTERVAL = 100;

// Время калибровки нулевого тока при включении Arduino.
const int ZERO_CALIBRATION_SAMPLES = 200;

// Сюда записывается реальное нулевое напряжение ACS712.
float acsZeroVoltage = 2.5;

// ============================================================
// СОСТОЯНИЕ МОТОРА
// ============================================================

bool motorRunning = false;
bool motorFault = false;

unsigned long motorStartTime = 0;
unsigned long overcurrentStartTime = 0;
unsigned long lastCurrentReadTime = 0;
unsigned long lastStatusPrintTime = 0;

// Время работы мотора на одно кормление.
unsigned long feedDuration = 0;

// ============================================================
// ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ
// ============================================================

String halfHoursToTime(int halfHours)
{
    int totalMinutes = halfHours * 30;
    int hours = (totalMinutes / 60) % 24;
    int minutes = totalMinutes % 60;
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

    if (feedCount <= 0)
        return false;

    int morningMin = halfHoursToMinutes(time_morning);
    int eveningMin = halfHoursToMinutes(time_sleep);

    if (currentMinutes < morningMin || currentMinutes > eveningMin)
    {
        return false;
    }

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

    float interval = (float)(eveningMin - morningMin) / (feedCount - 1);
    float t = (currentMinutes - morningMin) / interval;
    int k = round(t);

    if (k < 0 || k >= feedCount)
        return false;

    float feedMin = morningMin + k * interval;

    if (abs(currentMinutes - feedMin) <= 0.5)
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

// ============================================================
// ACS712
// ============================================================

// Считывает напряжение с выхода ACS712.
float readVoltage()
{
    long sum = 0;

    for (int i = 0; i < CURRENT_SAMPLES; i++)
    {
        sum += analogRead(ACS712_PIN);
    }

    float adc = (float)sum / CURRENT_SAMPLES;

    return adc * (ADC_REFERENCE / 1023.0);
}

// Возвращает ток в амперах.
// Для защиты берём модуль, потому что направление тока нам не важно.
float readCurrent()
{
    float voltage = readVoltage();

    float current = (voltage - acsZeroVoltage) / ACS_SENSITIVITY;

    return abs(current);
}

// Калибровка нуля ACS712.
// В ЭТОТ МОМЕНТ МОТОР ОБЯЗАТЕЛЬНО ДОЛЖЕН БЫТЬ ВЫКЛЮЧЕН.
void calibrateACS712()
{
    Serial.println("ACS712 calibration...");
    Serial.println("Motor MUST be OFF!");

    mySerial.println("ACS712 calibration...");
    mySerial.println("Motor MUST be OFF!");

    delay(500);

    long sum = 0;

    for (int i = 0; i < ZERO_CALIBRATION_SAMPLES; i++)
    {
        sum += analogRead(ACS712_PIN);
        delay(2);
    }

    float adc = (float)sum / ZERO_CALIBRATION_SAMPLES;

    acsZeroVoltage = adc * (ADC_REFERENCE / 1023.0);

    Serial.print("ACS zero voltage: ");
    Serial.println(acsZeroVoltage, 4);

    mySerial.print("ACS zero voltage: ");
    mySerial.println(acsZeroVoltage, 4);

    delay(500);
}

// ============================================================
// МОТОР
// ============================================================

// Запускает мотор.
// В отличие от старого feed(), здесь НЕТ delay().
// Поэтому Arduino может постоянно следить за током.
void startFeed()
{
    if (motorRunning)
        return;

    if (motorFault)
    {
        Serial.println("MOTOR LOCKED: reset Arduino after overload.");
        mySerial.println("MOTOR LOCKED: reset Arduino after overload.");
        return;
    }

    feedDuration = (unsigned long)10UL * 1000UL * option_values[1];

    motorStartTime = millis();
    overcurrentStartTime = 0;

    motorRunning = true;

    digitalWrite(MOTOR_PIN, HIGH);

    Serial.println("MOTOR START");
    mySerial.println("MOTOR START");
}

// Аварийно выключает мотор.
void stopMotorOverload(float current)
{
    digitalWrite(MOTOR_PIN, LOW);

    motorRunning = false;
    motorFault = true;

    Serial.println("!!! OVERCURRENT !!!");
    Serial.print("Current: ");
    Serial.print(current, 2);
    Serial.println(" A");
    Serial.println("MOTOR OFF - FAULT");

    mySerial.println("!!! OVERCURRENT !!!");
    mySerial.print("Current: ");
    mySerial.print(current, 2);
    mySerial.println(" A");
    mySerial.println("MOTOR OFF - FAULT");
}

// Обычное завершение кормления.
void finishFeed()
{
    digitalWrite(MOTOR_PIN, LOW);

    motorRunning = false;
    overcurrentStartTime = 0;

    Serial.println("MOTOR STOP");
    mySerial.println("MOTOR STOP");
}

// Проверяет ток и время работы мотора.
void updateMotor()
{
    if (!motorRunning)
        return;

    unsigned long nowMillis = millis();

    // Проверяем, не закончилось ли запланированное время работы.
    if (nowMillis - motorStartTime >= feedDuration)
    {
        finishFeed();
        return;
    }

    // Читаем ток с заданным периодом.
    if (nowMillis - lastCurrentReadTime >= CURRENT_READ_INTERVAL)
    {
        lastCurrentReadTime = nowMillis;

        float current = readCurrent();

        Serial.print("Current: ");
        Serial.print(current, 2);
        Serial.println(" A");

        mySerial.print("Current: ");
        mySerial.print(current, 2);
        mySerial.println(" A");

        // Если ток выше допустимого порога...
        if (current >= OVERCURRENT_LIMIT)
        {
            // ...запускаем таймер перегрузки.
            if (overcurrentStartTime == 0)
            {
                overcurrentStartTime = nowMillis;
            }
            // Если перегрузка держится достаточно долго — отключаем мотор.
            else if (nowMillis - overcurrentStartTime >= OVERCURRENT_TIME)
            {
                stopMotorOverload(current);
            }
        }
        else
        {
            // Ток вернулся в норму — сбрасываем таймер перегрузки.
            overcurrentStartTime = 0;
        }
    }
}

// ============================================================
// SETUP
// ============================================================

void setup()
{
    // СНАЧАЛА гарантированно выключаем мотор.
    pinMode(MOTOR_PIN, OUTPUT);
    digitalWrite(MOTOR_PIN, LOW);

    pinMode(ACS712_PIN, INPUT);

    mySerial.begin(9600);
    Serial.begin(9600);

    Serial.print("Data: ");
    Serial.println(__DATE__);

    Serial.print("Time: ");
    Serial.println(__TIME__);

    // Инициализация RTC
    Rtc.Begin();

    RtcDateTime compiled = RtcDateTime(__DATE__, __TIME__);
    // Rtc.SetDateTime(compiled); // Установка времени при необходимости

    // Калибруем ноль ACS712 при выключенном моторе.
    calibrateACS712();

    Serial.println("System ready.");
    Serial.print("Overcurrent limit: ");
    Serial.print(OVERCURRENT_LIMIT);
    Serial.println(" A");

    mySerial.println("System ready.");
    mySerial.print("Overcurrent limit: ");
    mySerial.print(OVERCURRENT_LIMIT);
    mySerial.println(" A");

    Serial.println();
}

// ============================================================
// LOOP
// ============================================================

void loop()
{
    RtcDateTime now = Rtc.GetDateTime();

    int currentMinutes = now.Hour() * 60 + now.Minute();
    int currentDay = now.Year() * 366 + now.Day();

    // --------------------------------------------------------
    // АВТОМАТИЧЕСКОЕ КОРМЛЕНИЕ
    // --------------------------------------------------------

    // Если мотор не работает и нет аварии — проверяем расписание.
    if (!motorRunning && !motorFault)
    {
        if (checkAndFeed(currentMinutes, currentDay))
        {
            startFeed();
        }
    }

    // --------------------------------------------------------
    // КОНТРОЛЬ МОТОРА И ТОКА
    // --------------------------------------------------------

    updateMotor();

    // --------------------------------------------------------
    // ПЕРИОДИЧЕСКИЙ СТАТУС
    // --------------------------------------------------------

    if (millis() - lastStatusPrintTime >= 3000)
    {
        lastStatusPrintTime = millis();

        float current = readCurrent();

        Serial.print("Status | Current: ");
        Serial.print(current, 2);
        Serial.print(" A | Motor: ");
        Serial.print(motorRunning ? "ON" : "OFF");
        Serial.print(" | Fault: ");
        Serial.println(motorFault ? "YES" : "NO");

        mySerial.print("Current: ");
        mySerial.print(current, 2);
        mySerial.print(" A | Motor: ");
        mySerial.print(motorRunning ? "ON" : "OFF");
        mySerial.print(" | Fault: ");
        mySerial.println(motorFault ? "YES" : "NO");

        mySerial.print("time: ");
        mySerial.print(now.Hour());
        mySerial.print(":");
        mySerial.print(now.Minute());
        mySerial.print(":");
        mySerial.println(now.Second());
    }

    // --------------------------------------------------------
    // С МОДУЛЯ НА КОМП
    // --------------------------------------------------------

    if (mySerial.available())
    {
        String data = mySerial.readString();
        if (data.startsWith("testFeed"))
        {
            startFeed();
        }
    }
}
