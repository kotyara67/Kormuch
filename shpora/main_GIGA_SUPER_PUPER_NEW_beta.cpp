// НАСТРОЙКА ПАРАМЕТРОВ ПОВЕДЕНИЯ

int time_morning = 8 * 2; // В получасах. 12 = 6 утра
int time_sleep = 19 * 2;  // Я хз что будет, если выставить time_morning <= time_sleep. Проверять не рекомендуется.

int option_values[2]{
    2, // кол-во кормлений
    4, // килограммы (в теории)
};

int countFeedsOverall = 0;

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

int minutesUntilNextFeed(int currentMinutes)
{
    int morningMinutes = halfHoursToMinutes(time_morning);
    int eveningMinutes = halfHoursToMinutes(time_sleep);
    int feedCount = option_values[0];
    if (feedCount <= 0)
        return 0; // нет кормлений

    if (feedCount == 1)
    {
        // одно кормление — всегда утром
        if (currentMinutes < morningMinutes)
            return morningMinutes - currentMinutes;
        else
            return (morningMinutes + 1440) - currentMinutes; // завтра
    }

    // Расчитываем интервал (в минутах)
    int interval = (eveningMinutes - morningMinutes) / (feedCount - 1);

    // Ищем первое кормление, которое строго позже текущего времени
    for (int i = 0; i < feedCount; i++)
    {
        int feedTime = morningMinutes + i * interval;
        if (feedTime > currentMinutes)
        {
            return feedTime - currentMinutes;
        }
    }

    // Если все сегодняшние прошли — следующее завтра утром
    return (morningMinutes + 1440) - currentMinutes;
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

// ===== ACS712-30A =====
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

void emergencyMotorOff()
{
    digitalWrite(MOTOR_PIN, LOW);
    motorOvercurrent = true;

    Serial.println("!!! OVERCURRENT - MOTOR OFF !!!");
    mySerial.println("!!! OVERCURRENT - MOTOR OFF !!!");
}

bool checkMotorCurrent()
{
    float current = abs(readCurrentAverage());

    if (current >= OVERCURRENT_LIMIT)
    {
        if (overcurrentStart == 0)
            overcurrentStart = millis();

        if (millis() - overcurrentStart >= OVERCURRENT_DELAY)
        {
            emergencyMotorOff();
            return false;
        }
    }
    else
    {
        overcurrentStart = 0;
    }

    return true;
}

bool printMotorInfo = false;
bool printTimeInfo = false;

void printMotorState()
{

    static unsigned long lastReport = 0;

    int reportDelay = 1000;
    if (printMotorInfo)
    {
        reportDelay = 200;
    }

    if (millis() - lastReport >= reportDelay)
    {
        lastReport = millis();

        if (printMotorInfo)
        {
            int adc = analogRead(ACS712_PIN);
            float voltage = adc * (ADC_REFERENCE / 1023.0);
            float current = abs(readCurrentAverage());
            bool motorState = (digitalRead(MOTOR_PIN) == HIGH);

            Serial.print("ADC: ");
            Serial.print(adc);
            Serial.print(" | Voltage: ");
            Serial.print(voltage, 3);
            Serial.print(" V | Current: ");
            Serial.print(current, 2);
            Serial.print(" A | Motor: ");
            Serial.print(motorState ? "ON" : "OFF");

            mySerial.print("ADC: ");
            mySerial.print(adc);
            mySerial.print(" | Voltage: ");
            mySerial.print(voltage, 3);
            mySerial.print(" V | Current: ");
            mySerial.print(current, 2);
            mySerial.print(" A | Motor: ");
            mySerial.print(motorState ? "ON" : "OFF");

            if (motorOvercurrent)
                Serial.print(" | FAULT: OVERCURRENT");
            mySerial.print(" | FAULT: OVERCURRENT");
        }

        if (printTimeInfo)
        {
            RtcDateTime now = Rtc.GetDateTime();
            int currentTotalMinutes = now.Hour() * 60 + now.Minute();
            int remain = minutesUntilNextFeed(currentTotalMinutes);
            int hrs = remain / 60;
            int mins = remain % 60;
            int secs = 59 - now.Second();

            Serial.print(" | Time: ");
            Serial.print(now.Hour());
            Serial.print(":");
            if (now.Minute() < 10)
                Serial.print("0");
            Serial.print(now.Minute());
            Serial.print(":");
            if (now.Second() < 10)
                Serial.print("0");
            Serial.println(now.Second());
            Serial.print("Следующее кормление через: ");
            Serial.print(hrs);
            Serial.print(":");
            Serial.print(mins);
            Serial.print(":");
            Serial.println(secs);

            mySerial.print(" | Time: ");
            mySerial.print(now.Hour());
            mySerial.print(":");
            if (now.Minute() < 10)
                mySerial.print("0");
            mySerial.print(now.Minute());
            mySerial.print(":");
            if (now.Second() < 10)
                mySerial.print("0");
            mySerial.println(now.Second());

            mySerial.print("Следующее кормление через: ");
            mySerial.print(hrs);
            mySerial.print(":");
            mySerial.print(mins);
            mySerial.print(":");
            mySerial.println(secs);
        }
    }
}

void feed()
{
    printMotorInfo = true;
    if (motorOvercurrent)
    {
        Serial.println("MOTOR BLOCKED: overcurrent fault");
        mySerial.println("MOTOR BLOCKED: overcurrent fault");
        return;
    }

    digitalWrite(MOTOR_PIN, HIGH);

    unsigned long feedTime = 10UL * 1000UL * option_values[1];
    unsigned long start = millis();

    while (millis() - start < feedTime)
    {
        if (!checkMotorCurrent())
            return;

        if (mySerial.available())
        {
            String data = mySerial.readString();
            if (data.startsWith("stop"))
            {
                digitalWrite(MOTOR_PIN, LOW);
                return;
            }
        }
        printMotorState();
        delay(10);
    }

    printMotorInfo = false;
    digitalWrite(MOTOR_PIN, LOW);
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
    // RtcDateTime compiled = RtcDateTime(__DATE__, __TIME__); // Копирование даты и времени в compiled
    Serial.println(); // Отправка данных на последовательный порт

    Serial.println("ACS712 calibration: MOTOR OFF");
    mySerial.println("ACS712 calibration: MOTOR OFF");
    calibrateACS712();

    EEPROM.get(0 * 4, option_values[1]);
    EEPROM.get(1 * 4, countFeedsOverall);

    Serial.print("EEPROM memory loaded. feeds: ");
    Serial.println(option_values[1]);
    Serial.print("countDeedsOverall: ");
    Serial.println(countFeedsOverall);
    mySerial.print("EEPROM memory loaded. feeds: ");
    mySerial.println(option_values[1]);
    mySerial.print("countDeedsOverall: ");
    mySerial.println(countFeedsOverall);

    Serial.println("System ready.");
    mySerial.println("System ready.");
}

void loop()
{
    RtcDateTime now = Rtc.GetDateTime();

    int currentMinutes = now.Hour() * 60 + now.Minute();
    int currentDay = now.Year() * 366 + now.Day();

    if (!motorOvercurrent && checkAndFeed(currentMinutes, currentDay))
    {
        EEPROM.get(1 * 4, countFeedsOverall);
        countFeedsOverall++;
        EEPROM.put(1 * 4, countFeedsOverall);
        feed();
    }

    printMotorState();

    if (mySerial.available())
    {
        String data = mySerial.readString();
        if (data.startsWith("help"))
        {
            mySerial.println("start - тестовое кормление");
            mySerial.println("stop - экстренное отключение кормления");
            mySerial.println("print motor - вкыл/выкл вывода информации о датчике мотора");
            mySerial.println("print time - вкыл/выкл вывода информации с модуля времени");
            mySerial.println("set time #HH:MM:SS - Задать время модулю. Пример использования: time set #11:30:15, это 11 часов, 30 минут, 15 секунд");
            mySerial.println(data.length());
        }
        else if (data.startsWith("start"))
        {
            feed();
        }
        else if (data.startsWith("print motor"))
        {
            printMotorInfo = !printMotorInfo;
        }
        else if (data.startsWith("print time"))
        {
            printTimeInfo = !printTimeInfo;
        }
        else if (data.startsWith("print feeds"))
        {
            mySerial.print("option_values[1]: ");
            mySerial.println(option_values[1]);
            mySerial.print("feedsOverall");
            mySerial.println(countFeedsOverall);
        }
        else if (data.startsWith("reset EEPROM"))
        {
            EEPROM.put(0 * 4, 0);
            EEPROM.put(1 * 4, 0);
        }
        else if (data.startsWith("set feeds"))
        {
            if (data.indexOf("#") != -1)
            {
                int paramIndex = data.indexOf("#");
                String dataFeeds = data.substring(paramIndex + 1, paramIndex + 2);
                int feeds = dataFeeds.toInt();
                if (feeds > 9)
                {
                    mySerial.println("Ошибка, значение большьше чем 9.");
                }
                else
                {
                    mySerial.println("Запись в EEPROM...");
                    option_values[1] = feeds;
                    EEPROM.put(0 * 4, feeds);
                    mySerial.print("Значение \"");
                    mySerial.print(option_values[1]);
                    mySerial.print("\" записано!");
                }
            }
        }
        else if (data.startsWith("set time")) // set time #**:**:** 20 с учетом /r/n
        {
            if (data.indexOf("#") != -1)
            {
                int paramIndex = data.indexOf("#");
                String dataTime = data.substring(paramIndex + 1, paramIndex + 9);
                if (dataTime.length() < 8)
                {
                    mySerial.println("Команда введена не верно! Отсутсвует делитель \"#\"");
                    mySerial.println("set time #HH:MM:SS - Задать время модулю. Пример использования: time set #11:30:15, это 11 часов, 30 минут, 15 секунд");
                }
                char charDataTime[8];
                dataTime.toCharArray(charDataTime, sizeof(charDataTime));
                RtcDateTime RtcTime = RtcDateTime(__DATE__, charDataTime);
                Rtc.SetDateTime(RtcTime);
                mySerial.print("Время установлено на ");
                mySerial.println(charDataTime);
            }
            else
            {
                mySerial.println("Команда введена не верно! Отсутсвует делитель \"#\"");
                mySerial.println("set time #HH:MM:SS - Задать время модулю. Пример использования: time set #11:30:15, это 11 часов, 30 минут, 15 секунд");
            }
        }
    }
}
