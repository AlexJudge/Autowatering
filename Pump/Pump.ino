/*
	Данный код превратит ATtiny85/ATtiny13 в спящий таймер.
	Через каждые PERIOD секунд система подаёт 5 вольт на пин MOS_PIN в течении WORK секунд.
	Всё время, кроме переключения пина, система спит и потребляет 28 микроампер.

  ATtiny13 Uпит (min): 2.7 В
  ATtiny85 Uпит (min): 1.8 В
*/
#include <avr/wdt.h>
#include <avr/sleep.h>

/*
 * Config section
 */

// Таймер вачдога работает неточно, поэтому необходимо отдельно вычислить поправочный коэффициент
// для каждого конкретного МК, чтобы переводить секунды в тики
#define TIMER_CORR_tiny13   0.813960
#define TIMER_CORR_tiny85   0.953855
#define TIMER_CORR_PERFECT  1
#define TIMER_CORR          TIMER_CORR_tiny85

/* ->|     INTERVAL     |<-
 *   [[----------][****]]
 *   [[  PERIOD  ][WORK]]
 * tick number = sec * corr
 * period in ticks = period in sec
 */
// Интервал срабатывания в секундах (пример: 60*60*24*3 = 259200 - три дня)
#define INTERVAL  259200
// Время работы в тиках 30 (приблизительно равно секундам, поправочный коэффициент можно не использовать, если не критично)
#define WORK      30
// Период ожидания между срабатываниями в тиках: полный интервал переводим в тики и отнимаем количество тиков работы
#define PERIOD    (INTERVAL * TIMER_CORR - WORK)


#define SENSOR_PIN      PB0     // датчик наличия воды. если не используется - закомментируй
#define MOS_PIN         PB1     // пин мосфета

#define EXTRA_PUMP_PIN  PB2     // пин принудительного запуска

#define HEARTBEAT_PIN   PB4
#define HB_PERIOD       5

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
boolean isPumping = false;
volatile boolean extraPump = false;

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
  for (byte i = 0; i < 6; i++) pinMode(i, INPUT);
  // подтягиваем пин для работы с кнопкой
  pinMode(EXTRA_PUMP_PIN, INPUT_PULLUP);
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
  bitSet(PCMSK, EXTRA_PUMP_PIN);
}

void loop() {
  if (!isPumping) {
    if ((extraPump || (mainTimer - myTimer >= PERIOD)) && waterCheck()) {
      resetTimer();
      startPump();
    }
  } else {
    if ((mainTimer - myTimer >= WORK) || !waterCheck()) {
      resetTimer();
      stopPump();
    }
  }
  extraPump = false;

  goSleep();
}

/**
 * Обработчик прерываний вачдога
 */
ISR (WDT_vect) {
  mainTimer++;
  bitSet(WDTCR, WDIE); // разрешаем прерывания по ватчдогу. Иначе будет реcет.
  heartbeat();
}

void heartbeat() {
  if (0 == (mainTimer % HB_PERIOD)) {
    pinMode(HEARTBEAT_PIN, OUTPUT);
    digitalWrite(HEARTBEAT_PIN, HIGH);
  } else {    
    digitalWrite(HEARTBEAT_PIN, LOW);
    pinMode(HEARTBEAT_PIN, INPUT);
  }
}

/**
 * Выводы PCINT0,PCINT1,PCINT2,PCINT3,PCINT4,PCINT5
 * генерируют одно и то же прерывание - PCINT0
 */
ISR (PCINT0_vect) {
  if (LOW == digitalRead(EXTRA_PUMP_PIN)) extraPump = true;
}

bool waterCheck() {
#ifdef SENSOR_PIN
  pinMode(SENSOR_PIN, INPUT_PULLUP);
  boolean retval = LOW == digitalRead(SENSOR_PIN);
  pinMode(SENSOR_PIN, INPUT);  
  return retval;
#else
  return true;
#endif
}

void resetTimer() {
  myTimer = mainTimer;
}

void startPump() {
  isPumping = true;
  pinMode(MOS_PIN, OUTPUT);
  digitalWrite(MOS_PIN, HIGH);
}

void stopPump() {
  isPumping = false;
  digitalWrite(MOS_PIN, LOW);
  pinMode(MOS_PIN, INPUT);
}

void goSleep() {
  sleep_enable();   // разрешаем сон
  sleep_cpu();      // спать!
}
