// SPDX-License-Identifier: GPL-3.0-or-later
// Settings page /config (login)

void sendConfig(const String& msg = "", bool err = false) {
  char live[80];
  if (lastMm < 0) snprintf(live, sizeof live, "%s", errorText(en));
  else snprintf(live, sizeof live, "%.2f mA = %d mm", lastMa, lastMm);
  String unchanged = T("unverändert", "unchanged");
  const char* tlsDe[] = {"Aus", "Verschlüsselt, ohne Serverprüfung", "Verschlüsselt, mit Serverprüfung"};
  const char* tlsEn[] = {"Off", "Encrypted, no server check", "Encrypted, with server check"};
  String tlsOpt;
  for (int i = 0; i < 3; i++)
    tlsOpt += "<option value='" + String(i) + "'" + (mqttTls == i ? " selected" : "") + ">" + T(tlsDe[i], tlsEn[i]) + "</option>";
  String h = head() + "<div class='hd'><h1>" + T("Einstellungen", "Settings") + "</h1><span>"
             "<a href='?lang=" + T("en'>English", "de'>Deutsch") + "</a> · <a href='/'>&larr; " + T("Füllstand", "Level") + "</a></span></div>";
  if (apMode) h += "<div class='msg err'><b>" + T("Access-Point-Modus:", "Access point mode:") + "</b> " + esc(apReason(en)) +
                   T(" Neuer Versuch alle 10 min.", " Retrying every 10 min.") + "</div>";
  if (msg.length()) h += "<div class='msg " + String(err ? "err" : "ok") + "'>" + msg + "</div>";
  String lock = defaultPass() ? " disabled" : "";  // default password: other tabs locked until it is changed
  if (defaultPass()) h += "<div class='msg err'><b>" + T("Bitte zuerst ein eigenes Passwort setzen.", "Please set your own password first.") + "</b> " +
                          T("Mit dem Standardpasswort kann jeder im Netz die Einstellungen ändern und Firmware aufspielen. "
                            "Bis dahin sind die anderen Tabs und OTA-Updates gesperrt.",
                            "With the default password, anyone on the network can change the settings and upload firmware. "
                            "Until then, the other tabs and OTA updates are locked.") + "</div>";
  // tabs: one form, so one save button covers all; tab = active tab, kept across saving
  h += "<form method='post'><input type='hidden' name='tab' id='tab' value='" + String(defaultPass() ? 2 : constrain(server.arg("tab").toInt(), 0, 2)) + "'>"
       "<nav class='seg tabs'><button type='button'" + lock + ">" + T("Verbindung", "Connectivity") + "</button>"
       "<button type='button'" + lock + ">" + T("Kalibrierung", "Calibration") + "</button>"
       "<button type='button'>" + T("Zugang", "Access") + "</button></nav>"
       // tab 1: connectivity
       "<section><div class='card'><h2>" + T("WLAN", "WiFi") + "</h2>"
       "<label>" + T("Netz (Liste oder eintippen)", "Network (pick or type)") + "<div class='scan'><input name='ssid' list='nets' value=\"" + esc(ssid) + "\">"
       "<button type='button' id='sb'>" + T("Suchen", "Scan") + "</button></div></label><datalist id='nets'></datalist>"
       "<label>" + T("Passwort", "Password") + "<input name='wpass' type='password' placeholder='" + unchanged + "' autocomplete='new-password'></label></div>"
       "<div class='card'><h2>MQTT</h2>"
       "<div class='row'><label>Broker<input name='mhost' placeholder='" + T("leer = MQTT aus", "empty = MQTT off") + "' value=\"" + esc(mqttHost) + "\"></label>"
       "<label>Port<input name='mport' type='number' min='1' max='65535' value='" + String(mqttPort) + "'></label></div>"
       "<label>TLS<select name='mtls'>" + tlsOpt + "</select></label>"
       "<label>" + T("CA-Zertifikat (PEM, nur mit Serverprüfung)", "CA certificate (PEM, server check only)") +
       "<textarea name='mca' rows='3' maxlength='3900' placeholder='-----BEGIN CERTIFICATE-----'>" + esc(mqttCa) + "</textarea></label>"
       "<p class='hint'>" + T("TLS meist an Port 8883. Mit Serverprüfung muss der Broker-Name zum Zertifikat passen.",
                              "TLS usually on port 8883. With server check, the broker name must match the certificate.") + "</p>"
       "<div class='row'><label>" + T("Benutzer", "User") + "<input name='muser' autocomplete='off' value=\"" + esc(mqttUser) + "\"></label>"
       "<label>" + T("Passwort", "Password") + "<input name='mpass' type='password' placeholder='" + unchanged + "' autocomplete='new-password'></label></div>"
       "<div class='row'><label>Topic<input name='mtopic' value=\"" + esc(mqttTopic) + "\"></label>"
       "<label>" + T("Sendeintervall (s)", "Publish interval (s)") + "<input name='mqttSec' type='number' min='10' max='3600' required value='" + String(mqttSec) + "'></label></div>"
       "<p class='hint'>" + T("Sendet den zuletzt gemessenen Wert, 10–3600 s. Kürzer als das Messintervall bringt keine neuen Werte.",
                              "Publishes the latest measured value, 10–3600 s. Shorter than the measuring interval brings no new values.") + "</p>"
       "<label>" + T("Testwert (mm)", "Test value (mm)") + "<input name='mtest' type='number' min='0' max='65535' placeholder='" + T("optional", "optional") + "'></label>"
       "<p class='hint'>" + T("Wird beim Speichern einmal gesendet, z. B. zum Testen von Home Assistant ohne Sensor. Der nächste echte Messwert ersetzt ihn. "
                              "Home Assistant findet die Zisterne automatisch (MQTT-Discovery).",
                              "Sent once on saving, e.g. to test Home Assistant without a sensor. The next real reading replaces it. "
                              "Home Assistant finds the cistern automatically (MQTT discovery).") + "</p></div></section>"
       // tab 2: calibration
       "<section><div class='card'><h2>" + T("Kalibrierung", "Calibration") + "</h2><div class='live'>" + live + "</div>"
       "<div class='mut'>" + T("aktueller Messwert", "current reading") + "</div>"
       "<div class='row'><label>" + T("Messbereich Sensor (mm)", "Sensor range (mm)") + "<input name='range' type='number' step='any' value='" + String(rangeMm) + "'></label>"
       "<label>Shunt (Ω)<input name='shunt' type='number' step='any' value='" + String(shuntOhm) + "'></label></div>"
       "<p class='hint'>" + T("Shunt = Messwiderstand auf der Platine, an dem der Sensorstrom (4–20 mA) in eine Spannung umgewandelt wird "
                              "(2 × 150 Ω parallel = 75 Ω). Muss normalerweise nicht geändert werden – nur nach Tausch des Widerstands "
                              "oder zur Feinkalibrierung mit dem per Multimeter gemessenen Wert.",
                              "Shunt = sense resistor on the board that converts the sensor current (4–20 mA) into a voltage "
                              "(2 × 150 Ω in parallel = 75 Ω). Normally no need to change it – only after replacing the resistor "
                              "or for fine calibration with the value measured by multimeter.") + "</p>"
       "<div class='row'><label>Offset (mm)<input name='offset' type='number' step='any' value='" + String(offsetMm) + "'></label>"
       "<label>" + T("oder Ist-Füllstand jetzt (mm)", "or actual level now (mm)") + "<input name='actual' type='number' step='any' placeholder='" +
       T("→ Offset berechnen", "→ compute offset") + "'></label></div>"
       "<label>" + T("Messintervall (s)", "Measuring interval (s)") + "<input name='measureSec' type='number' min='10' max='300' required value='" + String(measureSec) + "'></label>"
       "<p class='hint'>" + T("Wie oft der Sensor abgefragt wird, 10–300 s. Der Verlauf speichert unabhängig davon alle 5 min einen Wert.",
                              "How often the sensor is read, 10–300 s. The history stores a value every 5 min regardless.") + "</p></div></section>"
       // tab 3: access
       "<section><div class='card'><h2>" + T("Login Weboberfläche / OTA", "Login web interface / OTA") + "</h2>"
       "<div class='row'><label>" + T("Benutzer", "User") + "<input name='user' autocomplete='username' value=\"" + esc(webUser) + "\"></label>"
       "<label>" + T("Neues Passwort", "New password") + "<input name='pass' type='password' placeholder='" + unchanged + "' autocomplete='new-password'" + (defaultPass() ? " required" : "") + "></label></div></div></section>"
       "<div class='bar'><button class='btn'>" + T("Speichern", "Save") + "</button></div></form>"
       "<script>const S=document.querySelectorAll('section'),B=document.querySelectorAll('.tabs button'),"
       "show=i=>{tab.value=i;S.forEach((s,j)=>s.hidden=j!=i);B.forEach((b,j)=>b.className=j==i?'on':'')};"
       "B.forEach((b,i)=>b.onclick=()=>show(i));show(tab.value);"
       // WiFi list loads in the background: /scan answers 202 while scanning, max. 15 tries; button locked + spinner meanwhile
       "const done=()=>{sb.disabled=0;sb.className=''},scan=k=>{sb.disabled=1;sb.className='spin';"
       "fetch('/scan').then(r=>r.status==200?r.text().then(t=>{nets.innerHTML=t;done()}):k?setTimeout(scan,1000,k-1):done(),done)};"
       "sb.onclick=()=>scan(15);scan(15);"
       // invalid field in a hidden tab: switch there, else the browser can't show the error
       "document.forms[0].addEventListener('invalid',e=>show([...S].findIndex(s=>s.contains(e.target))),true);"
       // saving can take a few seconds (MQTT/TLS connect for the test value): lock button + spinner like the WiFi scan
       "document.forms[0].addEventListener('submit',()=>{const b=document.querySelector('.btn');b.disabled=1;b.classList.add('spin')})</script>"
       "</main></body></html>";
  server.send(200, "text/html", h);
}

void handleConfig() {
  setLang();
  if (!auth()) return;
  sendConfig();
}

void handleScan() {  // async WiFi scan (~2 s): 202 while running, then <option>s, strongest first
  if (!auth()) return;
  int n = WiFi.scanComplete();
  if (n < 0) {  // WIFI_SCAN_RUNNING, or WIFI_SCAN_FAILED = none started yet -> start one
    if (n == WIFI_SCAN_FAILED) WiFi.scanNetworks(true);
    server.send(202, "text/plain", "");
    return;
  }
  String nets, seen = "\n";
  for (int i = 0; i < n; i++) {
    String id = WiFi.SSID(i);
    if (!id.length() || seen.indexOf("\n" + id + "\n") >= 0) continue;  // skip hidden/duplicates
    seen += id + "\n";
    nets += "<option value=\"" + esc(id) + "\">" + WiFi.RSSI(i) + " dBm</option>";
  }
  WiFi.scanDelete();  // next call starts a fresh scan
  server.send(200, "text/html", nets);
}

void handleSave() {
  setLang();
  if (!auth()) return;
  if (defaultPass() && (!server.arg("pass").length() || server.arg("pass") == DEFAULT_PASS)) {
    sendConfig(T("Bitte ein anderes Passwort als das Standardpasswort wählen.", "Please choose a password other than the default."), true);
    return;
  }
  String msg = T("Gespeichert.", "Saved.");
  bool err = false;
  bool wifiChanged = server.arg("ssid") != ssid || server.arg("wpass").length();
  ssid = server.arg("ssid");
  if (server.arg("wpass").length()) wifiPass = server.arg("wpass");
  if (server.arg("user").length()) webUser = server.arg("user");
  if (server.arg("pass").length()) webPass = server.arg("pass");
  if (server.arg("range").toFloat() > 0) rangeMm = server.arg("range").toFloat();
  if (server.arg("shunt").toFloat() > 0) shuntOhm = server.arg("shunt").toFloat();
  offsetMm = server.arg("offset").toFloat();
  mqttHost = server.arg("mhost");
  if (server.arg("mport").toInt() > 0) mqttPort = server.arg("mport").toInt();
  mqttUser = server.arg("muser");
  if (server.arg("mpass").length()) mqttPass = server.arg("mpass");
  if (server.arg("mtopic").length()) mqttTopic = server.arg("mtopic");
  mqttTls = constrain(server.arg("mtls").toInt(), 0, 2);
  mqttCa = server.arg("mca");
  mqttCa.trim();
  if (server.arg("measureSec").length()) measureSec = constrain(server.arg("measureSec").toInt(), 10, 300);
  if (server.arg("mqttSec").length()) mqttSec = constrain(server.arg("mqttSec").toInt(), 10, 3600);
  if (server.arg("actual").length()) {  // measured actual level -> compute offset
    float mA = readMa();
    if (maOk(mA)) offsetMm = server.arg("actual").toFloat() - maToMmRaw(mA);
    else {
      msg = T("Sensorfehler - Ist-Wert nicht übernommen, Rest gespeichert.", "Sensor error - actual level not applied, rest saved.");
      err = true;
    }
  }
  savePrefs();
  ArduinoOTA.setPassword(webPass.c_str());
  mqtt.disconnect();                         // next publish reconnects with the new settings
  mqttApply();
  lastMa = readMa();
  lastMm = maToMm(lastMa);
  if (wifiChanged) {
    server.send(200, "text/html", head() + "<div class='msg ok'>" + T("WLAN gespeichert, starte neu …", "WiFi saved, restarting …") + "</div></main></body></html>");
    delay(1000);
    restart();
  }
  if (server.arg("mtest").length()) {  // test value: sent once now, the next regular publish replaces it
    String v = String(server.arg("mtest").toInt());
    if (!apMode && mqttHost.length() && mqttConnect() && mqtt.publish(mqttTopic.c_str(), v.c_str()))
      msg += T(" Testwert ", " Test value ") + v + T(" mm gesendet an ", " mm sent to ") + esc(mqttTopic) + ".";
    else {
      int s = mqtt.state();
      char tlsErr[80] = "";
      if (mqttTls) mqttTlsNet.lastError(tlsErr, sizeof tlsErr);  // e.g. certificate verify failed
      msg += T(" Testwert nicht gesendet: ", " Test value not sent: ") +
             (apMode ? T("Access-Point-Modus.", "access point mode.")
              : !mqttHost.length() ? T("kein Broker eingetragen.", "no broker set.")
              : mqttTls == 2 && !mqttCa.length() ? T("kein CA-Zertifikat eingetragen.", "no CA certificate set.")
              : s == 4 || s == 5 ? T("Benutzer/Passwort falsch.", "wrong user/password.")
              : String(T("Broker nicht erreichbar (Code ", "broker not reachable (code ") + s + (tlsErr[0] ? String(", ") + esc(tlsErr) : "") + ")."));
      err = true;
    }
  }
  sendConfig(msg, err);
}
