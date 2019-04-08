/**
   Schnapskarussel
   v1.2.0
   08.04.2019 16:26
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
   1.1.2 : Transmission auf 1 + Anpassung an neuen Nema 23 Motor
   1.1.3 : Optimierungen am Sketch -> weniger benötigter SRAM & Flash
   1.1.4 : LedParty der Neopixel funktioniert
   1.1.5 : Beispielcode für LED und einfacher Bedienung
   1.1.6 : NeoPixel schaltet nach Glas im Partymodus nun aus & ColorWipe Fix versuch #1
   1.1.7 : Fixes: activateGamemode wurde nicht gecalled & GLAS_STEP_OFFSET wurde nicht zur Umdrehung mitgezählt
   1.1.8 : rainbowFade hinzugefügt & FILL_WAITTIME kann nun auch größer als 255 millis sein
   1.1.9 : softDelay(...) eingefügt; blinkLed(...) geändert
   1.2.0 : Methoden kommentiert und Namensgebung angepasst
*/


// Debug precompiler code
#define DEBUG



#ifdef DEBUG
#define DEBUG_PRINTLN(x)  Serial.println(x)
#define DEBUG_PRINT(x) Serial.print(x)
#else
#define DEBUG_PRINTLN(x)
#define DEBUG_PRINT(x)
#endif




#include <AFMotor.h>
#include <Adafruit_NeoPixel.h>






// Pumpgeschweindigkeit der Wasserpumpe -> Max 255
const uint8_t SPEED_PUMPE = 255;


// Drehgeschwindigkeit des Steppers in RPM
const uint8_t SPEED_STEPPER = 8;

// Art der Steps: SINGLE, DOUBLE, INTERLEAVE, MICROSTEP
const uint8_t STEPSTYLE = DOUBLE;

// Wartezeit zum abtropfen, bevor weitergedreht wird, in Millisekunden
const uint16_t FILL_WAITTIME = 800;

// Zeit in Millisekunden, wielange die Pumpe zum befüllen laufen soll
const uint16_t FILLTIME = 1300;

// Zeit in Millisekunden, wielange die Pumpe sich volllaufen soll
const uint16_t WARMUPTIME = 1700;


// ======== ZUSTÄNDE =========

enum State { STATE_IDLE, STATE_FILL, STATE_ROTATE, STATE_WARMUP, STATE_ROTATE_INIT};
boolean isGameModeActive;

// Derzeitiger Zustand
uint8_t currentState;



// Schritte die noch gemacht werden sollen, damit Glas korrekt unterm Schlauch steht
const int8_t GLAS_STEP_OFFSET = 14;

// Übersetzung vom Motor auf das Drehkarussel
const float TRANSMISSION = 1.0;
// Schritte, die für eine komplette Umdrehung des Stepper-Motors nötig sind
const uint16_t FULL_ROTATE_STEPS = 400;
uint16_t neededSteps;
uint16_t stepsDone;



// Pin-Belegungen
const uint8_t PIN_BUTTON_START = A0;
const uint8_t PIN_BUTTON_WARMUP = A1;
const uint8_t PIN_BUTTON_GAME = A3;
const uint8_t PIN_SENSOR = A5;
const uint8_t PIN_NEOPIXEL = 2;



// Gibt die Größe des Zufallsbereich an, so größer die Zahl, desto unwahrscheinlicher ist es im GameMode, dass ein Glas befüllt wird =>    Wahrscheinlichkeit =  1 / (RANDOM_SIZE + 1)
const uint8_t RANDOM_SIZE = 2;

// Zeit in Millisekunden, wie schnell die LED blinkt
const uint16_t LED_BLINK_TIME = 255;
uint8_t currentLedState;



// Motor initialisieren
AF_DCMotor pumpe(1); // M1
AF_DCMotor ventil(2, MOTOR12_64KHZ); // M2
AF_Stepper stepper(FULL_ROTATE_STEPS, 2); // M3 & M4



// NeoPixel initialisieren
const uint8_t NEOPIXEL_COUNT = 12;
const uint8_t NEOPIXEL_BRIGHTNESS = 100;
Adafruit_NeoPixel neopixel = Adafruit_NeoPixel(NEOPIXEL_COUNT, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);



//
void setup() {

  // Monitor einschalten
  Serial.begin(9600);
  DEBUG_PRINTLN("Start");


  // Pins initialisieren
  pinMode(PIN_BUTTON_START, INPUT_PULLUP);
  pinMode(PIN_BUTTON_WARMUP, INPUT_PULLUP);
  pinMode(PIN_BUTTON_GAME, INPUT_PULLUP);
  pinMode(PIN_SENSOR, INPUT);


  // Motorgeschwindigkeiten festlegen
  pumpe.setSpeed(SPEED_PUMPE);
  stepper.setSpeed(SPEED_STEPPER);
  ventil.setSpeed(255);
  


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
  DEBUG_PRINT("Benötigte Schritte: ");
  DEBUG_PRINTLN(neededSteps);


  // Neopixel aktivieren
  neopixel.setBrightness(NEOPIXEL_BRIGHTNESS);
  neopixel.begin();
  neopixel.show(); // Alle ausschalten

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
        DEBUG_PRINTLN("IDLE -> ROTATE_INIT - Start-Button gedrückt");
      } else if (isButtonPressed(PIN_BUTTON_WARMUP)) {
        currentState = STATE_WARMUP;
        DEBUG_PRINTLN("IDLE -> INIT - Warmup-Button gedrückt");
      } else if (isButtonPressed(PIN_BUTTON_GAME)) {
        currentState = STATE_ROTATE_INIT;
        activateGameMode();
        DEBUG_PRINTLN("IDLE -> ROTATE - Game-Button gedrückt");
      }

      // Während idle immer seeden
      randomSeed(millis());

      //
      rainbowFade();
      break;





    // Code während auffüllen
    case STATE_FILL:

      // Befüllen
      if (isGameModeActive) {
        runPumpe(FILLTIME, FORWARD);
      } else {

        // Beim Befüllen ohne Gamemode soll weitergefaded werden (fadeLed() benötigt kontinuierliche Aufrufe)
        runPumpe(FILLTIME, FORWARD, fadeLed);
      }

      // Kurz warten zum abtropfen, trotzdem weiterhin Led-Pulse ausführen
      softDelay(FILL_WAITTIME, fadeLed);

      // Nach dem befüllen freidrehen
      while (istGlasVorSensor()) {
        doRotateStep();
      }

      // Wieder in Rotation wechseln
      currentState = STATE_ROTATE;
      DEBUG_PRINTLN("FILL -> ROTATE - Füllen fertig, drehe weiter");
      break;





    // Code während rotieren
    case STATE_ROTATE:

      // Beim Rotieren ohne Gamemode soll weitergefaded werden (fadeLed() benötigt kontinuierliche Aufrufe)
      if (!isGameModeActive) {
        fadeLed();
      }

      if (istGlasVorSensor()) {

        // evtl nötig, damit glas gerade unterm Schlauch
        if (GLAS_STEP_OFFSET > 0) {
          doRotateSteps(GLAS_STEP_OFFSET);
        } else if (GLAS_STEP_OFFSET < 0) {
          doRotateSteps(GLAS_STEP_OFFSET);
        }


        // GameMode code
        if (isGameModeActive) {
          setAllPixel(0);
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
        DEBUG_PRINTLN("ROTATE -> FILL - Glas gefunden, füllen");
      }

      // Umdrehung fertig
      if (stepsDone >= neededSteps) {

        DEBUG_PRINTLN("ROTATE -> IDLE - Fertig");

        // Umdrehung fertig
        // Mithilfe von Grün blinken anzeigen, dass Prozess fertig (zusätzlich beruhigt diese Zeit die Platte)
        setAllPixel(0, 0, 0);
        delay(500);
        neopixelBlink(3, 200, 400, 65280, false);

        // Stepper loslassen wegen Hitze und Energie
        stepper.release();
        // State wechseln zu IDLE
        goIdle();

      } else {
        // Weiterdrehen
        doRotateStep();
      }
      break;







    // Code während Warmup
    case STATE_WARMUP:
      runPumpe(WARMUPTIME, FORWARD);

      // State wechseln zu IDLE
      goIdle();
      DEBUG_PRINTLN("WARMUP -> IDLE - Warmup fertig");
      break;






    // Code während Rotate_init zum Freidrehen des Tellers
    case STATE_ROTATE_INIT:
      if (istGlasVorSensor()) {
        stepper.step(1, FORWARD, STEPSTYLE);
      } else {
        // Nochmal etwas vordrehen, damit auch ganz sicher nichts mehr vor dem Sensor
        stepper.step(2, FORWARD, STEPSTYLE);
        currentState = STATE_ROTATE;
        DEBUG_PRINTLN("ROTATE_INIT -> ROTATE - init fertig, nun drehen");
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
  Macht einen negativen Motorstep für die Rotation einer ganzen Umdrehung und zieht diesen Schritt wieder ab
*/
void doNegativeRotateStep() {
  stepper.step(1, BACKWARD, STEPSTYLE);
  stepsDone--;
}



/**
  Macht einen Motorstep für die Rotation einer ganzen Umdrehung
*/
void doRotateSteps(uint8_t steps) {

  if (steps > 0) {
    for (uint8_t i = 0; i < steps; i++) {
      doRotateStep();
    }
  } else if (steps < 0) {
    for (uint8_t i = 0; i < -steps; i++) {
      doNegativeRotateStep();
    }
  }
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
void runPumpe(uint16_t mill, uint8_t direction) {

  unsigned long start = millis();

  ventil.run(FORWARD);
  pumpe.run(direction);

  // Solange blocken, bis Zeit fertig
  while (millis() < start + mill) {
  }

  // Pumpe releasen
  pumpe.run(RELEASE);
  ventil.run(RELEASE);
}



/**
   Betreibt die Pumpe für die angegebenen Millisekunden
   Lässt zu, dass bei diesem Delay eine Funktion gecalled wird
*/
void runPumpe(uint16_t mill, uint8_t direction, void (*func)()) {

  unsigned long start = millis();

  pumpe.run(direction);

  // Solange blocken, bis Zeit fertig
  while (millis() < start + mill) {
    func();
  }

  // Pumpe releasen
  pumpe.run(RELEASE);
}




/**
  Gibt true zurück, wenn der angegebene Button (angeschlossen an dem Pin (mit Pullup)) gedrückt ist
*/
inline bool isButtonPressed(uint8_t buttonPin) {
  return (digitalRead(buttonPin) == LOW) ? true : false;

}



/**
   Wechselt den State in den Idle State und setzt dabei das ganze System in den Idle
*/
void goIdle() {

  // Setze GameMode zurück
  isGameModeActive = false;

  currentState = STATE_IDLE;

  // Status LED dauer-an
  currentLedState = HIGH;
  //digitalWrite(PIN_STATUS_LED, currentLedState);

  // Schritte zurücksetzen
  stepsDone = 0;
}



/**
  Lässt die Neopixel-LEDs in zufälligen Farben sehr langsam alle gemeinsam faden.
  Achtung: sollte in jedem Clock-Cycle aufgerufen werden, da die Methode eine Clock benutzt zum berechnen des nächsten Blink-States
*/
void fadeLed() {

  // Delaytime in millis
  static uint16_t delayTime = 10;
  static uint32_t lastTime = 0;
  static int16_t i = 0;
  static uint8_t direction = FORWARD;
  static uint8_t color = random(0, 7);

  if (lastTime + delayTime < millis()) {
    switch (color) {
      // Rot
      case 0:
        setAllPixel(i, 0, 0);
        break;
      // Grün
      case 1:
        setAllPixel(0, i, 0);
        break;
      // Blau
      case 2:
        setAllPixel(0, 0, i);
        break;
      // Gelb
      case 3:
        setAllPixel(i, i, 0);
        break;
      // Weiß
      case 4:
        setAllPixel(i, i, i);
        break;
      // Lila
      case 5:
        setAllPixel(i, 0, i);
        break;
      // Cyan
      case 6:
        setAllPixel(0, i, i);
        break;
    }

    if (direction == FORWARD) {
      i += 3;
    } else {
      i -= 3;
    }


    if (i >= 256) {
      i = 255;
      direction = BACKWARD;
    } else if (i < 0) {
      i = 0;
      direction = FORWARD;
      color = random(0, 7);
    }

    lastTime = millis();
  }
}


/**
   Aktiviert den GameMode für die nächste Umdrehung und erstellt einen Zufallsseed
*/
void activateGameMode() {
  isGameModeActive = true;
}


/**
   Lässt die Ring-LED blinken für einen Effekt.
   Blockiert, bis Lichteffekte fertig.
   (3 Umdrehungen Farbe + dunkeldrehen + 3 mal blinken grün/rot)
*/
void ledParty(bool willGlassFill) {

  // Eine grüne Pixel Umdrehung
  colorCircle(0, 255, 0);

  // Eine blaue Pixel Umdrehung
  colorCircle(0, 0, 255);

  // Eine rote Pixel Umdrehung
  colorCircle(255, 0, 0);

  // Eine farblose Pixel Umdrehung - (Um Pixel auszuschalten)
  colorCircle(0, 0, 0);

  // Warte zwei Sekunden
  delay(2000);

  //
  uint32_t color;
  if (willGlassFill) {
    color = neopixel.Color(255, 0, 0);
  } else {
    color = neopixel.Color(0, 255, 0);
  }

  // Blinkt 3 mal die Neopixels mit der berechneten Farbe und openEnd (openEnd bedeutet, Farbe bleibt am Ende an)
  neopixelBlink(3, 200, 400, color, true);

}



/**
   Macht eine Farbkreis-Animation auf dem Neopixel
*/
void colorCircle(uint8_t red, uint8_t green, uint8_t blue) {
  for (uint8_t i = 0; i < NEOPIXEL_COUNT; i++) {
    neopixel.setPixelColor(i, red, green, blue);
    neopixel.show();
    delay(50);
  }
}



/**
   Blinkt das ganze Neopixel in der angegebenen Anzahl, mit der angegeben Farbe, in dem angegebenen delay (in Millisekunden)
*/
void neopixelBlink(uint8_t blinkTimes, uint16_t lightDelay, uint16_t darkDelay, uint32_t color, boolean openEnd) {

  for (uint8_t n = 0; n < blinkTimes; n++) {
    // Setze alle Pixel
    setAllPixel(color);
    delay(lightDelay);

    if (n < blinkTimes - 1 || !openEnd) {
      setAllPixel(0);
      delay(darkDelay);
    }
  }
}




/**
   Setzt alle Neopixel auf die gegebene Farbe
*/
void setAllPixel(uint32_t color) {
  for (uint8_t i = 0; i < NEOPIXEL_COUNT; i++) {
    neopixel.setPixelColor(i, color);
  }
  neopixel.show();
}



/**
   Setzt alle Neopixel auf die gegebene Farbe
*/
void setAllPixel(uint8_t red, uint8_t green, uint8_t blue) {
  setAllPixel(neopixel.Color(red, green, blue));
}



/**
  Macht einen Rainbow-Fade mit allen Neopixel-LEDs gleichzeitig.
  fadeTime kann in der Methode angepasst werden
*/
void rainbowFade() {
  static uint8_t i = 0;
  static uint16_t fadeTime = 2000;
  static uint32_t lastMillis = 0;

  uint32_t curMillis = millis();
  if (curMillis > lastMillis + fadeTime) {

    // rainbow code here
    setAllPixel(Wheel(i++));

    if (i > 255) {
      i = 0;
    }

    // update last time
    lastMillis = curMillis;
  }

}



/** Input a value 0 to 255 to get a color value.
    The colours are a transition r - g - b - back to r.
*/
uint32_t Wheel(uint8_t WheelPos) {
  WheelPos = 255 - WheelPos;
  if (WheelPos < 85) {
    return neopixel.Color(255 - WheelPos * 3, 0, WheelPos * 3);
  }
  if (WheelPos < 170) {
    WheelPos -= 85;
    return neopixel.Color(0, WheelPos * 3, 255 - WheelPos * 3);
  }
  WheelPos -= 170;
  return neopixel.Color(WheelPos * 3, 255 - WheelPos * 3, 0);
}



/**
   Delayt die gegebene Anzahl an Millisekunden, exakt genauso wie delay(...), lässt jedoch während des delays code-ausführung zu
*/
void softDelay(uint32_t ms, void (*func)()) {
  uint32_t start = millis();

  while (start + ms > millis()) {

    // Led fade während des delays
    func();
  }
}
