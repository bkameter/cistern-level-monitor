// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Benjamin – https://github.com/bkameter/cistern-level-monitor
// Developed with the help of Claude Code (Anthropic), https://claude.com/claude-code

#include <Wire.h>
#include <Adafruit_ADS1X15.h>

// ---- Adjust ----
const float RANGE_M       = 2.0;  // sensor range in m water column
const float SHUNT_OHM     = 75.0;  // R1 || R2 = 2x 150 Ohm
// ------------------

const int PIN_SDA = 8;    // LOLIN C3 mini
const int PIN_SCL = 10;

Adafruit_ADS1115 ads;

void setup() {
  Serial.begin(115200);
  delay(2000);                       // time to open the serial monitor
  Wire.begin(PIN_SDA, PIN_SCL);

  if (!ads.begin(0x48, &Wire)) {     // ADDR to GND -> 0x48
    Serial.println("ADS1115 nicht gefunden - Verdrahtung/Steckrichtung pruefen!");
    while (true) delay(1000);
  }
  ads.setGain(GAIN_TWO);             // range +/-2.048 V (fits 0.3-1.5 V)
  Serial.println("ADS1115 OK");
}

void loop() {
  // average 16 readings
  float sum = 0;
  for (int i = 0; i < 16; i++) {
    sum += ads.computeVolts(ads.readADC_SingleEnded(0));
    delay(5);
  }
  float u  = sum / 16.0;                   // shunt voltage in V
  float mA = u / SHUNT_OHM * 1000.0;       // loop current

  Serial.printf("U = %.3f V   I = %.2f mA   ", u, mA);

  if (mA < 3.5) {
    Serial.println("-> Fehler: kein Strom (Sensor nicht angeschlossen / Kabelbruch?)");
  } else if (mA > 21.0) {
    Serial.println("-> Fehler: Strom zu hoch (Kurzschluss?)");
  } else {
    float level = (mA - 4.0) / 16.0 * RANGE_M;
    if (level < 0) level = 0;
    Serial.printf("-> Fuellhoehe = %.3f m\n", level);
  }
  delay(2000);
}