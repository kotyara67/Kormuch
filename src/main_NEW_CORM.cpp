// НАСТРОЙКА ПАРАМЕТРОВ ПОВЕДЕНИЯ

int time_morning = 8 * 2; // В получасах. 12 = 6 утра
int time_sleep = 19 * 2;  // Я хз что будет, если выставить time_morning <= time_sleep. Проверять не рекомендуется.
// int mins_to_afk = 10;  // период неактивности

int option_values[3]{
    3, // кол-во кормлений
    5, // килограммы (в теории)

    0 // ОНО ТУТ НАДО. из интерфейса кормушки можно случийно получить доступ к третьей опции, хотя у нас ее нет.
      // Однако программа все равно попытается изменить значение вне диапозона массива. 0 предотвращает краш программы.
};

#include <Arduino.h>
// Память
// #include <EEPROM.h>
// RTC
#include <RtcDS1302.h>

ThreeWire myWire(9, 8, 10); // DAT, CLK, RST
RtcDS1302<ThreeWire> Rtc(myWire);

// Пины для TB6600
#define STEP_PIN 10 // PUL
#define DIR_PIN 11  // DIR
#define ENA_PIN 12  // ENA

// Пин "АФК" транзистора
// #define STEP_PIN 13 // AFK

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

int stepDelay = 50; // микросекунды между шагами (чем меньше, тем быстрее)

void feed()
{
    delay(2);
    digitalWrite(ENA_PIN, LOW);
    delay(2);                                // 👇 вот это значение - коэффицент.
    int feedRevs = option_values[1] * 160.0; // время кормления 50.0 ~ 5 минут при значении 50 кг

    for (long i = 0; i < (long)stepsPerRevolution * feedRevs; i++)
    {
        digitalWrite(STEP_PIN, HIGH);
        delayMicroseconds(stepDelay);
        digitalWrite(STEP_PIN, LOW);
        delayMicroseconds(stepDelay);
    }

    digitalWrite(ENA_PIN, HIGH); // отключить драйвер (хз вообще нужно оно тут или нет, разницы вроде никакой не должно быть. Но Если оно работает - трогать не стоит)
}

void setup()
{

    // пины для драйвера
    pinMode(STEP_PIN, OUTPUT);
    pinMode(DIR_PIN, OUTPUT);
    pinMode(ENA_PIN, OUTPUT);

    // Драйвер отключен по умолчанию (активный LOW)
    digitalWrite(ENA_PIN, HIGH);

    // Начальное направление
    digitalWrite(DIR_PIN, LOW); // ПОМЕНЯТЬ НА LOW ДЛЯ ВРАЩЕНИЯ В ДРУГУЮ СТОРОНУ

    Serial.begin(9600);       // Установка последовательной связи на скорости 9600
    Serial.print("Data: ");   // Отправка данных на последовательный порт
    Serial.println(__DATE__); // Получение даты и времени с ПК
    Serial.print("Time: ");   // Отправка данных на последовательный порт
    Serial.println(__TIME__); // Получение даты и времени с ПК
    // Инициализация RTC
    Rtc.Begin();
    RtcDateTime compiled = RtcDateTime(__DATE__, __TIME__); // Копирование даты и времени в compiled
    // Rtc.SetDateTime(compiled); // не сбрасывать время при каждом запуске								// Установка времени
    Serial.println(); // Отправка данных на последовательный порт
}

int tick = 0;

void loop()
{

    // Rtc.GetDateTime();
    RtcDateTime now = Rtc.GetDateTime();

    int currentMinutes = now.Hour() * 60 + now.Minute();
    int currentDay = now.Year() * 366 + now.Day();

    feed();

    // if (checkAndFeed(currentMinutes, currentDay))
    // {
    //     feed();
    // }
}