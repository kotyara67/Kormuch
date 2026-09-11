// НАСТРОЙКА ПАРАМЕТРОВ ПОВЕДЕНИЯ

int time_morning = 8 * 2; // В получасах. 12 = 6 утра
int time_sleep = 19 * 2;  // Я хз что будет, если выставить time_morning <= time_sleep. Проверять не рекомендуется.

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

SoftwareSerial mySerial(RX_PIN, TX_PIN); // RX, TX

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

// --- ACS712 / test settings ---
const float ACS712_SENSITIVITY = 0.066; // ACS712-30A: ~66 mV/A
float acsZeroVoltage = 2.5;

const unsigned long SENSOR_INTERVAL = 200;
const unsigned long MOTOR_TEST_DELAY = 15000;
const unsigned long MOTOR_TEST_TIME = 10000;

unsigned long lastSensorRead = 0;
unsigned long testStartTime = 0;
bool testMotorStarted = false;
bool motorState = false;

int stepDelay = 80; // микросекунды между шагами (чем меньше, тем быстрее)

void feed()
{
    motorState = true;
    digitalWrite(MOTOR_PIN, HIGH);
    delay(10 * 1000 * option_values[1]); // секунды (сколько должно работать для 1 кг) * 1000 (в одной секунде 1000 мс) * КГ
    digitalWrite(MOTOR_PIN, LOW);
    motorState = false;
}

int readACS712ADC()
{
    const int samples = 20;
    long sum = 0;

    for (int i = 0; i < samples; i++)
    {
        sum += analogRead(ACS712_PIN);
    }

    return (int)(sum / samples);
}

float adcToVoltage(int adc)
{
    return adc * (5.0 / 1023.0);
}

float readVoltage(int adc)
{
    return adcToVoltage(adc);
}

float readCurrent(int adc)
{
    float voltage = adcToVoltage(adc);
    return abs((voltage - acsZeroVoltage) / ACS712_SENSITIVITY);
}

void printSensorData()
{
    int adc = readACS712ADC();
    float voltage = readVoltage(adc);
    float current = readCurrent(adc);

    // Небольшие показания вокруг нуля считаем нулём для удобства отображения.
    if (current > -0.05 && current < 0.05)
        current = 0.0;

    Serial.print("MOTOR: ");
    Serial.print(motorState ? "ON" : "OFF");
    Serial.print(" | ADC: ");
    Serial.print(adc);
    Serial.print(" | Voltage: ");
    Serial.print(voltage, 3);
    Serial.print(" V | Current: ");
    Serial.print(current, 2);
    Serial.println(" A");

    mySerial.print("MOTOR: ");
    mySerial.print(motorState ? "ON" : "OFF");
    mySerial.print(" | ADC: ");
    mySerial.print(adc);
    mySerial.print(" | Voltage: ");
    mySerial.print(voltage, 3);
    mySerial.print(" V | Current: ");
    mySerial.print(current, 2);
    mySerial.println(" A");
}

void calibrateACS712()
{
    Serial.println("ACS712 calibration: MOTOR OFF");
    mySerial.println("ACS712 calibration: MOTOR OFF");

    const int samples = 100;
    long sum = 0;

    for (int i = 0; i < samples; i++)
    {
        sum += analogRead(ACS712_PIN);
        delay(5);
    }

    int zeroADC = (int)(sum / samples);
    acsZeroVoltage = adcToVoltage(zeroADC);

    Serial.print("ACS712 zero ADC: ");
    Serial.println(zeroADC);
    Serial.print("ACS712 zero voltage: ");
    Serial.print(acsZeroVoltage, 3);
    Serial.println(" V");

    mySerial.print("ACS712 zero ADC: ");
    mySerial.println(zeroADC);
    mySerial.print("ACS712 zero voltage: ");
    mySerial.print(acsZeroVoltage, 3);
    mySerial.println(" V");
}

void setup()
{
    // Мотор отключен по умолчанию
    pinMode(MOTOR_PIN, OUTPUT);
    digitalWrite(MOTOR_PIN, LOW);

    pinMode(ACS712_PIN, INPUT);
    motorState = false;

    // set the data rate for the SoftwareSerial port
    mySerial.begin(9600);

    Serial.begin(9600); // Установка последовательной связи на скорости 9600
    Serial.print("Data: ");
    Serial.println(__DATE__);
    Serial.print("Time: ");
    Serial.println(__TIME__);

    testStartTime = millis();
    calibrateACS712();
    // Инициализация RTC
    Rtc.Begin();
    RtcDateTime compiled = RtcDateTime(__DATE__, __TIME__); // Копирование даты и времени в compiled
    // Rtc.SetDateTime(compiled);                              // Установка времени
    Serial.println(); // Отправка данных на последовательный порт
}

void loop()
{
    RtcDateTime now = Rtc.GetDateTime();

    int currentMinutes = now.Hour() * 60 + now.Minute();
    int currentDay = now.Year() * 366 + now.Day();

    // Сохраняем штатное расписание кормления.
    // Если мотор не работает и нет аварии — проверяем расписание.
    // Тест: через 15 секунд после включения Arduino включаем мотор на 10 секунд.
    if (!testMotorStarted && millis() - testStartTime >= MOTOR_TEST_DELAY)
    {
        testMotorStarted = true;
        motorState = true;
        digitalWrite(MOTOR_PIN, HIGH);
        Serial.println("=== TEST MOTOR ON ===");
        mySerial.println("=== TEST MOTOR ON ===");
    }

    if (testMotorStarted && motorState && millis() - testStartTime >= MOTOR_TEST_DELAY + MOTOR_TEST_TIME)
    {
        motorState = false;
        digitalWrite(MOTOR_PIN, LOW);
        Serial.println("=== TEST MOTOR OFF ===");
        mySerial.println("=== TEST MOTOR OFF ===");
    }

    // Измеряем и выводим данные каждые 200 мс.
    if (millis() - lastSensorRead >= SENSOR_INTERVAL)
    {
        lastSensorRead = millis();
        printSensorData();

        Serial.print("time: ");
        Serial.print(now.Hour());
        Serial.print(":");
        if (now.Minute() < 10)
            Serial.print("0");
        Serial.print(now.Minute());
        Serial.print(":");
        if (now.Second() < 10)
            Serial.print("0");
        Serial.println(now.Second());

        mySerial.print("time: ");
        mySerial.print(now.Hour());
        mySerial.print(":");
        if (now.Minute() < 10)
            mySerial.print("0");
        mySerial.print(now.Minute());
        mySerial.print(":");
        if (now.Second() < 10)
            mySerial.print("0");
        mySerial.println(now.Second());
    }

    // с модуля на комп
    if (mySerial.available())
    {
        String data = mySerial.readString();
        if (data.startsWith("start"))
        {
        }
    }
}
