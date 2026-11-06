/*
  VOC sudden-increase detector - tolerant of a loose connection (for breadboard testing)
  Board : Arduino Uno R3
  Sensor: SGP40 breakout (I2C, address 0x59)

  Library (Sketch > Include Library > Manage Libraries):
    - "Adafruit SGP40 Sensor"  (accept the dependencies, incl. Adafruit BusIO)

  Wiring:
    SGP40 VIN -> Arduino 3V3
    SGP40 GND -> Arduino GND
    SGP40 SDA -> Arduino A4
    SGP40 SCL -> Arduino A5
  Optional: buzzer + to pin 8, - to GND (built-in LED on pin 13 also lights)

  Serial Monitor: 115200 baud
*/

#include <Wire.h>
#include "Adafruit_SGP40.h"

Adafruit_SGP40 sgp;

// ---------- Settings you can tune ----------
const uint8_t SGP40_ADDR     = 0x59;
const int   LED_PIN          = LED_BUILTIN;
const int   BUZZER_PIN       = 8;
const unsigned long SAMPLE_MS = 1000;   // algorithm expects 1 sample per second
const unsigned long WARMUP_S  = 60;     // datasheet: <60 s until VOC events are reliably detected
const float SPIKE_DELTA      = 50.0;    // alert if VOC index is this far above baseline
const float CLEAR_DELTA      = 25.0;    // alert clears when back within this of baseline
const float BASELINE_ALPHA   = 0.02;    // smaller = slower baseline
const float RATE_LIMIT       = 30.0;    // also alert on a jump this big in one second
// -------------------------------------------

float baseline = 0;
float lastVoc = 0;
bool  alarm = false;
bool  baselineReady = false;
bool  sensorOk = false;
unsigned long startMs;

// Returns true if something answers at the sensor address (works even when contact is flaky)
bool sensorPresent() {
  Wire.beginTransmission(SGP40_ADDR);
  return Wire.endTransmission() == 0;
}

// (Re)start the sensor and its VOC algorithm and warm-up timer
bool startSensor() {
  if (!sensorPresent()) return false;
  if (!sgp.begin()) return false;
  baselineReady = false;
  alarm = false;
  startMs = millis();
  return true;
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  Wire.begin();
#if defined(WIRE_HAS_TIMEOUT)
  Wire.setWireTimeout(3000, true);   // don't hang if the bus glitches
#endif

  Serial.println(F("Looking for SGP40 at 0x59 (wiggle the pins if it is not found)..."));
}

void loop() {
  static unsigned long lastSample = 0;s
  if (millis() - lastSample < SAMPLE_MS) return;
  lastSample = millis();

  // ----- Connection handling -----
  if (!sensorPresent()) {
    if (sensorOk) Serial.println(F("!!! SENSOR DISCONNECTED - check pins. Will restart warm-up when it returns."));
    else          Serial.println(F("SGP40 not found... check SDA/SCL/VIN/GND contact."));
    sensorOk = false;
    digitalWrite(LED_PIN, LOW);
    noTone(BUZZER_PIN);
    return;
  }

  if (!sensorOk) {
    if (startSensor()) {
      sensorOk = true;
      Serial.println(F("SGP40 connected. Warming up (about 60 s)..."));
    } else {
      Serial.println(F("SGP40 answers but did not start - retrying..."));
      return;
    }
  }

  // ----- Measurement -----
  int32_t voc = sgp.measureVocIndex(25, 50);   // (temperature C, humidity %RH) defaults

  unsigned long elapsed = (millis() - startMs) / 1000;
  if (elapsed < WARMUP_S || voc <= 0) {
    Serial.print(F("Warming up... "));
    Serial.print(elapsed);
    Serial.println(F(" s"));
    return;
  }

  if (!baselineReady) {
    baseline = voc;
    lastVoc = voc;
    baselineReady = true;
  }

  float delta = voc - baseline;
  float jump  = voc - lastVoc;
  lastVoc = voc;

  if (!alarm && (delta >= SPIKE_DELTA || jump >= RATE_LIMIT)) alarm = true;
  if (alarm && delta < CLEAR_DELTA) alarm = false;

  // Freeze baseline during an alarm so a spike doesn't become the new normal
  if (!alarm) baseline = baseline + BASELINE_ALPHA * (voc - baseline);

  digitalWrite(LED_PIN, alarm ? HIGH : LOW);
  if (alarm) tone(BUZZER_PIN, 2000); else noTone(BUZZER_PIN);

  Serial.print(F("VOC index: "));  Serial.print(voc);
  Serial.print(F("  baseline: ")); Serial.print(baseline, 1);
  Serial.print(F("  delta: "));    Serial.print(delta, 1);
  Serial.println(alarm ? F("  >>> VOC SPIKE DETECTED <<<") : F("  normal"));
}
