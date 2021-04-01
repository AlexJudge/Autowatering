/**
 * Ардуинка записывает в постоянную память время в миллисекундах возникновения двух каких-либо сигналов, нампример от МК
 * Выход первого МК можно подключить к пинам D0-D7, выход второго - D8-D13
 * Запись в память будет происходить только после первого нажатия на кнопку BTN_MEM_W_ENABLE_PIN, подключенную к одному из пинов A0-A5
 * 
 * Порядок работы:
 * - прошиваем в режиме MODE_MONITORING
 * - подключаем к МК
 * - выпоняем необходимые испытания за один раз (повторное испытания после отключения и включения питания затрет старые данные)
 * - подключаем к компьютеру
 * - прошиваем в режиме вывода данных: все режимы закомментированы
 * - открываем монитор порта и забираем сохранненые данные
 * - прошиваем в режиме MODE_CLEAR для обнуления записанных данных
 * - ардуинка готова к новой прошивке
 */

#include <EEPROM.h>
#include <avr/sleep.h>

// Конфиг для МК №1
#define SIGNAL_1_NAME        String("ATTiny85")
#define SIGNAL_1_PIN         5
#define SIGNAL_1_START_ADDR  0
int data1Addr = SIGNAL_1_START_ADDR;

// Конфиг для МК №2 (если тестируется только один МК, то этот блок можно закомментировать)
#define SIGNAL_2_NAME        String("ATTiny13")
#define SIGNAL_2_PIN         12
#define SIGNAL_2_START_ADDR  512
int data2Addr = SIGNAL_2_START_ADDR;

// Индикаторный светодиод
#define LED_PIN 13

// Кнопка включения режима "запись в память разрешена"
#define BTN_MEM_W_ENABLE_PIN A0
bool memWriteEnabled = false;

// Режим работы Arduino по приоритетам
//#define MODE_CLEAR     // очистка постоянной памяти
#define MODE_MONITORING  // ждем сигнала и записываем время
// По-умолчанию: печать содержимого памяти

void setup() {
  Serial.begin(9600);
  Serial.println("/////////////");
  Serial.println("Start");
  
#ifdef MODE_CLEAR
  clearMem();
#else

#ifdef MODE_MONITORING
  monitoring();
#else
  printMem();
#endif

#endif
}

void clearMem() {
  pinMode(LED_PIN, OUTPUT);
  for (int i = 0 ; i < EEPROM.length() ; i++) EEPROM.update(i, 0);
  digitalWrite(LED_PIN, HIGH);
  Serial.println("Memory cleanup completed.");
}

void monitoring() {
  pinMode(SIGNAL_1_PIN, INPUT);
  attachPCINT(SIGNAL_1_PIN);

#ifdef SIGNAL_2_PIN
  pinMode(SIGNAL_2_PIN, INPUT);
  attachPCINT(SIGNAL_2_PIN);
#endif
  
  pinMode(BTN_MEM_W_ENABLE_PIN, INPUT_PULLUP);
  attachPCINT(BTN_MEM_W_ENABLE_PIN);
  
  pinMode(LED_PIN, OUTPUT);
  
  sei();
}

void attachPCINT(byte pin) {
  if (pin < 8) {            // D0-D7 (PCINT2)
    bitSet(PCICR, PCIE2);
    bitSet(PCMSK2, pin);
  } else if (pin > 13) {    // A0-A5 (PCINT1)
    bitSet(PCICR, PCIE1);
    bitSet(PCMSK1, pin - 14);
  } else {                  // D8-D13 (PCINT0)
    bitSet(PCICR, PCIE0);
    bitSet(PCMSK0, pin - 8);
  }
}

void printMem() {
  long firstMillis;
  for (int addr = 0; addr < EEPROM.length(); addr+=4) {
    if (SIGNAL_1_START_ADDR == addr) {
      Serial.println("=========");
      Serial.println(SIGNAL_1_NAME);
      firstMillis = 0;
    }

#ifdef SIGNAL_2_START_ADDR
    if (SIGNAL_2_START_ADDR == addr) {
      Serial.println("=========");
      Serial.println(SIGNAL_2_NAME);
      firstMillis = 0;
    }
#endif

    long data = eeprom_read_dword(addr);    
    if (0 == firstMillis) firstMillis = data;
    data -= firstMillis;

    if (data > 0) Serial.println(String(data) + " -> " + millis2time(data));
  }
  
  Serial.println("=========");
  Serial.println("Memory print completed.");
}

String millis2time(long ms) {
  long sec = ms / 1000;
  
  byte h = (sec / 3600) % 24;
  byte m = (sec % 3600) / 60;
  byte s = (sec % 3600) % 60;
  
  return fix(h) + ":" + fix(m) + ":" + fix(s);
}

String fix(byte v) {
  return String(v < 10 ? "0" : "") + v;
}

void loop() {}

/*
 * Обработчики прерываний
 * 
 * Прерывание вызывается при переключении состояния любого пина из группы
 */

// Пины A0-A5
ISR(PCINT1_vect) {  
  Serial.println("Button INT");
  if (digitalRead(BTN_MEM_W_ENABLE_PIN)) memWriteEnabled = true;
}

// Пины 0-7
ISR(PCINT2_vect) {
  Serial.print(SIGNAL_1_NAME + " INT: ");
  process(SIGNAL_1_PIN, &data1Addr);  
}

#ifdef SIGNAL_2_PIN
// Пины 8-13
ISR(PCINT0_vect) {
  Serial.print(SIGNAL_2_NAME + " INT: ");
  process(SIGNAL_2_PIN, &data2Addr);
}
#endif

void process(byte pin, int* addr) {
  bool pinVal = digitalRead(pin);
  Serial.println(pinVal);
  
  if (!memWriteEnabled) return;
    
  digitalWrite(LED_PIN, pinVal);
  
  if (!pinVal || *addr >= EEPROM.length()) return;
  
  long data = millis(); // 4 байта -> dword
  eeprom_write_dword(*addr, data);
  *addr += 4;
}
