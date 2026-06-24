// НАСТРОЙКА ПАРАМЕТРОВ ПОВЕДЕНИЯ

int time_morning = 12; // В получасах. 12 = 6 утра
int time_sleep = 44;   // Я хз что будет, если выставить time_morning <= time_sleep. Проверять не рекомендуется.
// int mins_to_afk = 10;  // период неактивности

int option_values[3]{
	3, // кол-во кормлений
	5, // килограммы (в теории)

	0 // ОНО ТУТ НАДО. из интерфейса кормушки можно случийно получить доступ к третьей опции, хотя у нас ее нет.
	  // Однако программа все равно попытается изменить значение вне диапозона массива. 0 предотвращает краш программы.
};

#include <Arduino.h>
// Либа для устранения дребезга
#include <Bounce2.h>
// Либа для LCD
#include <LiquidCrystal_I2C.h>
// Память
#include <EEPROM.h>
// RTC
#include <RtcDS1302.h>

ThreeWire myWire(9, 8, 10); // DAT, CLK, RST
RtcDS1302<ThreeWire> Rtc(myWire);

// Пины кнопок
#define BUTTON_PIN_UP 7
#define BUTTON_PIN_DOWN 6
#define BUTTON_PIN_LEFT 5
#define BUTTON_PIN_RIGHT 4

// Пины для TB6600
#define STEP_PIN 11 // PUL
#define DIR_PIN 12	// DIR
#define ENA_PIN 3	// ENA

// Пин "АФК" транзистора
// #define STEP_PIN 13 // AFK

// Обьекты Bounce
Bounce button_up = Bounce();
Bounce button_down = Bounce();
Bounce button_left = Bounce();
Bounce button_right = Bounce();

// LCD
LiquidCrystal_I2C lcd(0x27, 20, 4); // I2C address 0x27, 20 column and 4 rows

int line = 0;
int tab = 0;

int initInt = 0;

// static uint32_t afkTimer = 0;

String options_to_print[3][3]{
	{"Time:", "Next feed:", ""},
	{"Feeds:", "Kilos:", ""},
	{"Save", "Load", "TEST FEED"}
	// Дата Время       Дтя
	// Утро Сон  Еда    дн
	// Корм Кило Вибро  ил
	// Слот Сохр! Загр! г
};

// Переменные для защиты от повторного срабатывания
int lastFeedMinute = -1;
int lastDate = -1;

const int stepsPerRevolution = 200; // для NEMA23 1.8° (200 шагов/оборот)

String halfHoursToTime(int halfHours)
{
	int totalMinutes = halfHours * 30;
	int hours = (totalMinutes / 60) % 24; // часы в пределах 0..23
	int minutes = totalMinutes % 60;	  // всегда 0 или 30
	return String(hours) + ":" + String(minutes);
}

// Переводит получасы в минуты от полуночи
int halfHoursToMinutes(int half)
{
	return half * 30;
}

// Авторство - DeepSeek
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

int stepDelay = 200; // микросекунды между шагами (чем меньше, тем быстрее)

void feed()
{
	digitalWrite(ENA_PIN, LOW);
	delay(2);
	lcd.clear();
	lcd.setCursor(2, 1);
	lcd.print("FEEDING IN");
	lcd.setCursor(4, 2);
	lcd.print("PROGRESS");
	int feedRevs = option_values[1];

	for (int i = 0; i < stepsPerRevolution * feedRevs; i++)
	{
		digitalWrite(STEP_PIN, HIGH);
		delayMicroseconds(stepDelay);
		digitalWrite(STEP_PIN, LOW);
		delayMicroseconds(stepDelay);
	}

	digitalWrite(ENA_PIN, HIGH); // отключить драйвер (хз вообще нужно оно тут или нет, разницы вроде никакой не должно быть. Но Если оно работает - трогать не стоит)

	initInt = 0;
}

void setup()
{

	lcd.init(); // initialize the lcd
	lcd.backlight();
	lcd.clear();

	// пины для драйвера
	pinMode(STEP_PIN, OUTPUT);
	pinMode(DIR_PIN, OUTPUT);
	pinMode(ENA_PIN, OUTPUT);

	// Драйвер отключен по умолчанию (активный LOW)
	digitalWrite(ENA_PIN, HIGH);

	// Начальное направление
	digitalWrite(DIR_PIN, LOW); // ПОМЕНЯТЬ НА LOW ДЛЯ ВРАЩЕНИЯ В ДРУГУЮ СТОРОНУ

	Serial.begin(9600);		  // Установка последовательной связи на скорости 9600
	Serial.print("Data: ");	  // Отправка данных на последовательный порт
	Serial.println(__DATE__); // Получение даты и времени с ПК
	Serial.print("Time: ");	  // Отправка данных на последовательный порт
	Serial.println(__TIME__); // Получение даты и времени с ПК
	// Инициализация RTC
	Rtc.Begin();
	RtcDateTime compiled = RtcDateTime(__DATE__, __TIME__); // Копирование даты и времени в compiled
	// Rtc.SetDateTime(compiled); // не сбрасывать время при каждом запуске								// Установка времени
	Serial.println(); // Отправка данных на последовательный порт

	// Настройка Bounce
	button_up.attach(BUTTON_PIN_UP, INPUT_PULLUP); // USE INTERNAL PULL-UP
	button_up.interval(10);						   // интервал в миллисеках
	button_down.attach(BUTTON_PIN_DOWN, INPUT_PULLUP);
	button_down.interval(10);
	button_left.attach(BUTTON_PIN_LEFT, INPUT_PULLUP);
	button_left.interval(10);
	button_right.attach(BUTTON_PIN_RIGHT, INPUT_PULLUP);
	button_right.interval(10);

	// load
	int feeds = 0;
	int kilos = 0;
	EEPROM.get(0 * 4, feeds);
	EEPROM.get(1 * 4, kilos);
	option_values[0] = feeds;
	option_values[1] = kilos;
}

int tick = 0;

void loop()
{

	// Rtc.GetDateTime();
	RtcDateTime now = Rtc.GetDateTime();

	int currentMinutes = now.Hour() * 60 + now.Minute();
	int currentDay = now.Year() * 366 + now.Day();

	if (checkAndFeed(currentMinutes, currentDay))
	{
		feed();
	}

	// if (millis() - afkTimer >= 10 * 60) // 10 минут
	// {
	// 	afkTimer = millis(); // сброс таймера
	// }

	// Update the Bounce instance
	button_up.update();
	button_down.update();
	button_left.update();
	button_right.update();

	// <Bounce>.changed() RETURNS true IF THE STATE CHANGED (FROM HIGH TO LOW OR LOW TO HIGH)
	// Кнопка ВВЕРХ
	if (button_up.changed())
	{
		if (button_up.read() == HIGH)
		{
			line--;
			line = constrain(line, 0, 3);
		}
	}

	// Кнопка ВНИЗ
	if (button_down.changed())
	{
		if (button_down.read() == HIGH && tab > 0)
		{
			line++;
			line = constrain(line, 0, 3);
		}
	}

	// ВЛЕВО
	if (button_left.changed())
	{
		if (button_left.read() == HIGH)
		{
			if (line == 0 && tab > 0)
			{
				tab--;
				tab = constrain(tab, 0, 2);
			}
			else if (tab == 1)
			{
				option_values[line - 1]--;
			}
		}
	}

	// ВПРАВО
	if (button_right.changed())
	{
		if (button_right.read() == HIGH)
		{
			if (line == 0 && tab < 2)
			{
				tab++;
				tab = constrain(tab, 0, 2);
			}
			else if (tab == 1)
			{
				option_values[line - 1]++;
			}
			else if (tab == 2)
			{
				// int option_values[2]{
				// 	3, // кол-во кормлений
				// 	5  // килограммы (в теории)
				// };
				if (line == 1)
				{
					// save
					int feeds = option_values[0];
					int kilos = option_values[1];
					EEPROM.put(0 * 4, feeds);
					EEPROM.put(1 * 4, kilos);
				}
				else if (line == 2)
				{
					// load
					int feeds = 0;
					int kilos = 0;
					EEPROM.get(0 * 4, feeds);
					EEPROM.get(1 * 4, kilos);
					option_values[0] = feeds;
					option_values[1] = kilos;
				}
				else if (line == 3)
				{
					feed();
				}
			}
		}
	}

	// int option_values[2]{
	// 	3,	//кол-во кормлений
	// 	5	//килограммы (в теории)
	// };
	// Держим значения параметров в допустимом диапазоне

	// На всякий случай
	tab = constrain(tab, 0, 2);
	line = constrain(line, 0, 3);

	// кол-во кормлений
	option_values[0] = constrain(option_values[0], 0, 50);

	// килограммы (в теории)
	option_values[1] = constrain(option_values[1], 0, 50);

	if ( // Если была отпущена любая кнопка
		(button_up.changed() && button_up.read() == HIGH) ||
		(button_down.changed() && button_down.read() == HIGH) ||
		(button_left.changed() && button_left.read() == HIGH) ||
		(button_right.changed() && button_right.read() == HIGH) ||
		initInt == 0)
	{

		initInt = 1;
		// LCD
		lcd.clear();
		lcd.setCursor(0, 0);
		lcd.print(" i  #  s");
		if (line == 0)
		{
			lcd.setCursor(tab * 3, 0);
			lcd.print(">");
		}
		else
		{
			lcd.setCursor(0, line);
			lcd.print(">");
		}

		// inside tab lines
		for (int i = 0; i < 3; i++)
		{
			if (options_to_print[tab][i] == "")
			{
				continue;
			}
			lcd.setCursor(1, i + 1);
			if (tab != 0)
			{
				lcd.print(options_to_print[tab][i]);
			}

			if (tab == 1)
			{
				lcd.setCursor(options_to_print[tab][i].length() + 1, i + 1);
				lcd.print(option_values[i]);
			}
			if (tab == 2 && button_right.changed())
			{
				if (line == 1)
				{
					lcd.setCursor(1, 3);
					lcd.print("Saved!");
				}
				else if (line == 2)
				{
					lcd.setCursor(1, 3);
					lcd.print("Loaded!");
				}
			}
		}
	}

	// Serial.print(tab);
	if (millis() % 1000 > 0 && millis() % 1000 < 100 && tab == 0)
	{
		char buf[9];
		sprintf(buf, "%02u:%02u:%02u", now.Hour(), now.Minute(), now.Second());
		lcd.setCursor(1, 1);
		lcd.print("Time:        ");
		lcd.setCursor(6, 1);
		lcd.print(buf);

		lcd.setCursor(1, 2);
		lcd.print("Next feed:");
		lcd.print(floor(minutesUntilNextFeed(now.Minute()) / 60));
		lcd.print(":");
		lcd.print(minutesUntilNextFeed(now.Minute()) / 60);
		lcd.print(":");
		lcd.print(60 - now.Second());
	}
}