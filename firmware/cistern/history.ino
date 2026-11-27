// SPDX-License-Identifier: GPL-3.0-or-later
// History: 5-min values in RAM (today, yesterday) and flash

void logHistory(const tm& t) {  // one value every 5 minutes
  int slot = t.tm_hour * 12 + t.tm_min / 5;
  if (slot == lastSlot) return;
  int prevSlot = lastSlot;
  lastSlot = slot;
  uint32_t today = (t.tm_year + 1900) * 10000 + (t.tm_mon + 1) * 100 + t.tm_mday;
  if (dayDate[dayIdx] != today) {  // new day (or reboot): load today's already saved values, if any
    dayIdx = (dayIdx + 1) % DAYS;
    dayDate[dayIdx] = 0;
    readDay(today, level[dayIdx]);
    dayDate[dayIdx] = today;
  }
  // at a 300 s interval, timing jitter can skip a slot now and then -> fill it with this (< 5 min old) value
  if (prevSlot >= 0 && slot == prevSlot + 2) level[dayIdx][slot - 1] = lastMm;
  level[dayIdx][slot] = lastMm;
  dirty[dayIdx] = true;
}

// Flash: raw partition as a ring of month blocks, no file system (LittleFS would cost ~45 KB of firmware).
// Block = 31 days x 288 x int16 at fixed offsets, then the YYYYMM it holds; 5 sectors = 20 KB, 768 KB -> 38 months.
// Erased flash reads 0xFFFF = -1 = empty, so days are written in place without erasing (NOR: bits only go 1 -> 0).
const uint32_t MONTH_BYTES = 31 * SLOTS * 2, BLOCK = 5 * 4096;
uint32_t monthAddr(uint32_t month) { return (month / 100 * 12 + month % 100) % (hist->size / BLOCK) * BLOCK; }
bool monthHere(uint32_t month) {  // block holds this month (and not the one 38 months earlier)?
  uint32_t m = 0;
  esp_partition_read(hist, monthAddr(month) + MONTH_BYTES, &m, sizeof m);
  return m == month;
}

void historyBegin() {
  hist = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "spiffs");
  if (hist && hist->size < BLOCK) hist = nullptr;
  if (!hist) Serial.println("No history partition - history in RAM only (today/yesterday)");
}

void readDay(uint32_t date, int16_t* dst) {  // YYYYMMDD from RAM (newer than flash) or flash, else -1
  for (int i = 0; i < DAYS; i++)
    if (dayDate[i] == date) { memcpy(dst, level[i], sizeof level[i]); return; }
  uint32_t month = date / 100;
  if (!hist || !monthHere(month) ||
      esp_partition_read(hist, monthAddr(month) + (date % 100 - 1) * sizeof level[0], dst, sizeof level[0]) != ESP_OK)
    memset(dst, 0xFF, sizeof level[0]);
}

void handleHistory() {  // /history?m=YYYYMM -> 31 days x 288 int16, binary little endian
  uint32_t m = server.arg("m").toInt();
  if (m < 200001 || m > 999912) { server.send(400, "text/plain", "m=YYYYMM"); return; }
  int16_t tmp[SLOTS];
  server.setContentLength(31 * sizeof tmp);
  server.send(200, "application/octet-stream", "");
  for (int day = 1; day <= 31; day++) {
    readDay(m * 100 + day, tmp);
    server.sendContent((const char*)tmp, sizeof tmp);
  }
}

void saveHistory() {  // changed days only
  int n = 0;
  for (int i = 0; i < DAYS; i++) {
    if (!dirty[i]) continue;
    uint32_t month = dayDate[i] / 100, a = monthAddr(month);
    if (!monthHere(month)) {  // new month: erase the block (drops the month 38 months back), then mark it
      if (esp_partition_erase_range(hist, a, BLOCK) != ESP_OK || esp_partition_write(hist, a + MONTH_BYTES, &month, sizeof month) != ESP_OK) continue;
    }
    a += (dayDate[i] % 100 - 1) * sizeof level[i];
    int16_t d[SLOTS];
    if (esp_partition_read(hist, a, d, sizeof d) != ESP_OK) continue;
    for (int s = 0; s < SLOTS; s++)
      if (d[s] == -1) d[s] = level[i][s];  // only fill empty slots, written ones can't change without an erase
    if (esp_partition_write(hist, a, d, sizeof d) == ESP_OK) {
      dirty[i] = false;
      n++;
    }  // otherwise retry next time
  }
  if (n) Serial.printf("History saved: %d day(s)\n", n);
}
