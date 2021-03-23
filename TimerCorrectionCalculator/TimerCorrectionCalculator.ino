/**
 * Данный скетч вычисляет поправочный коэффициент для таймера стороннего МК.
 * В тестируемом МК должна быть прошивка, которая подает импульсы длительностью и промежутком EXPECTED на пин SIGNAL_PIN ардуинки.
 */

#define SIGNAL_PIN 12

void setup() {
  Serial.begin(9600);
  Serial.println();
  Serial.println();
  Serial.println("Start");
  
  pinMode(SIGNAL_PIN, INPUT);
  pciSetup(SIGNAL_PIN);
  
  sei();
}

void pciSetup(byte pin) {
  bitSet(*digitalPinToPCMSK(pin), digitalPinToPCMSKbit(pin));  // Разрешаем PCINT для указанного пина
  bitSet(PCIFR, digitalPinToPCICRbit(pin)); // Очищаем признак запроса прерывания для соответствующей группы пинов
  bitSet(PCICR, digitalPinToPCICRbit(pin)); // Разрешаем PCINT для соответствующей группы пинов
}

// WORK и PERIOD из Pump.ino в миллисекундах
#define EXPECTED 5000;
// количество цифр после запятой для поправочного коэффициента. Достаточно 6 - погрешность в районе 1с/3д
#define PRECISION 6
long N = -1, prevMillis = 0, sum = 0, firstMillis;
float prevCorr = 0;

ISR (PCINT0_vect) {
  N++;
  if (0 == N) {
    firstMillis = millis(); // первый пропускаем, поэтому таймер отсчитываем за вычитом первого промежутка
    return; // пропускаем первый сигнал потому что МК начинает работу раньше чем ардуинка (ардуинка перезапускается при открытии монитора порта)
  }

  long localMillis = millis() - firstMillis;
  
  int deltaMillis = localMillis - prevMillis;
  prevMillis = localMillis;
  sum += deltaMillis;
  long avg = sum / N;
  long expectedSum = N * EXPECTED;
  float corr = fix(((float)expectedSum) / sum, PRECISION);
  String diff = calcDiff(prevCorr, corr);
  prevCorr = corr;
  
  Serial.print("N = " + String(N) + "; ");
  Serial.print("millis = " + String(localMillis) + "; ");
  Serial.print("delta = " + String(deltaMillis) + "; ");
  Serial.print("avg = " + String(avg) + "; ");
  Serial.print("timer = " + sec2time(localMillis / 1000) + "; ");
  Serial.print("corr = ");
  Serial.print(corr, PRECISION);
  Serial.print("; diff = " + diff);
  Serial.println();
}

float fix(float val, int prec) {
  return trunc(val * pow(10, prec)) / pow(10, prec);
}

/**
 * Вычисляем насколько очередной коэффициент вносит поправку по сравнению с предыдущим,
 * при расчете на промежуток времени PERIOD
 */
#define PERIOD 259200 // 3 дня
String calcDiff(float prevCorr, float newCorr) {
  long prevRealPeriod = PERIOD / prevCorr;
  long newRealPeriod = PERIOD / newCorr;
  
  return String(prevRealPeriod - newRealPeriod);
}

String sec2time(long sec) {
  String retval = "";

  // h
  retval += sec/60/60;
  
  retval += ":";

  // mm
  if (sec/60%60<10) retval += "0";
  retval += (sec/60)%60;
  
  retval += ":";

  // ss
  if (sec%60<10) retval += "0";
  retval += sec%60;

  return retval;
}

void loop() {
}
