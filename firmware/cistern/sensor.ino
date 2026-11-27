// SPDX-License-Identifier: GPL-3.0-or-later
// ADS1115: loop current -> level

float readMa() {  // average of 16 readings, loop current in mA
  if (!adsOk) return NAN;
  float sum = 0;
  for (int i = 0; i < 16; i++) {
    sum += ads.computeVolts(ads.readADC_SingleEnded(0));
    delay(5);
  }
  return sum / 16 / shuntOhm * 1000;
}

bool maOk(float mA) { return !isnan(mA) && mA >= 3.5 && mA <= 21; }

float maToMmRaw(float mA) { return (mA - 4) / 16 * rangeMm; }  // without offset

int maToMm(float mA) {
  if (!maOk(mA)) return -1;
  return max(0L, lroundf(maToMmRaw(mA) + offsetMm));
}

const char* errorText(bool e) {
  if (!adsOk) return e ? "Error: ADS1115 not found" : "Fehler: ADS1115 nicht gefunden";
  if (lastMm < 0) return e ? "Error: current outside 3.5-21 mA (broken wire/short circuit?)"
                           : "Fehler: Strom ausserhalb 3,5-21 mA (Kabelbruch/Kurzschluss?)";
  return "";
}
