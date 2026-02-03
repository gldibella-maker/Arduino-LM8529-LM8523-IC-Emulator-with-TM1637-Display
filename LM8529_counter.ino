/*
 * LM8529 Emulator for Arduino (TM1637 LED Edition)
 * REVISIONE 5: Ripristino Active HIGH per ingressi di controllo.
 * 
 * LOGICA INGRESSI (Active HIGH):
 * - RESET, STW_TAPE, START_STOP, MEMORY_STOP: HIGH = Attivo.
 * - Nota: I pin sono configurati come INPUT. Assicurarsi di avere 
 *   resistenze di pull-down esterne se i segnali sono flottanti.
 * 
 * LOGICA SEGNALI TAPE (Fig. 10):
 * - TAPEPULSE: Fronte di salita (RISING) su Pin 2 (Interrupt).
 * - UPDOWN: Campionato istantaneamente nell'ISR. HIGH=UP, LOW=DOWN.
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

long storedN = -1;
bool lastMemStopInput = LOW; 
bool out1Active = false;
unsigned long out1StartTime = 0;
long lastMatchedValue = -1;

void setup() {
  // Configurazione Ingressi: INPUT semplice (Richiede Pull-down esterni)
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

  // Inizializzazione Uscite Active LOW (Inattive = HIGH)
  digitalWrite(PIN_OUT_ZERO, HIGH);
  digitalWrite(PIN_OUT1, HIGH);
  digitalWrite(PIN_OUT_TARGET, HIGH);
  digitalWrite(PIN_OUT_29, HIGH);
  digitalWrite(PIN_SEG_B_D1, HIGH);
  digitalWrite(PIN_SEG_C_D1, HIGH);

  display.setBrightness(0x0f);
  display.clear();

  // Configurazione Timer 1 (100Hz per cronometro)
  cli();
  TCCR1A = 0; TCCR1B = 0; TCNT1 = 0; 
  OCR1A = 2499; 
  TCCR1B |= (1 << WGM12) | (1 << CS11) | (1 << CS10); 
  TIMSK1 |= (1 << OCIE1A); 
  sei();

  // Sincronizzazione Fig. 10: Interrupt su Fronte di Salita
  attachInterrupt(digitalPinToInterrupt(PIN_TAPE_PULSE), tapePulseISR, RISING);
}

// ISR Cronometro: Incrementa se START_STOP è HIGH
ISR(TIMER1_COMPA_vect) {
  if (digitalRead(PIN_START_STOP) == HIGH) stwCounter++;
}

// ISR Nastro: Cattura direzione al fronte di salita di TAPEPULSE
void tapePulseISR() {
  // Conteggio attivo se NON in modalità Stopwatch (STW_TAPE == LOW)
  if (digitalRead(PIN_STW_TAPE) == LOW) {
    // Sincronizzazione Fig. 10: UPDOWN HIGH = UP, LOW = DOWN
    bool dirUp = (digitalRead(PIN_UP_DOWN) == HIGH);
    
    pulseSubCount++;
    if (pulseSubCount >= 1) { // Divisore 2:1
      pulseSubCount = 0;
      if (dirUp) tapeCounter++;
      else tapeCounter--;
      
      if (tapeCounter > 9999) tapeCounter = 0;
      else if (tapeCounter < 0) tapeCounter = 9999;
    }
  }
}

void loop() {
  // Lettura segnali Active HIGH
  bool isStwMode = (digitalRead(PIN_STW_TAPE) == HIGH);
  bool isResetAct = (digitalRead(PIN_RESET) == HIGH);
  bool currentMemStopInput = (digitalRead(PIN_MEMORY_STOP) == HIGH);

  long currentTapeValue;
  noInterrupts(); currentTapeValue = tapeCounter; interrupts();

  if (isResetAct) {
    if (isStwMode) { 
      noInterrupts(); stwCounter = 0; interrupts(); 
    } else { 
      noInterrupts(); tapeCounter = 0; pulseSubCount = 0; interrupts(); 
      storedN = -1; 
      currentTapeValue = 0;
    }
  }

  // Cattura memoria su fronte di salita (Active HIGH)
  if (currentMemStopInput == HIGH && lastMemStopInput == LOW) {
    storedN = currentTapeValue;
  }
  lastMemStopInput = currentMemStopInput;

  // Gestione OUT 1 (Active LOW per 300ms al match)
  if (!isStwMode && storedN != -1 && currentTapeValue == storedN) {
    if (!out1Active && currentTapeValue != lastMatchedValue) {
      digitalWrite(PIN_OUT1, LOW); // ATTIVA USCITA
      out1Active = true;
      out1StartTime = millis();
      lastMatchedValue = currentTapeValue;
    }
  } else {
    if (currentTapeValue != storedN) lastMatchedValue = -1;
  }
  if (out1Active && (millis() - out1StartTime >= 300)) {
    digitalWrite(PIN_OUT1, HIGH); // DISATTIVA USCITA
    out1Active = false;
  }

  updateDisplayAndOutputs(isStwMode, currentTapeValue);
}

void updateDisplayAndOutputs(bool isStwMode, long currentTape) {
  int digit1Val = 0;
  bool suppressed = false;

  if (isStwMode) {
    long currentStw;
    noInterrupts(); currentStw = stwCounter; interrupts();
    int min = (currentStw / 6000) % 100;
    int sec = (currentStw / 100) % 60;
    digit1Val = (min / 10) % 10;
    
    // Colon lampeggiante se attivo, fisso se fermo
    bool running = (digitalRead(PIN_START_STOP) == HIGH);
    uint8_t dots = (running && (millis() / 500) % 2 != 0) ? 0 : 0b01000000;
    display.showNumberDecEx(min * 100 + sec, dots, true);
  } else {
    digit1Val = (currentTape / 1000) % 10;
    suppressed = (currentTape < 1000);
    display.showNumberDec(currentTape, false);
  }

  // Uscite Segmenti Digit 1 (Invertite: LOW=Acceso)
  if (suppressed) {
    digitalWrite(PIN_SEG_B_D1, HIGH);
    digitalWrite(PIN_SEG_C_D1, HIGH);
  } else {
    digitalWrite(PIN_SEG_B_D1, (SEGMENT_MAP[digit1Val] & 0b00000010) ? LOW : HIGH);
    digitalWrite(PIN_SEG_C_D1, (SEGMENT_MAP[digit1Val] & 0b00000100) ? LOW : HIGH);
  }

  // Uscite di stato fisse (Active LOW)
  digitalWrite(PIN_OUT_ZERO, (!isStwMode && currentTape == 0) ? LOW : HIGH);
  digitalWrite(PIN_OUT_TARGET, (!isStwMode && currentTape == 9) ? LOW : HIGH);
  digitalWrite(PIN_OUT_29, (!isStwMode && currentTape == 29) ? LOW : HIGH);
}
