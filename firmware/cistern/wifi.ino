// SPDX-License-Identifier: GPL-3.0-or-later
// WiFi station, fallback access point

void wifiStart() {
  WiFi.setHostname(HOSTNAME);
  if (ssid.length()) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), wifiPass.c_str());
    WiFi.setTxPower(TX_POWER);
    for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) delay(500);
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("WiFi connected, http://%s\n", WiFi.localIP().toString().c_str());
  } else {
    apStatus = WiFi.status();
    WiFi.disconnect();
    int n = WiFi.scanNetworks();  // diagnostics: what does the ESP receive at all?
    Serial.printf("WiFi scan: %d networks\n", n);
    for (int i = 0; i < n; i++) Serial.printf("  %4d dBm  channel %2d  %s\n", WiFi.RSSI(i), WiFi.channel(i), WiFi.SSID(i).c_str());
    WiFi.scanDelete();
    apMode = true;
    WiFi.mode(WIFI_AP);
    bool apOk = WiFi.softAP(AP_SSID, AP_PASS);
    WiFi.setTxPower(TX_POWER);
    Serial.printf("softAP %s, channel %d, TX power %.1f dBm\n", apOk ? "OK" : "ERROR", WiFi.channel(), WiFi.getTxPower() / 4.0);
    Serial.println(apReason(true));
    Serial.printf("No WiFi -> access point '%s', http://%s/config\n", AP_SSID, WiFi.softAPIP().toString().c_str());
  }
}

String apReason(bool e) {  // shown on /config and Telnet
  String s = "'" + ssid + "'";
  if (!ssid.length()) return e ? "No WiFi configured." : "Kein WLAN eingestellt.";
  if (apStatus == WL_NO_SSID_AVAIL) return e ? "WiFi " + s + " not found (router off / too far away?)."
                                             : "WLAN " + s + " nicht gefunden (Router aus / zu weit weg?).";
  if (apStatus == WL_CONNECT_FAILED) return e ? "Connection to " + s + " refused (wrong password?)."
                                              : "Verbindung zu " + s + " abgelehnt (Passwort falsch?).";
  return (e ? "No connection to " + s + " after 20 s (status " : "Keine Verbindung zu " + s + " nach 20 s (Status ") + String(apStatus) + ").";
}
