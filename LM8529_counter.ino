/*
 * LM8529 Emulator for Arduino (TM1637 LED Edition)
 * Logica Punti (Colon):
 * - TAPE: Sempre SPENTI.
 * - STW Marcia: LAMPEGGIO 1Hz.
 * - STW Stop: FISSI ACCESI.
 */

#include <TM1637Display.h>
#include <avr/interrupt.h>

const int PIN_CLK = 2;
const int PIN_DIO = 3;
TM1637Display display(PIN_CLK, PIN_DIO);

const int PIN_UP_DOWN      = 4;    
const int PIN_TAPE_PULSE   = 5;  
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
  0b01101101  // 9 (alternativo 0b01101111)
};

long tapeCounter = 0;
long storedN = -1;
volatile long stwCounter = 0; 
int pulseSubCount = 0;

bool lastPulseState = LOW;
bool lastMemStopInput = LOW;
bool out1Active = false;
unsigned long out1StartTime = 0;
long lastMatchedValue = -1;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 5; 

void setup() {
  pinMode(PIN_UP_DOWN, INPUT_PULLUP);
  pinMode(PIN_TAPE_PULSE, INPUT_PULLUP);
  pinMode(PIN_STW_TAPE, INPUT_PULLUP);
  pinMode(PIN_RESET, INPUT_PULLUP);
  pinMode(PIN_START_STOP, INPUT_PULLUP);
  pinMode(PIN_MEMORY_STOP, INPUT_PULLUP);
  
  pinMode(PIN_OUT_ZERO, OUTPUT);
  pinMode(PIN_OUT1, OUTPUT);
  pinMode(PIN_OUT_TARGET, OUTPUT);
  pinMode(PIN_OUT_29, OUTPUT);
  pinMode(PIN_SEG_B_D1, OUTPUT);
  pinMode(PIN_SEG_C_D1, OUTPUT);

  display.setBrightness(0x0f);
  display.clear();

  // Timer 1 per precisione 10ms (100Hz)
  cli();
  TCCR1A = 0; TCCR1B = 0; TCNT1 = 0; 
  OCR1A = 2499; 
  TCCR1B |= (1 << WGM12) | (1 << CS11) | (1 << CS10); 
  TIMSK1 |= (1 << OCIE1A); 
  sei();
}

ISR(TIMER1_COMPA_vect) {
  if (digitalRead(PIN_START_STOP) == HIGH) stwCounter++;
}

void loop() {
  bool isStwMode = (digitalRead(PIN_STW_TAPE) == HIGH);
  bool isReset = (digitalRead(PIN_RESET) == HIGH);
  bool currentMemStopInput = (digitalRead(PIN_MEMORY_STOP) == HIGH);

  if (isReset) {
    if (isStwMode) { noInterrupts(); stwCounter = 0; interrupts(); }
    else { tapeCounter = 0; pulseSubCount = 0; storedN = -1; }
  }

  if (currentMemStopInput == HIGH && lastMemStopInput == LOW) storedN = tapeCounter;
  lastMemStopInput = currentMemStopInput;

  handleTapeCounter();

  // Logic OUT 1
  if (!isStwMode && storedN != -1 && tapeCounter == storedN) {
    if (!out1Active && tapeCounter != lastMatchedValue) {
      digitalWrite(PIN_OUT1, HIGH);
      out1Active = true;
      out1StartTime = millis();
      lastMatchedValue = tapeCounter;
    }
  } else {
    if (tapeCounter != storedN) lastMatchedValue = -1;
  }
  if (out1Active && (millis() - out1StartTime >= 300)) {
    digitalWrite(PIN_OUT1, LOW);
    out1Active = false;
  }

  // Monitor Segmenti Digit 1 per coerenza soppressione
  int digit1Value = 0;
  bool isDigit1Suppressed = false;

  if (isStwMode) {
    long currentStw;
    noInterrupts(); currentStw = stwCounter; interrupts();
    int minutes = (currentStw / 6000) % 100;
    digit1Value = (minutes / 10) % 10;
    isDigit1Suppressed = false;
  } else {
    digit1Value = (tapeCounter / 1000) % 10;
    if (tapeCounter < 1000) isDigit1Suppressed = true;
  }
  
  if (isDigit1Suppressed) {
    digitalWrite(PIN_SEG_B_D1, LOW);
    digitalWrite(PIN_SEG_C_D1, LOW);
  } else {
    bool isSegBActive = (SEGMENT_MAP[digit1Value] & 0b00000010);
    digitalWrite(PIN_SEG_B_D1, isSegBActive ? HIGH : LOW);
    bool isSegCActive = (SEGMENT_MAP[digit1Value] & 0b00000100);
    digitalWrite(PIN_SEG_C_D1, isSegCActive ? HIGH : LOW);
  }

  if (isStwMode) displayStopwatch();
  else displayTape();

  digitalWrite(PIN_OUT_ZERO, (!isStwMode && tapeCounter == 0) ? HIGH : LOW);
  digitalWrite(PIN_OUT_TARGET, (!isStwMode && tapeCounter == 9) ? HIGH : LOW);
  digitalWrite(PIN_OUT_29, (!isStwMode && tapeCounter == 29) ? HIGH : LOW);
}

void handleTapeCounter() {
  bool currentPulse = digitalRead(PIN_TAPE_PULSE);
  bool dirUp = (digitalRead(PIN_UP_DOWN) == HIGH);
  if (currentPulse != lastPulseState) {
    if ((millis() - lastDebounceTime) > debounceDelay) {
      if (currentPulse == LOW) {
        pulseSubCount++;
        if (pulseSubCount >= 5) {
          pulseSubCount = 0;
          if (dirUp) tapeCounter++;
          else tapeCounter--;
          if (tapeCounter > 9999) tapeCounter = 0;
          if (tapeCounter < 0) tapeCounter = 9999;
        }
      }
      lastPulseState = currentPulse;
      lastDebounceTime = millis();
    }
  }
}

void displayTape() {
  display.showNumberDec(tapeCounter, false);
}

void displayStopwatch() {
  long currentStw;
  noInterrupts(); currentStw = stwCounter; interrupts();
  int minutes = (currentStw / 6000) % 100;
  int seconds = (currentStw / 100) % 60;
  
  bool isRunning = (digitalRead(PIN_START_STOP) == HIGH);
  uint8_t dots = 0;
  
  if (isRunning) {
    // Lampeggio 1Hz se attivo
    if ((millis() / 500) % 2 == 0) dots = 0b01000000;
    else dots = 0;
  } else {
    // Fisso ACCESO se in stop (solo in modalità STW)
    dots = 0b01000000;
  }
  
  display.showNumberDecEx(minutes * 100 + seconds, dots, true);
}
