#include <Arduino.h>
// Либа для устранения дребезга
#include <Bounce2.h>
// Либа для LCD
#include <LiquidCrystal_I2C.h>
// Память
#include <EEPROM.h>
// RTC
#include <RtcDS1302.h>

ThreeWire myWire(9, 8, 10); // DAT, CLK, RST 7 6 8 -> 9 8 10
RtcDS1302<ThreeWire> Rtc(myWire);

// Пины кнопок
#define BUTTON_PIN_UP 7
#define BUTTON_PIN_DOWN 6
#define BUTTON_PIN_LEFT 5
#define BUTTON_PIN_RIGHT 4

// Пины для TB6600
#define STEP_PIN 11 // PUL
#define DIR_PIN 12	// DIR
#define ENA_PIN 13	// ENA

// Обьекты Bounce
Bounce button_up = Bounce();
Bounce button_down = Bounce();
Bounce button_left = Bounce();
Bounce button_right = Bounce();

// LCD
LiquidCrystal_I2C lcd(0x27, 20, 4); // I2C address 0x27, 20 column and 4 rows
// Символы
//  byte rus_char_D[] = {
//    B00011,
//    B00101,
//    B00101,
//    B01001,
//    B01001,
//    B11111,
//    B10001,
//    B00000
//  };

// byte rus_char_t[] = {
//   B00000,
//   B00000,
//   B01110,
//   B00100,
//   B00100,
//   B00100,
//   B00100,
//   B00000
// };

// byte rus_char_ya[] = {
//   B00000,
//   B00000,
//   B00110,
//   B01010,
//   B00110,
//   B01010,
//   B01010,
//   B00000
// };

// byte rus_char_d[] = {
//   B00000,
//   B00000,
//   B00110,
//   B01010,
//   B01010,
//   B11110,
//   B10010,
//   B00000
// };

// byte rus_char_n[] = {
//   B00000,
//   B00000,
//   B10010,
//   B10010,
//   B11110,
//   B10010,
//   B10010,
//   B00000
// };

// byte rus_char_i[] = {
//   B00000,
//   B00000,
//   B10010,
//   B10010,
//   B10110,
//   B11010,
//   B10010,
//   B00000
// };

// byte rus_char_l[] = {
//   B00000,
//   B00000,
//   B01111,
//   B01001,
//   B01001,
//   B01001,
//   B10001,
//   B00000
// };

// byte rus_char_g[] = {
//   B00000,
//   B00000,
//   B11100,
//   B10000,
//   B10000,
//   B10000,
//   B10000,
//   B00000
// };

int line = 0;
int tab = 0;

int initInt = 0;

String options_to_print[5][3]{
	{"_TIME_", "_LEFT_", "_BAR_"},
	{"Date:", "Time:", ""},
	{"Wake:", "Sleep:", ""},
	{"Feeds:", "Revolutions:", "Vibro:"},
	{"Save", "Load", ""}
	// Дата Время       Дтя
	// Утро Сон  Еда    дн
	// Корм Кило Вибро  ил
	// Слот Сохр! Загр! г
};

int option_values[2][3]{
	{12, 44, 0},
	{3, 2, 5}};

// Переменные для защиты от повторного срабатывания
int lastFeedMinute = -1;
int lastDate = -1;

const int stepsPerRevolution = 200; // для NEMA23 1.8° (200 шагов/оборот)
int stepDelay = 1000;				// микросекунды между шагами (чем меньше, тем быстрее)

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

// Проверяет, нужно ли кормить в текущий момент
// Авторство - Deepseek
bool checkAndFeed(int currentMinutes, int currentDay)
{
	int feedCount = option_values[1][0];
	int morningTime = option_values[0][0];
	int eveningTime = option_values[0][1];
	// Если кормлений нет — выходим
	if (feedCount <= 0)
		return false;

	int morningMin = halfHoursToMinutes(morningTime);
	int eveningMin = halfHoursToMinutes(eveningTime);

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

void feed()
{
	lcd.clear();
	lcd.setCursor(2, 1);
	lcd.print("FEEDING IN");
	lcd.setCursor(4, 2);
	lcd.print("PROGRESS");
	int feedRevs = option_values[1][1];

	for (int i = 0; i < stepsPerRevolution * feedRevs; i++)
	{
		digitalWrite(STEP_PIN, HIGH);
		delayMicroseconds(stepDelay);
		digitalWrite(STEP_PIN, LOW);
		delayMicroseconds(stepDelay);
	}

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

	// Включаем драйвер (активный LOW для ENA-)
	digitalWrite(ENA_PIN, LOW);

	// Начальное направление
	digitalWrite(DIR_PIN, HIGH); // ПОМЕНЯТЬ НА LOW ДЛЯ ВРАЩЕНИЯ В ДРУГУЮ СТОРОНУ

	Serial.begin(9600);		  // Установка последовательной связи на скорости 9600
	Serial.print("Data: ");	  // Отправка данных на последовательный порт
	Serial.println(__DATE__); // Получение даты и времени с ПК
	Serial.print("Time: ");	  // Отправка данных на последовательный порт
	Serial.println(__TIME__); // Получение даты и времени с ПК
	// Инициализация RTC
	Rtc.Begin();
	RtcDateTime compiled = RtcDateTime(__DATE__, __TIME__); // Копирование даты и времени в compiled
	Rtc.SetDateTime(compiled);								// Установка времени
	Serial.println();										// Отправка данных на последовательный порт

	// Настройка Bounce
	button_up.attach(BUTTON_PIN_UP, INPUT_PULLUP); // USE INTERNAL PULL-UP
	button_up.interval(10);						   // интервал в миллисеках
	button_down.attach(BUTTON_PIN_DOWN, INPUT_PULLUP);
	button_down.interval(10);
	button_left.attach(BUTTON_PIN_LEFT, INPUT_PULLUP);
	button_left.interval(10);
	button_right.attach(BUTTON_PIN_RIGHT, INPUT_PULLUP);
	button_right.interval(10);
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

	// Update the Bounce instance
	button_up.update();
	button_down.update();
	button_left.update();
	button_right.update();

	// <Bounce>.changed() RETURNS true IF THE STATE CHANGED (FROM HIGH TO LOW OR LOW TO HIGH)
	if (button_up.changed())
	{
		if (button_up.read() == HIGH)
		{
			line--;
			line = constrain(line, 0, 3);
		}
	}

	if (button_down.changed())
	{
		if (button_down.read() == HIGH && tab > 0)
		{
			line++;
			line = constrain(line, 0, 3);
		}
	}

	if (button_left.changed())
	{
		if (button_left.read() == HIGH)
		{
			if (line == 0 && tab > 0)
			{
				tab--;
				tab = constrain(tab, 0, 4);
			}
			else if (tab > 1 && tab != 4)
			{
				option_values[tab - 2][line - 1]--;
			}
		}
	}

	if (button_right.changed())
	{
		if (button_right.read() == HIGH)
		{
			if (line == 0 && tab < 4)
			{
				tab++;
				tab = constrain(tab, 0, 4);
			}
			else if (tab > 1 && tab != 4)
			{
				option_values[tab - 2][line - 1]++;
			}
			else if (tab == 4 && line == 1)
			{ // save
				int wake = option_values[0][0];
				int sleep = option_values[0][1];
				int feeds = option_values[1][0];
				int kilos = option_values[1][1];
				int vibro = option_values[1][2];
				EEPROM.put(0 * 4, wake);
				EEPROM.put(1 * 4, sleep);
				EEPROM.put(2 * 4, feeds);
				EEPROM.put(3 * 4, kilos);
				EEPROM.put(4 * 4, vibro);
			}
			else if (tab == 4 && line == 2)
			{ // load
				int wake = 0;
				int sleep = 0;
				int feeds = 0;
				int kilos = 0;
				int vibro = 0;
				EEPROM.get(0 * 4, wake);
				EEPROM.get(1 * 4, sleep);
				EEPROM.get(2 * 4, feeds);
				EEPROM.get(3 * 4, kilos);
				EEPROM.get(4 * 4, vibro);
				option_values[0][0] = wake;
				option_values[0][1] = sleep;
				option_values[1][0] = feeds;
				option_values[1][1] = kilos;
				option_values[1][2] = vibro;
			}
		}
	}
	// int option_values[2][3]{
	// {12, 44, 0},
	// {3, 2, 5}};

	if (option_values[0][0] < 0)
	{
		option_values[0][0] = 0;
	}
	if (option_values[0][0] > 46)
	{
		option_values[0][0] = 46;
	}

	if (option_values[0][1] < 2)
	{
		option_values[0][1] = 2;
	}
	if (option_values[0][1] > 46)
	{
		option_values[0][1] = 46;
	}

	// 10 + 1 > 2
	// 2 -> 10
	// 10 + 1 > 10
	if (option_values[0][0] + 1 > option_values[0][1])
	{
		option_values[0][1] = option_values[0][0] + 1;
	}

	if (option_values[1][0] < 0)
	{
		option_values[1][0] = 0;
	}
	if (option_values[1][0] > 20)
	{
		option_values[1][0] = 20;
	}

	if (option_values[1][1] < 0)
	{
		option_values[1][1] = 0;
	}
	if (option_values[1][1] > 255)
	{
		option_values[1][1] = 255;
	}

	if (option_values[1][2] < 0)
	{
		option_values[1][2] = 0;
	}
	if (option_values[1][2] > 10)
	{
		option_values[1][2] = 10;
	}

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
		lcd.print(" i  &  @  #  s");
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

			if (tab > 1 && tab != 4)
			{
				if (tab == 2)
				{
					lcd.setCursor(options_to_print[tab][i].length() + 1, i + 1);
					lcd.print(halfHoursToTime(option_values[tab - 2][i]));
				}
				else
				{
					lcd.setCursor(options_to_print[tab][i].length() + 1, i + 1);
					lcd.print(option_values[tab - 2][i]);
				}
			}
			if (tab == 4 && button_right.changed())
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
		lcd.setCursor(1, 1);
		lcd.print("Time:");
		lcd.print(now.Hour());
		lcd.print(":");
		lcd.print(now.Minute());
		lcd.print(":");
		lcd.print(now.Second());

		// lcd.setCursor(1, 2);
		// lcd.print("Next feed:");
		// lcd.print(now.Hour());
		// lcd.print(":");
		// lcd.print(now.Minute());
		// lcd.print(":");
		// lcd.print(now.Second());
	}
}