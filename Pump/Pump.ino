/*
	Данный код превратит ATtiny85/ATtiny13 в спящий таймер.
	Через каждые INTERVAL секунд система подаёт 5 вольт на пин MOS_PIN в течении WORK секунд (т.е. с периодичностью PERIOD секунд).
	Всё время, кроме переключения пина, система спит и потребляет 28 микроампер.

                RST-|==V==|-VCC  -> BTN
  ADJUST ->  A3/PB3-|     |-PB2  -> HB_OK
  SENSOR ->     PB4-|     |-PB1  -> HB_ERR
                GND-|=====|-PB0  -> MOS/BTN

  SENSOR: [SENSOR_PIN | GND]       - проверка наличия воды перед включением помпы
  ADJUST: [GND | ADJUST_PIN | VCC] - подстройка времени работы +/- WORK_AJUST
  HB:     [GND | HB_OK | HB_ERR]   - светодиод индикации питания/ошибки
  BTN:    [VCC | MOS_PIN]          - кнопка принудительного запуска
  

    Copyright (C) 2021 Alexey Silichenko (a.silichenko@gmail.com)
	
	This is modified version of https://github.com/AlexGyver/Auto_Pump_Sleep by AlexGyver

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
    USA
*/
#define ATTINY13

#ifdef ATTINY13
#define F_CPU 1200000UL
#endif

#include <avr/wdt.h>
#include <avr/sleep.h>
#include <avr/io.h>

/*
 * Config section
 */

#define MODE_BTN_PUMP

// Таймер вачдога работает неточно, поэтому необходимо отдельно вычислить поправочный коэффициент
// для каждого конкретного МК, чтобы переводить секунды в тики
#define TIMER_CORR_tiny13   81275
#define TIMER_CORR_tiny85   95100
#define SCALE 100000        // использование типа float съедает много памяти, поэтому используем дополнительную переменную для хранения отрицательного порядка
#define TIMER_CORR_PERFECT  SCALE

#ifdef ATTINY13
#define TIMER_CORR          TIMER_CORR_tiny13
#else 
#define TIMER_CORR          TIMER_CORR_tiny85
#endif
//#define TIMER_CORR          TIMER_CORR_PERFECT

uint64_t timerCorr = SCALE;


#define PERIOD_M  60
#define PERIOD_H  (60 * PERIOD_M)
#define PERIOD_D  (24 * PERIOD_H)
/* ->|      PERIOD      |<-
 *   [[----------][****]]
 *   [[ INTERVAL ][WORK]]
 * tick number = sec * corr
 * period in ticks = period in sec
 */

#define PERIOD_1  10
#define WORK_1    3
 
// Периодичность срабатывания в секундах
#define PERIOD    ((uint64_t)PERIOD_1 * TIMER_CORR / SCALE)
// Время работы в тиках
#define WORK      (WORK_1 * TIMER_CORR / SCALE)
// Интервал ожидания между срабатываниями в тиках: период переводим в тики и отнимаем количество тиков работы
#define INTERVAL  (PERIOD - WORK)


#define MOS_PIN             PB0     // пин мосфета
//#define SENSOR_PIN          PB4     // пин датчика наличия воды (если не используется - закомментируй)

//#define WORK_AJUST_PIN      A3     // пин подстройки длительности полива (если не используется - закомментируй)
//#define WORK_AJUST_MIN      50     // минимальное значение подстройки в процентах
//#define WORK_AJUST_MAX      150    // максимальное значение подстройки в процентах

#define HEARTBEAT_OK_PIN    PB1
#define HEARTBEAT_ERR_PIN   PB2
#define HB_PERIOD           2//8


/*
 * end of config section
 */

// ATtiny13
#ifndef WDIE
#define WDIE WDTIE
#endif

// АЦП
#define adc_disable()   bitClear(ADCSRA, ADEN)    // disable ADC (before power-off)
#define adc_enable()    bitSet(ADCSRA, ADEN)      // re-enable ADC
#define adc_not_ready() bit_is_set(ADCSRA, ADSC)  // check if ADC ready


volatile uint32_t mainTimer, myTimer;
volatile bool isWorking = false;
volatile bool extraWork = false;
volatile bool errFlag = false;  // если что-то пошло не так (вода закончилась)

long workDuration = WORK,
     intervalDuration = INTERVAL;

void setup() {
  adc_disable();                        // отключить АЦП (экономия энергии)
  setupPins();
  setupInterrupts();
  setupWatchdog();
  sei();                                // разрешаем прерывания
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);  // максимальный сон
}

void setupPins() {
  // все пины как входы, экономия энергии
  for (byte i = 0; i < 6; i++) {
    if (i < 5) digitalWrite(i, LOW);
    pinMode(i, INPUT);
  }
}

void setupWatchdog() {
  wdt_reset();            // инициализация ватчдога
  wdt_enable(WDTO_1S);    // разрешаем ватчдог
  // 15MS, 30MS, 60MS, 120MS, 250MS, 500MS, 1S, 2S, 4S, 8S
  // WDIE (Watchdog Interrupt Enable): бит разрешения прерываний от таймера. 
  // если установлен в 1, то прерывания разрешены, если в 0 – запрещены.
  bitSet(WDTCR, WDIE);     // разрешаем прерывания по ватчдогу. Иначе будет резет.
}

void setupInterrupts() {
  bitSet(GIMSK, PCIE); // Разрешаем внешние прерывания PCINT0.
  // Установкой битов PCINT0-5 регистра PCMSK можно задать 
  // при изменении состояния каких входов будет срабатывать прерывание PCINT0
  bitSet(PCMSK, MOS_PIN);
}

void loop() {
  if(!extraWork) {
    if (!isWorking && isTimeUp(intervalDuration)) {
      resetTimer();
      if (sensorCheck()) startWork();
    } else if (isWorking && (isTimeUp(workDuration) || !sensorCheck())) {
      resetTimer();
      stopWork();
    }
  }

  goSleep();
}

bool isTimeUp(long duration) {
  return (mainTimer - myTimer) >= duration;
}

/**
 * Обработчик прерываний вачдога
 */
ISR (WDT_vect) {
  mainTimer++;
  bitSet(WDTCR, WDIE); // разрешаем прерывания по ватчдогу. Иначе будет реcет.
  heartbeat();
}

/**
 * Мигаем светодиодом раз в HB_PERIOD секунд
 */
void heartbeat() {
  // reset leds if err flag changed since last HB
  setPin(HEARTBEAT_OK_PIN, LOW);
  setPin(HEARTBEAT_ERR_PIN, LOW);

  if (0 == (mainTimer % HB_PERIOD)) setPin(errFlag ? HEARTBEAT_ERR_PIN : HEARTBEAT_OK_PIN, HIGH);
}

void setPin(byte pin, byte val) {
  if (LOW == val) {
    digitalWrite(pin, LOW);
    pinMode(pin, INPUT);
  } else {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH);
  }
}

/**
 * Выводы PCINT0,PCINT1,PCINT2,PCINT3,PCINT4,PCINT5
 * генерируют одно и то же прерывание - PCINT0
 */
ISR (PCINT0_vect) {
  extraWork = (HIGH == digitalRead(MOS_PIN) && !isWorking); // кнопка считается нажатой, если на пине высокий уровень сигнала, но он не был поднят программно
}

bool sensorCheck() {
  bool retval = true;
#ifdef SENSOR_PIN
  pinMode(SENSOR_PIN, INPUT_PULLUP);
  retval = LOW == digitalRead(SENSOR_PIN);
  pinMode(SENSOR_PIN, INPUT);
#endif
  errFlag = !retval;
  return retval;
}

void resetTimer() {
  myTimer = mainTimer;
}

void startWork() {
  isWorking = true;
  pinMode(MOS_PIN, OUTPUT);
  digitalWrite(MOS_PIN, HIGH);
  adjustWorkDuration();
}

void adjustWorkDuration() {
#ifdef WORK_AJUST_PIN
  adc_enable();
  while(adc_not_ready());
  int val = analogRead(WORK_AJUST_PIN);
  adc_disable();

  int adjustPercent = map(val, 0, 1023, WORK_AJUST_MIN, WORK_AJUST_MAX);
  workDuration = WORK * adjustPercent / 100;
  intervalDuration = PERIOD - workDuration;
#endif
}

void stopWork() {
  isWorking = false;
  digitalWrite(MOS_PIN, LOW);
  pinMode(MOS_PIN, INPUT);
}

void goSleep() {
  sleep_enable();       // Set the SE (sleep enable) bit. 
  sleep_bod_disable();  // Recommended practice is to disable the BOD
  sleep_cpu();          // Put the device into sleep mode. The SE bit must be set beforehand, and it is recommended to clear it afterwards. 
  // вышли из сна
  sleep_disable();      // Clear the SE (sleep enable) bit.
}
