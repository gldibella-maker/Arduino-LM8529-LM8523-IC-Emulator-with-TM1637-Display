/*
 * LM8529 Emulator for Arduino (TM1637 LED Edition)
 * REVISIONE 6: Debounce 2ms e Conteggio Background
 * 
 * LOGICA CONTEGGIO (Fig. 10):
 * - TAPEPULSE (Pin 2): Interrupt RISING con debounce di 2ms.
 * - Il conteggio prosegue in background anche se STW_TAPE è attivo.
 * 
 * LOGICA INGRESSI (Active HIGH):
 * - RESET, STW_TAPE, START_STOP, MEMORY_STOP: attivi se HIGH (5V).
 * 
 * LOGICA USCITE (Active LOW):
 * - OUT1, ZERO, TARGET, 29, SEG_B, SEG_C sono attive a livello LOW.
 */

#include <TM1637Display.h>
#include <avr/interrupt.h>

const int PIN_CLK = A4;
const int PIN_DIO = A5;
TM1637Display display(PIN_CLK, PIN_DIO);

const int PIN_UP_DOWN      = 3;    
const int PIN_TAPE_PULSE   = 2; // Pin 2 (INT0)
const int PIN_STW_TAPE     = 6;    
const int PIN_RESET        = 7;     
const int PIN_START_STOP   = 8; 
const int PIN_MEMORY_STOP  = 9; 
const int PIN_OUT_ZERO     = 10;    
const int PIN_OUT1         = 11;      
const int PIN_OUT_TARGET   = A0;  
const int PIN_OUT_29       = A1; 
const int PIN_SEG_B_D1     = A2; 
const int PIN_SEG_C_D1     = A3;     

const uint8_t SEGMENT_MAP[] = {
  0b00111111, // 0 
  0b00000110, // 1 
  0b01011011, // 2 
  0b01001111, // 3 
  0b01100110, // 4 
  0b01101101, // 5 
  0b01111101, // 6 
  0b00000111, // 7 
  0b01111111, // 8 
  0b01101101  // 9 
};

// Variabili volatili per ISR
volatile long tapeCounter = 0;
volatile int pulseSubCount = 0;
volatile long stwCounter = 0; 
volatile unsigned long lastPulseMillis = 0;

long storedN = -1;
bool lastMemStopInput = LOW;
bool out1Active = false;
unsigned long out1StartTime = 0;
long lastMatchedValue = -1;

void setup() {
  pinMode(PIN_UP_DOWN, INPUT);
  pinMode(PIN_TAPE_PULSE, INPUT);
  pinMode(PIN_STW_TAPE, INPUT);
  pinMode(PIN_RESET, INPUT);
  pinMode(PIN_START_STOP, INPUT);
  pinMode(PIN_MEMORY_STOP, INPUT);
  
  pinMode(PIN_OUT_ZERO, OUTPUT);
  pinMode(PIN_OUT1, OUTPUT);
  pinMode(PIN_OUT_TARGET, OUTPUT);
  pinMode(PIN_OUT_29, OUTPUT);
  pinMode(PIN_SEG_B_D1, OUTPUT);
  pinMode(PIN_SEG_C_D1, OUTPUT);

  digitalWrite(PIN_OUT_ZERO, HIGH);
  digitalWrite(PIN_OUT1, HIGH);
  digitalWrite(PIN_OUT_TARGET, HIGH);
  digitalWrite(PIN_OUT_29, HIGH);
  digitalWrite(PIN_SEG_B_D1, HIGH);
  digitalWrite(PIN_SEG_C_D1, HIGH);

  display.setBrightness(0x0f);
  display.clear();

  // Timer 1: 100Hz (10ms) per il cronometro
  cli();
  TCCR1A = 0; TCCR1B = 0; TCNT1 = 0; 
  OCR1A = 2499; 
  TCCR1B |= (1 << WGM12) | (1 << CS11) | (1 << CS10); 
  TIMSK1 |= (1 << OCIE1A); 
  sei();

  // Interrupt su fronte di SALITA (Fig. 10)
  attachInterrupt(digitalPinToInterrupt(PIN_TAPE_PULSE), tapePulseISR, RISING);
}

ISR(TIMER1_COMPA_vect) {
  if (digitalRead(PIN_START_STOP) == HIGH) stwCounter++;
}

void tapePulseISR() {
  unsigned long now = millis();
  // Debounce 2ms
  if (now - lastPulseMillis >= 2) {
    lastPulseMillis = now;
    // Conteggio sempre attivo (Background)
    bool dirUp = (digitalRead(PIN_UP_DOWN) == HIGH);
    
    pulseSubCount++;
    if (pulseSubCount >= 1) { // Divisore 5:1
      pulseSubCount = 0;
      if (dirUp) tapeCounter++;
      else tapeCounter--;
      
      if (tapeCounter > 9999) tapeCounter = 0;
      else if (tapeCounter < 0) tapeCounter = 9999;
    }
  }
}

void loop() {
  bool isStwMode = (digitalRead(PIN_STW_TAPE) == HIGH);
  bool isResetAct = (digitalRead(PIN_RESET) == HIGH);
  bool currentMemStopInput = (digitalRead(PIN_MEMORY_STOP) == HIGH);

  long currentTapeVal; 
  noInterrupts(); currentTapeVal = tapeCounter; interrupts();

  if (isResetAct) {
    if (isStwMode) { 
      noInterrupts(); stwCounter = 0; interrupts(); 
    } else { 
      noInterrupts(); tapeCounter = 0; pulseSubCount = 0; interrupts(); 
      storedN = -1; 
      currentTapeVal = 0;
    }
  }

  // Cattura memoria su fronte di salita
  if (currentMemStopInput == HIGH && lastMemStopInput == LOW) {
    storedN = currentTapeVal;
  }
  lastMemStopInput = currentMemStopInput;

  // OUT 1 (Active LOW al match)
  if (!isStwMode && storedN != -1 && currentTapeVal == storedN) {
    if (!out1Active && currentTapeVal != lastMatchedValue) {
      digitalWrite(PIN_OUT1, LOW); 
      out1Active = true;
      out1StartTime = millis();
      lastMatchedValue = currentTapeVal;
    }
  } else {
    if (currentTapeVal != storedN) lastMatchedValue = -1;
  }
  
  if (out1Active && (millis() - out1StartTime >= 300)) {
    digitalWrite(PIN_OUT1, HIGH); 
    out1Active = false;
  }

  updateDisplay(isStwMode, currentTapeVal);
}

void updateDisplay(bool isStwMode, long currentTapeVal) {
  int digit1Value = 0;
  bool isDigit1Suppressed = false;

  if (isStwMode) {
    long currentStw;
    noInterrupts(); currentStw = stwCounter; interrupts();
    int min = (currentStw / 6000) % 100;
    int sec = (currentStw / 100) % 60;
    digit1Value = (min / 10) % 10;
    bool running = (digitalRead(PIN_START_STOP) == HIGH);
    uint8_t dots = (running && (millis() / 500) % 2 != 0) ? 0 : 0b01000000;
    display.showNumberDecEx(min * 100 + sec, dots, true);
  } else {
    digit1Value = (currentTapeVal / 1000) % 10;
    isDigit1Suppressed = (currentTapeVal < 1000);
    display.showNumberDec(currentTapeVal, false);
  }

  if (isDigit1Suppressed) {
    digitalWrite(PIN_SEG_B_D1, HIGH);
    digitalWrite(PIN_SEG_C_D1, HIGH);
  } else {
    digitalWrite(PIN_SEG_B_D1, (SEGMENT_MAP[digit1Value] & 0b00000010) ? LOW : HIGH);
    digitalWrite(PIN_SEG_C_D1, (SEGMENT_MAP[digit1Value] & 0b00000100) ? LOW : HIGH);
  }

  digitalWrite(PIN_OUT_ZERO, (!isStwMode && currentTapeVal == 0) ? LOW : HIGH);
  digitalWrite(PIN_OUT_TARGET, (!isStwMode && currentTapeVal <= 9) ? LOW : HIGH);
  digitalWrite(PIN_OUT_29, (!isStwMode && currentTapeVal <= 29) ? LOW : HIGH);
}
