[![](https://iarduino.ru/img/logo.svg)](https://iarduino.ru)[![](https://wiki.iarduino.ru/img/git-shop.svg?3)](https://iarduino.ru) [![](https://wiki.iarduino.ru/img/git-wiki.svg?2)](https://wiki.iarduino.ru) [![](https://wiki.iarduino.ru/img/git-lesson.svg?2)](https://lesson.iarduino.ru)[![](https://wiki.iarduino.ru/img/git-forum.svg?2)](http://forum.trema.ru)

# iarduino\_ACS712

**This is a library for Arduino IDE. It allows to work with [Current Sensor](https://iarduino.ru/shop/Sensory-Datchiki/datchik-toka-5a-trema-modul-v2-0.html), module made by iArduino.ru**

**Данная библиотека для Arduino IDE позволяет работать с модулем [Датчик тока 5А](https://iarduino.ru/shop/Sensory-Datchiki/datchik-toka-5a-trema-modul-v2-0.html) от [iArduino.ru](https://iarduino.ru)**

> Подробнее про установку библиотеки читайте в нашей [инструкции](https://wiki.iarduino.ru/page/Installing_libraries/).

> Подробнее про подключение к [Arduino UNO](https://iarduino.ru/shop/boards/arduino-uno-r3.html)/[Piranha UNO](https://iarduino.ru/shop/boards/piranha-uno-r3.html) читайте на нашей [wiki](https://wiki.iarduino.ru/page/current_sensor/#h3_3)


| Модель | Ссылка на магазин |
|---|---|
| <p>Датчик тока 5А</p> <img src="https://wiki.iarduino.ru/img/resources/927/927.svg" width="100px"></img>| https://iarduino.ru/shop/Sensory-Datchiki/datchik-toka-5a-trema-modul-v2-0.html |


## Назначение функций и переменных:

**Подробное описание работы с библиотекой и примеры смотрите на [нашем сайте](https://wiki.iarduino.ru/page/current_sensor/#h3_6)**

**Инициализация:**

```C++
#include <iarduino_ACS712.h> // Подключаем библиотеку для работы с Trema датчиком тока.
iarduino_ACS712 ОБЪЕКТ( ВЫВОД ); // Создаём объект указывая вывод к которому подключён датчик.
```

**Чтение постоянного тока** 

```C++
ОБЪЕКТ.readDC( [КОЭФФИЦИЕНТ] ); // Чтение постоянного тока, с указанием коэффициента сглаживания.
```

**Чтение переменного тока** 

```C++
ОБЪЕКТ.readAC( [КОЭФФИЦИЕНТ] ); // Чтение переменного тока, с указанием коэффициента сглаживания.
```

**Коррекция показаний DC** 

```C++
ОБЪЕКТ.calibrationDC( КОЭФФИЦИЕНТ ); // Коррекция показаний постоянного тока.
```

**Коррекция показаний AC** 

```C++
ОБЪЕКТ.calibrationAC( КОЭФФИЦИЕНТ ); // Коррекция показаний переменного тока.
```

**Установка коррекции DC** 

```C++
ОБЪЕКТ.setZeroVDC( НАПРЯЖЕНИЕ ); // Установка коррекции смещения нуля для постоянного тока.
```

**Установка коррекции AC** 

```C++
ОБЪЕКТ.setZeroVAC( НАПРЯЖЕНИЕ ); // Установка коррекции смещения нуля для переменного тока.
```

**Чтение постоянного напряжения** 

```C++
ОБЪЕКТ.getZeroVDC(); // Чтение постоянного напряжения на выходе модуля.
```

**Чтение переменного напряжения** 

```C++
ОБЪЕКТ.getZeroVAC(); // Чтение переменного напряжения на выходе модуля.
```

