// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Benjamin – https://github.com/bkameter/cistern-level-monitor
// Developed with the help of Claude Code (Anthropic), https://claude.com/claude-code
#define VERS "cistern-esp32 1.2, 26.11.2026"
// Cistern level gauge, LOLIN C3 mini + ADS1115 + 4-20 mA pressure sensor
// Web: / level (public), /config settings (login zisterne:zisterne), German/English (?lang=de|en); Telnet port 23, MQTT, OTA
// History: 5-min values, 38 months in the raw "spiffs" partition, served as /history?m=YYYYMM (31x288 int16, -1 = empty)
// No/wrong WiFi -> access point "Zisterne-Setup" (PW zisterne), settings at http://192.168.4.1/config
// Factory reset: hold BOOT button for 2 s within the first 5 s after power-up
// Code: sensor, wifi, web (level page), settings (/config), mqtt, history – each its own .ino tab

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include <time.h>
#include <esp_partition.h>

const char* HOSTNAME    = "Zisterne";
const char* AP_SSID     = "Zisterne-Setup";
const char* AP_PASS     = "zisterne";        // WPA2: min. 8 characters
const char* DEFAULT_PASS = "zisterne";       // web/OTA; while unchanged: /config only offers "Access", OTA is off
const char* NTP_SERVER  = "de.pool.ntp.org";
const char* TZ_INFO     = "CET-1CEST,M3.5.0/02,M10.5.0/03";
const int PIN_SDA = 8, PIN_SCL = 10, PIN_BOOT = 9;
// ESP32-C3 boards often transmit a distorted signal at full power: reception is fine, but phones
// don't see the AP or the router connection fails. 8.5 dBm usually fixes it. Try WIFI_POWER_19_5dBm (max).
const wifi_power_t TX_POWER = WIFI_POWER_8_5dBm;
const float SMOOTHING_S = 100;  // smoothing time constant in s, independent of the measuring interval

Preferences prefs;
String ssid, wifiPass, webUser, webPass;
String mqttHost, mqttUser, mqttPass, mqttTopic;   // empty mqttHost = MQTT off
int mqttPort;
int mqttTls;    // 0 = plain, 1 = TLS without server check, 2 = TLS, server checked against mqttCa
String mqttCa;  // PEM, CA certificate of the broker (mode 2)
int measureSec, mqttSec;   // intervals: measuring 10-300 s (history needs a value every 5 min), MQTT 10-3600 s
float rangeMm, shuntOhm, offsetMm;   // calibration, adjustable via /config
// shuntOhm: sense resistor converting the 4-20 mA loop current into a voltage for the ADS1115 (I = U / R).
// Fixed hardware value, only change it after swapping the resistor or to enter its measured value.

Adafruit_ADS1115 ads;
bool adsOk = false;
WebServer server(80);
WiFiServer telnet(23);
WiFiClient telnetClient;
WiFiClient mqttNet;
WiFiClientSecure mqttTlsNet;
PubSubClient mqtt(mqttNet);
bool apMode = false;
wl_status_t apStatus;         // why AP mode, see apReason()
bool defaultPass() { return webPass == DEFAULT_PASS; }
bool en = false;              // language of the current web request, see setLang()

float lastMa = NAN;
int lastMm = -1;              // -1 = measurement error
const int DAYS = 2;           // RAM holds only today + yesterday (until next save), the rest lives in flash
const int SLOTS = 288;        // 5-minute values per day
int16_t level[DAYS][SLOTS];   // days x 5-min values, -1 = no value
uint32_t dayDate[DAYS];       // YYYYMMDD, 0 = unused
bool dirty[DAYS];             // changed since last save
int dayIdx = 0, lastSlot = -1;
const esp_partition_t* hist = nullptr;  // history partition, nullptr = RAM only
char buf[1200];               // current report for Serial, Telnet

//*********************************************************************
void loadPrefs() {  // keys are persisted: renaming one loses its stored value
  ssid      = prefs.getString("ssid", "");
  wifiPass  = prefs.getString("wifiPass", "");
  webUser   = prefs.getString("webUser", "zisterne");
  webPass   = prefs.getString("webPass", DEFAULT_PASS);
  rangeMm   = prefs.getFloat("rangeMm", 2000);  // sensor 0-2 m water column
  shuntOhm  = prefs.getFloat("shuntOhm", 75);      // 2x 150 ohm in parallel
  offsetMm  = prefs.getFloat("offsetMm", 0);
  mqttHost  = prefs.getString("mqttHost", "");
  mqttPort  = prefs.getInt("mqttPort", 1883);
  mqttUser  = prefs.getString("mqttUser", "");
  mqttPass  = prefs.getString("mqttPass", "");
  mqttTopic = prefs.getString("mqttTopic", "Wasserstand-Zisterne");
  mqttTls   = prefs.getInt("mqttTls", 0);
  mqttCa    = prefs.getString("mqttCa", "");
  measureSec = prefs.getInt("measureSec", 10);
  mqttSec   = prefs.getInt("mqttSec", 60);
}

void savePrefs() {
  prefs.putString("ssid", ssid);
  prefs.putString("wifiPass", wifiPass);
  prefs.putString("webUser", webUser);
  prefs.putString("webPass", webPass);
  prefs.putFloat("rangeMm", rangeMm);
  prefs.putFloat("shuntOhm", shuntOhm);
  prefs.putFloat("offsetMm", offsetMm);
  prefs.putString("mqttHost", mqttHost);
  prefs.putInt("mqttPort", mqttPort);
  prefs.putString("mqttUser", mqttUser);
  prefs.putString("mqttPass", mqttPass);
  prefs.putString("mqttTopic", mqttTopic);
  prefs.putInt("mqttTls", mqttTls);
  prefs.putString("mqttCa", mqttCa);
  prefs.putInt("measureSec", measureSec);
  prefs.putInt("mqttSec", mqttSec);
}

void restart() {  // planned restart: save history first
  if (hist) saveHistory();
  ESP.restart();
}

void report(const tm& t) {
  int p = snprintf(buf, sizeof buf, "C I S T E R N   L E V E L   (%s)\r\n"
                   "=============================\r\n\r\n"
                   "%02d.%02d.%04d-%02d:%02d:%02d;  0_h     4_h     8_h    12_h    16_h    20_h\r\n",
                   VERS, t.tm_mday, t.tm_mon + 1, t.tm_year + 1900, t.tm_hour, t.tm_min, t.tm_sec);
  for (int n = DAYS - 1; n >= 0; n--) {  // days in RAM (yesterday, today)
    int d = (dayIdx + DAYS - n) % DAYS;
    if (!dayDate[d]) continue;
    p += snprintf(buf + p, sizeof buf - p, "Day %02d.%02d.           %5d;  %5d;  %5d;  %5d;  %5d;  %5d\r\n",
                  (int)(dayDate[d] % 100), (int)(dayDate[d] / 100 % 100), level[d][0], level[d][48], level[d][96], level[d][144], level[d][192], level[d][240]);  // Telnet: 4 h grid only
  }
  snprintf(buf + p, sizeof buf - p, "C U R R E N T: %.2f mA;  %d mm;  RSSI %d dBm%s\r\n%s\r\n",
           lastMa, lastMm, WiFi.RSSI(), apMode ? (";  ACCESS POINT MODE: " + apReason(true)).c_str() : "", errorText(true));
}

//*********************************************************************
void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(PIN_BOOT, INPUT_PULLUP);
  prefs.begin("zisterne");

  digitalWrite(LED_BUILTIN, HIGH);  // LED on: 5 s window for factory reset via BOOT button
  int pressed = 0;                  // only on a 2 s continuous press, not on a glitch
  while (millis() < 5000 || pressed) {
    pressed = digitalRead(PIN_BOOT) == LOW ? pressed + 1 : 0;
    if (pressed >= 200) {
      prefs.clear();
      Serial.println("Factory reset done");
      break;
    }
    delay(10);
  }
  digitalWrite(LED_BUILTIN, LOW);
  Serial.printf("\nCistern level, %s\n", VERS);
  loadPrefs();
  historyBegin();

  Wire.begin(PIN_SDA, PIN_SCL);
  adsOk = ads.begin(0x48, &Wire);   // ADDR to GND
  if (adsOk) ads.setGain(GAIN_TWO); // +/-2.048 V, fits 0.3-1.5 V
  else Serial.println("ADS1115 not found - check wiring!");

  wifiStart();
  configTzTime(TZ_INFO, NTP_SERVER);
  MDNS.begin(HOSTNAME);

  const char* hdrs[] = {"Cookie", "Accept-Language"};  // for setLang()
  server.collectHeaders(hdrs, 2);
  server.on("/", HTTP_GET, handleRoot);
  server.on("/history", HTTP_GET, handleHistory);
  server.on("/config", HTTP_GET, handleConfig);
  server.on("/config", HTTP_POST, handleSave);
  server.on("/scan", HTTP_GET, handleScan);
  server.begin();
  telnet.begin();

  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.setPassword(webPass.c_str());
  ArduinoOTA.onStart([] {  // ESP reboots after the update -> save history first
    if (hist && ArduinoOTA.getCommand() == U_FLASH) saveHistory();
  });
  ArduinoOTA.begin();

  mqttApply();
  mqtt.setBufferSize(768);                     // discovery message ~400 bytes, default 256 is too small
  digitalWrite(LED_BUILTIN, apMode);  // LED stays on in AP mode
}

void loop() {
  if (!apMode && !defaultPass()) ArduinoOTA.handle();  // OTA off in AP mode (public AP password) and with the default password
  server.handleClient();
  mqtt.loop();
  if (telnet.hasClient()) telnetClient = telnet.accept();  // new client replaces the old one
  // AP mode because the router was down at boot? Retry after 10 min.
  if (apMode && ssid.length() && millis() > 600000) restart();

  static unsigned long tMqtt = 0;  // MQTT: publishes the latest value, independent of the measuring interval
  if (!apMode && lastMm >= 0 && mqttHost.length() && millis() - tMqtt >= mqttSec * 1000UL) {
    tMqtt = millis();
    if (mqttConnect()) mqtt.publish(mqttTopic.c_str(), String(lastMm).c_str());
  }

  static unsigned long tLast = 0;
  if (millis() - tLast < measureSec * 1000UL) return;
  tLast = millis();

  digitalWrite(LED_BUILTIN, !apMode);
  float mA = readMa();
  // exponential moving average with a fixed time constant; pass sensor errors through immediately
  float k = 1 - expf(-measureSec / SMOOTHING_S);  // 10 s -> 0.1, 300 s -> 0.95
  lastMa = (maOk(mA) && maOk(lastMa)) ? lastMa + k * (mA - lastMa) : mA;
  int mm = maToMm(lastMa);
  if (mm < 0 || lastMm < 0 || abs(mm - lastMm) > 1) lastMm = mm;  // 1 mm dead band against flicker
  tm t;
  if (getLocalTime(&t, 0)) logHistory(t);  // only after NTP sync
  static unsigned long tSave = 15 * 60000UL;  // first save 15 min after boot, then every 4 h
  if (hist && (long)(millis() - tSave) >= 0) {
    saveHistory();
    tSave = millis() + 4 * 3600000UL;
  }
  report(t);
  Serial.print(buf);
  if (telnetClient.connected()) telnetClient.print(buf);
  digitalWrite(LED_BUILTIN, apMode);
}
