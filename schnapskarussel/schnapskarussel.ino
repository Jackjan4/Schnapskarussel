/**
   Schnapskarussel
   v1.1.1
   27.01.2019 27.01.2019
   1.0.1 : Bugfix, dass nach dem auffüllen frei gedreht wird
   1.0.2 : Unendlich warmup gefixt + Stepper step in eigene Methode
   1.0.3 : Unterstützung dritter Button for Cooldown + Cooldown code + Pumpenmethode nimmt nun Richtung an
           + Optimierungen, dass Sketch weniger Speicher benötigt
   1.0.4 : Verstellbare Offset-Konstante fürs Glas hinzugefügt
   1.0.5 : Abfrage zum Glas-Step-Offset hinzugefügt für die Möglichkeit bei Wert 0 es auszuschalten
   1.0.6 : Stepper am Ende des Prozesses releasen für Schutz vor Überhitzung + LED Code + Wartezeit nach auffüllen
   1.0.7 : Automatische Stepberechnungen je nach Stepstyle + Motor bremsen, damit er nicht weiterdreht nach dem rotieren
   1.0.8 : Game mode hinzugefügt
   1.0.9 : Cooldown entfernt, da Pumpe nicht rückwärts laufen kann
   1.1.0 : Neopixel eingefügt + GLAS_STEP_OFFSET kann nun auch negativ sein (somit kann der offset auch rückwärts erfolgen)
   1.1.1 : Übersetzungsvariable hinzugefügt + DEBUG precompiler code
*/


#include <AFMotor.h>
#include <Adafruit_NeoPixel.h>

// Debug precompiler code
#define DEBUG
#ifdef DEBUG
#define DEBUG_PRINT(x)  Serial.println(x)
#else
#define DEBUG_PRINT(x)
#endif




// Pumpgeschweindigkeit der Wasserpumpe
const uint8_t SPEED_PUMPE = 100;


// Drehgeschwindigkeit des Steppers
const uint8_t SPEED_STEPPER = 10;

// Art der Steps: SINGLE, DOUBLE, INTERLEAVE, MICROSTEP
const uint8_t STEPSTYLE = DOUBLE;

// Wartezeit zum abtropfen, bevor weitergedreht wird
const uint8_t FILL_WAITTIME = 100;

// Zeit die gewartet werden soll, um den Teller am Ende der Umdrehung zu beruhigen
const int PUMPE_BREAKTIME = 400;

// Zeit in Millisekunden, wielange die Pumpe zum befüllen laufen soll
const int FILLTIME = 200;

// Zeit in Millisekunden, wielange die Pumpe sich volllaufen soll
const int WARMUPTIME = 2000;


// ======== ZUSTÄNDE =========

enum State { STATE_IDLE = 1, STATE_FILL = 2, STATE_ROTATE = 4, STATE_INIT = 8, STATE_ROTATE_INIT = 16};
boolean isGameModeActive;

// Derzeitiger Zustand
uint8_t currentState;



// Schritte die noch gemacht werden sollen, damit Glas korrekt unterm Schlauch steht
const int8_t GLAS_STEP_OFFSET = 5;

// Übersetzung vom Motor auf das Drehkarussel
const float TRANSMISSION = 3.0;
// Schritte, die für eine komplette Umdrehung des Stepper-Motors nötig sind
const uint16_t FULL_ROTATE_STEPS = 200;
uint16_t neededSteps;
uint16_t stepsDone;



// Pin-Belegungen
const uint8_t PIN_BUTTON_START = A0;
const uint8_t PIN_BUTTON_WARMUP = A1;
const int PIN_BUTTON_GAME = A3;
const int PIN_SENSOR = A5;
const int PIN_STATUS_LED = 2;
const int PIN_NEOPIXEL = A4;



// Gibt die Größe des Zufallsbereich an, so größer die Zahl, desto unwahrscheinlicher ist es im GameMode, dass ein Glas befüllt wird =>    Wahrscheinlichkeit =  1 / (RANDOM_SIZE + 1)
const uint8_t RANDOM_SIZE = 3;

// Zeit in Millisekunden wie schnell die LED blinkt
const uint16_t LED_BLINK_TIME = 255;
uint8_t currentLedState;



// Motor initialisieren
AF_DCMotor pumpe(1); // M1
AF_Stepper stepper(FULL_ROTATE_STEPS, 2); // M3 & M4



// NeoPixel initialisieren
Adafruit_NeoPixel neopixel = Adafruit_NeoPixel(12, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);



// 
void setup() {

  
  // Monitor einschalten
  Serial.begin(9600);
  DEBUG_PRINT("Programmstart");

  // Pins initialisieren
  pinMode(PIN_BUTTON_START, INPUT_PULLUP);
  pinMode(PIN_BUTTON_WARMUP, INPUT_PULLUP);
  pinMode(PIN_BUTTON_GAME, INPUT_PULLUP);
  pinMode(PIN_SENSOR, INPUT);
  pinMode(PIN_STATUS_LED, OUTPUT);

  pumpe.setSpeed(SPEED_PUMPE);
  stepper.setSpeed(SPEED_STEPPER);


  // Berechne benötigte Steps
  switch (STEPSTYLE) {
    case DOUBLE:
    case SINGLE:
      neededSteps = FULL_ROTATE_STEPS * TRANSMISSION;
      break;

    case INTERLEAVE:
      neededSteps = (FULL_ROTATE_STEPS * 2) * TRANSMISSION;
      break;

    case MICROSTEP:
      neededSteps = (FULL_ROTATE_STEPS / 2) * TRANSMISSION;
      break;
  }

  DEBUG_PRINT(neededSteps);


  // Beginne mit idle state
  goIdle();
}



void loop() {

  switch (currentState) {


    // Code während idle
    case STATE_IDLE:
      if (isButtonPressed(PIN_BUTTON_START)) {
        currentState = STATE_ROTATE_INIT;
        isGameModeActive = false;
        DEBUG_PRINT("IDLE -> ROTATE_INIT - Start-Button gedrückt");
      } else if (isButtonPressed(PIN_BUTTON_WARMUP)) {
        currentState = STATE_INIT;
        DEBUG_PRINT("IDLE -> INIT - Warmup-Button gedrückt");
      } else if (isButtonPressed(PIN_BUTTON_GAME)) {
        currentState = STATE_ROTATE_INIT;
        isGameModeActive = true;
        DEBUG_PRINT("IDLE -> ROTATE - Game-Button gedrückt");
      }
      break;





    // Code während auffüllen
    case STATE_FILL:

      // Befüllen
      blinkLed();
      runPumpe(FILLTIME, FORWARD);

      // Kurz warten zum abtropfen
      delay(FILL_WAITTIME);

      // Nach dem befüllen freidrehen
      while (istGlasVorSensor()) {
        doRotateStep();
      }

      // Wieder in Rotation wechseln
      currentState = STATE_ROTATE;
      DEBUG_PRINT("FILL -> ROTATE - Füllen fertig, drehe weiter");
      break;





    // Code während rotieren
    case STATE_ROTATE:
      blinkLed();
      if (istGlasVorSensor()) {

        
        if (GLAS_STEP_OFFSET > 0) {
          // evtl nötig, damit glas gerade unterm Schlauch
          stepper.step(GLAS_STEP_OFFSET, FORWARD, STEPSTYLE);
        } else if (GLAS_STEP_OFFSET < 0) {
          stepper.step(-GLAS_STEP_OFFSET, BACKWARD, STEPSTYLE);
        }

        // GameMode code
        if (isGameModeActive) {
          // Erstellen eines zufalligen boolean
          bool fillGlass = (random(RANDOM_SIZE) == 0) ? true : false;
          ledParty(fillGlass);
          // Wenn r == false: überspringe das Glas
          if (!fillGlass) {
            break;
          }
        }

        // Glas erkannt, also in Füllmodus wechseln
        currentState = STATE_FILL;
        DEBUG_PRINT("ROTATE -> FILL - Glas gefunden, füllen");
      }

      // Umdrehung fertig
      if (stepsDone >= neededSteps) {
        
        DEBUG_PRINT("ROTATE -> IDLE - Drehung fertig");
        // Umdrehung fertig, bremsen(also Stepper release) um unnötigen Stromverbrauch & Hitzeentwicklung zu vermeiden
        delay(PUMPE_BREAKTIME);
        stepper.release();
        // State wechseln zu IDLE
        goIdle();

      } else {
         // Weiterdrehen
        doRotateStep();
      }
      break;





    // Code während Warmup
    case STATE_INIT:
      blinkLed();
      runPumpe(WARMUPTIME, FORWARD);

      // State wechseln zu IDLE
      goIdle();
      DEBUG_PRINT("WARMUP -> IDLE - Warmup fertig");
      break;




    // Code während Rotate_init zum Freidrehen des Tellers
    case STATE_ROTATE_INIT:
      blinkLed();
      if (istGlasVorSensor()) {
        stepper.step(1, FORWARD, STEPSTYLE);
      } else {
        // Nochmal etwas vordrehen, damit auch ganz sicher nichts mehr vor dem Sensor
        stepper.step(2, FORWARD, STEPSTYLE);
        currentState = STATE_ROTATE;
        DEBUG_PRINT("ROTATE_INIT -> ROTATE - init fertig, begin drehen");
      }
      break;

  }




}



/**
  Macht einen Motorstep für die Rotation einer ganzen Umdrehung
*/
void doRotateStep() {
  stepper.step(1, FORWARD, STEPSTYLE);
  stepsDone++;
}



/**
   Gibt true zurück, wenn Glas vor dem Sensor steht
   Ansonsten false
*/
inline bool istGlasVorSensor() {
  return (digitalRead(PIN_SENSOR) == LOW) ? true : false;
}



/**
   Betreibt die Pumpe für die angegebenen Millisekunden
*/
void runPumpe(int mill, int direction) {

  unsigned long start = millis();

  pumpe.run(direction);
  while (millis() < start + mill) {

    // Da Methode blocking ist, LED blinken aufrufen, damit diese weitermacht
    blinkLed();
  }
  pumpe.run(RELEASE);
}





/**
  Gibt true zurück, wenn der angegebene Button (angeschlossen an dem Pin) gedrückt ist
*/
bool isButtonPressed(uint8_t buttonPin) {
  uint8_t val = digitalRead(buttonPin);
  return (val == LOW) ? true : false;

}



/**
   Wechselt den State in den Idle State und setzt dabei das ganze System in den Idle
*/
void goIdle() {
  isGameModeActive = false;
  currentState = STATE_IDLE;
  currentLedState = HIGH;
  digitalWrite(PIN_STATUS_LED, currentLedState);
  stepsDone = 0;
}



/**
  Lässt die LED blinken. Achtung: sollte in jedem Clock-Cycle aufgerufen werden, da die Methode eine Clock benutzt zum berechnen des nächsten Blink-States
*/
void blinkLed() {

  // Static, sodass diese Funktion ihren State speichert -> Coroutine
  static unsigned long ledStateMillis;

  unsigned long curMillis = millis();

  if (curMillis > ledStateMillis + LED_BLINK_TIME) {
    uint8_t newState = (currentLedState == HIGH) ? LOW : HIGH;
    // Wechsel LED status
    digitalWrite(PIN_STATUS_LED, newState);
    currentLedState = newState;
    ledStateMillis = curMillis;
  }
}


/**
   Aktiviert den GameMode für die nächste Umdrehung und erstellt einen Zufallsseed
*/
void activateGameMode() {
  isGameModeActive = true;
  randomSeed(millis());
}


/**
 * Lässt die Ring-LED blinken für einen Effekt.
 * Blockiert, bis Lichteffekte fertig
 */
void ledParty(bool willGlassFill) {
  
}
