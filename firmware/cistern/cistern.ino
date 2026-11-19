// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Benjamin – https://github.com/bkameter/cistern-level-monitor
// Developed with the help of Claude Code (Anthropic), https://claude.com/claude-code
#define VERS "cistern-esp32 1.0, 08.10.2026"
// Cistern level gauge, LOLIN C3 mini + ADS1115 + 4-20 mA pressure sensor
// Web: / level (public), /config settings (login zisterne:zisterne); Telnet port 23, MQTT, OTA
// History: 5-min values, 1 year in LittleFS (/h/YYYYMM.bin), served as /history?m=YYYYMM (31x288 int16, -1 = empty)
// No/wrong WiFi -> access point "Zisterne-Setup" (PW zisterne), settings at http://192.168.4.1/config
// Factory reset: hold BOOT button for 2 s within the first 5 s after power-up

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include <time.h>
#include <LittleFS.h>
#include <vector>

const char* HOSTNAME    = "Zisterne";
const char* AP_SSID     = "Zisterne-Setup";
const char* AP_PASS     = "zisterne";        // WPA2: min. 8 characters
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
PubSubClient mqtt(mqttNet);
bool apMode = false;
String apReason;              // why AP mode, shown on /config

float lastMa = NAN;
int lastMm = -1;              // -1 = measurement error
const int DAYS = 2;           // RAM holds only today + yesterday (until next save), the rest lives in LittleFS
const int SLOTS = 288;        // 5-minute values per day
int16_t level[DAYS][SLOTS];   // days x 5-min values, -1 = no value
uint32_t dayDate[DAYS];       // YYYYMMDD, 0 = unused
bool dirty[DAYS];             // changed since last save
int dayIdx = 0, lastSlot = -1;
bool fsOk = false;
char buf[1200];               // current report for Serial, Telnet

//*********************************************************************
void loadPrefs() {  // keys are persisted: renaming one loses its stored value
  ssid      = prefs.getString("ssid", "");
  wifiPass  = prefs.getString("wifiPass", "");
  webUser   = prefs.getString("webUser", "zisterne");
  webPass   = prefs.getString("webPass", "zisterne");
  rangeMm   = prefs.getFloat("rangeMm", 2000);  // sensor 0-2 m water column
  shuntOhm  = prefs.getFloat("shuntOhm", 75);      // 2x 150 ohm in parallel
  offsetMm  = prefs.getFloat("offsetMm", 0);
  mqttHost  = prefs.getString("mqttHost", "");
  mqttPort  = prefs.getInt("mqttPort", 1883);
  mqttUser  = prefs.getString("mqttUser", "");
  mqttPass  = prefs.getString("mqttPass", "");
  mqttTopic = prefs.getString("mqttTopic", "Wasserstand-Zisterne");
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
  prefs.putInt("measureSec", measureSec);
  prefs.putInt("mqttSec", mqttSec);
}

//*********************************************************************
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

//*********************************************************************
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
    wl_status_t st = WiFi.status();
    apReason = !ssid.length()            ? "Kein WLAN eingestellt."
             : st == WL_NO_SSID_AVAIL    ? "WLAN '" + ssid + "' nicht gefunden (Router aus / zu weit weg?)."
             : st == WL_CONNECT_FAILED   ? "Verbindung zu '" + ssid + "' abgelehnt (Passwort falsch?)."
             : "Keine Verbindung zu '" + ssid + "' nach 20 s (Status " + String(st) + ").";
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
    Serial.println(apReason);
    Serial.printf("No WiFi -> access point '%s', http://%s/config\n", AP_SSID, WiFi.softAPIP().toString().c_str());
  }
}

//*********************************************************************
#define CSS \
  ":root{color-scheme:light dark;--bg:#f6f7f9;--card:#fff;--fg:#1d2330;--mut:#6b7280;--line:#e5e7eb;--acc:#1f77d0}" \
  "@media(prefers-color-scheme:dark){:root{--bg:#12151b;--card:#1b2029;--fg:#e6e9ef;--mut:#9aa3b2;--line:#2c3340;--acc:#5aa9f0}}" \
  "body{margin:0;padding:16px;background:var(--bg);color:var(--fg);font:15px/1.4 system-ui,sans-serif}" \
  "main{max-width:760px;margin:auto}h1{font-size:20px;margin:0 0 12px}h2{font-size:16px;margin:0}" \
  ".card{background:var(--card);border-radius:10px;padding:16px;margin-bottom:16px;box-shadow:0 1px 3px #0002}" \
  ".big{font-size:44px;font-weight:600}.mut{color:var(--mut);font-size:13px}.err{color:#d33}a{color:var(--acc)}" \
  ".hd{display:flex;justify-content:space-between;align-items:center;gap:8px;margin-bottom:12px}.hd h1{margin:0}" \
  /* chart */ \
  ".seg{display:flex;background:var(--bg);border-radius:8px;padding:2px}" \
  ".seg button{padding:4px 10px;border:0;border-radius:6px;background:none;color:var(--mut);font:inherit;font-size:13px;cursor:pointer}" \
  ".seg button.on{background:var(--card);color:var(--fg);box-shadow:0 1px 2px #0003}" \
  ".nv{display:flex;gap:8px;align-items:center;margin-bottom:8px}.nv input{margin:0;text-align:center}" \
  ".nv button{background:var(--bg);color:var(--fg);border:1px solid var(--line);border-radius:8px;padding:8px 14px;font:inherit;cursor:pointer}" \
  ".nv button:disabled{opacity:.4;cursor:default}" \
  ".cw{position:relative}svg{width:100%;height:auto;overflow:visible;touch-action:pan-y}svg text{fill:var(--mut);font-size:11px}" \
  ".g{stroke:var(--line)}.l{fill:none;stroke:var(--acc);stroke-width:2;stroke-linejoin:round}circle{fill:var(--acc)}" \
  ".hl{stroke:var(--mut);stroke-dasharray:3 3}#hc{fill:var(--card);stroke:var(--acc);stroke-width:2.5}" \
  ".tip{position:absolute;pointer-events:none;background:var(--fg);color:var(--bg);padding:6px 9px;border-radius:6px;font-size:13px;white-space:nowrap}" \
  /* form */ \
  "label{display:block;margin-top:12px;font-size:13px;color:var(--mut)}" \
  "input{display:block;width:100%;box-sizing:border-box;margin-top:4px;padding:9px 10px;font:inherit;color:var(--fg);" \
  "background:var(--bg);border:1px solid var(--line);border-radius:8px}" \
  "input:focus{outline:2px solid var(--acc);outline-offset:-1px}" \
  ".row{display:grid;grid-template-columns:repeat(auto-fit,minmax(140px,1fr));gap:0 12px}" \
  ".live{font-size:22px;font-weight:600;margin-top:4px}.hint{margin:6px 0 0;font-size:12px;color:var(--mut)}" \
  ".msg{padding:10px 14px;border-radius:8px;margin-bottom:16px}.ok{background:#2e9d4f22;border:1px solid #2e9d4f66}" \
  ".msg.err{color:inherit;background:#d3333322;border:1px solid #d3333366}" \
  ".bar{position:sticky;bottom:0;padding:12px 0;background:var(--bg)}" \
  ".btn{background:var(--acc);color:var(--card);border:0;border-radius:8px;padding:10px 24px;font:inherit;font-weight:600;cursor:pointer}"

const char* HEAD = "<!DOCTYPE html><html lang='de'><head><meta charset='utf-8'>"
  "<meta name='viewport' content='width=device-width,initial-scale=1'><title>Zisterne</title>"
  "<style>" CSS "</style></head><body><main>";

bool auth() {
  if (server.authenticate(webUser.c_str(), webPass.c_str())) return true;
  server.requestAuthentication(BASIC_AUTH, "Zisterne");
  return false;
}

String esc(String s) {
  s.replace("&", "&amp;"); s.replace("\"", "&quot;"); s.replace("<", "&lt;");
  return s;
}

const char PAGE[] = R"HTML(<!DOCTYPE html><html lang="de"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Zisterne</title><style>)HTML" CSS R"HTML(</style></head><body><main><h1>Zisterne</h1>
<div class="card"><div class="big" id="cur"></div><div class="mut" id="info"></div></div>
<div class="card"><div class="hd"><span class="mut">Verlauf (mm)</span>
<nav class="seg"><button data-m="d">Tag</button><button data-m="w">Woche</button><button data-m="m">Monat</button></nav></div>
<div class="nv"><button id="prev" aria-label="zurück">‹</button><input type="date" id="date"><button id="next" aria-label="vor">›</button></div>
<div class="cw"><svg id="chart" viewBox="0 0 600 260"></svg><div id="tip" class="tip" hidden></div></div></div>
<p class="mut"><a href="/config">Einstellungen</a></p></main>
<script>/*DATA*/
cur.textContent=A.mm>=0?A.mm+' mm':'Fehler';
info.innerHTML=(A.ma>=0?A.ma.toFixed(2)+' mA · ':'')+A.time+(A.err?'<br><span class="err">'+A.err+'</span>':'');
// days as day numbers (UTC, since 1970); value index = day number*288 + 5-min slot
const J=new Date(),TODAY=Date.UTC(J.getFullYear(),J.getMonth(),J.getDate())/864e5,MIN=TODAY-366,
 dt=n=>new Date(n*864e5),iso=n=>dt(n).toISOString().slice(0,10),
 fmtDay=n=>dt(n).toLocaleDateString('de',{day:'2-digit',month:'2-digit',timeZone:'UTC'}),
 monthKey=n=>dt(n).getUTCFullYear()*100+dt(n).getUTCMonth()+1,  // YYYYMM
 cache={},month=k=>cache[k]||(cache[k]=fetch('/history?m='+k).then(r=>r.ok?r.arrayBuffer():Promise.reject())
  .then(b=>new Int16Array(b)).catch(()=>{delete cache[k];return new Int16Array(0)}));
function state(){  // from #hash: view d/w/m and anchor date, e.g. #m,2026-03-15
 const[m,a]=location.hash.slice(1).split(','),n=a?Math.min(TODAY,Math.max(MIN,Date.parse(a)/864e5)):TODAY;
 return{m:m&&'dwm'.includes(m)?m:'w',a:n||TODAY}}
function span(m,a){  // [first, last day]
 if(m=='d')return[a,a];if(m=='w')return[a-6,a];
 const d=dt(a),y=d.getUTCFullYear(),o=d.getUTCMonth();return[Date.UTC(y,o,1)/864e5,Date.UTC(y,o+1,1)/864e5-1]}
const setView=(m,a)=>location.hash=m+(a==TODAY?'':','+iso(a));
document.querySelectorAll('.seg button').forEach(b=>b.onclick=()=>setView(b.dataset.m,state().a));
date.min=iso(MIN);date.max=iso(TODAY);date.onchange=()=>date.value&&setView(state().m,Date.parse(date.value)/864e5);
prev.onclick=()=>{const{m,a}=state(),[f,l]=span(m,a);setView(m,Math.max(MIN,m=='m'?f-1:a-(l-f+1)))};
next.onclick=()=>{const{m,a}=state(),[f,l]=span(m,a);setView(m,Math.min(TODAY,m=='m'?l+1:a+(l-f+1)))};
const W=600,H=260,L=44,R=12,T=12,B=24;
async function draw(){
 const h0=location.hash,{m,a}=state(),[first,last]=span(m,a);
 document.querySelectorAll('.seg button').forEach(b=>b.className=b.dataset.m==m?'on':'');
 date.value=iso(a);prev.disabled=first<=MIN;next.disabled=last>=TODAY;tip.hidden=true;
 const ks=[...new Set([monthKey(first),monthKey(last)])],arr=await Promise.all(ks.map(month)),P=[];
 if(location.hash!=h0)return;  // user navigated on meanwhile
 ks.forEach((k,j)=>{const b=Date.UTC(k/100|0,k%100-1,1)/864e5*288;
  arr[j].forEach((v,s)=>{if(v>=0&&b+s>=first*288&&b+s<(last+1)*288)P.push([b+s,v])})});
 if(!P.length){chart.innerHTML='<text x="300" y="130" text-anchor="middle">Keine Daten in diesem Zeitraum</text>';return}
 const x0=first*288,x1=(last+1)*288-1,
  mn=Math.min(...P.map(p=>p[1])),mx=Math.max(...P.map(p=>p[1])),
  S=[10,20,50,100,200,500,1000].find(s=>s*4>=mx-mn)||2000,  // grid step, y axis adapts to the visible range
  lo=Math.floor(mn/S)*S,hi=Math.max(Math.ceil(mx/S)*S,lo+S),
  x=i=>L+(W-L-R)*(i-x0)/Math.max(x1-x0,1),y=v=>T+(H-T-B)*(hi-v)/(hi-lo);
 let s='',d='';
 for(let v=lo;v<=hi;v+=S)s+=`<line class="g" x1="${L}" x2="${W-R}" y1="${y(v)}" y2="${y(v)}"/><text x="${L-6}" y="${y(v)+4}" text-anchor="end">${v}</text>`;
 const st=x1-x0<288?72:288*Math.ceil((last-first+1)/8);  // x labels: 6 h grid (day) or at most 8 days
 for(let i=x0;i<=x1;i+=st)s+=`<text x="${x(i)}" y="${H-6}" text-anchor="middle">${i%288?i%288/12+' Uhr':fmtDay(i/288)}</text>`;
 P.forEach((p,j)=>d+=(j&&p[0]-P[j-1][0]==1?'L':'M')+x(p[0]).toFixed(1)+','+y(p[1]).toFixed(1));  // don't bridge gaps
 s+=`<path class="l" d="${d}"/>`;
 if(P.length<60)s+=P.map(p=>`<circle cx="${x(p[0])}" cy="${y(p[1])}" r="3"/>`).join('');
 chart.innerHTML=s+`<line id="hl" class="hl" y1="${T}" y2="${H-B}" visibility="hidden"/><circle id="hc" r="5" visibility="hidden"/>`;
 chart.onpointermove=chart.onpointerdown=e=>{  // mouse: hover and click, touch: tap/swipe; tip stays visible
  const b=chart.getBoundingClientRect(),i=x0+((e.clientX-b.left)/b.width*W-L)/(W-L-R)*(x1-x0),
   p=P.reduce((a,q)=>Math.abs(q[0]-i)<Math.abs(a[0]-i)?q:a),px=x(p[0]),py=y(p[1]);
  hl.setAttribute('x1',px);hl.setAttribute('x2',px);hc.setAttribute('cx',px);hc.setAttribute('cy',py);
  hl.setAttribute('visibility','visible');hc.setAttribute('visibility','visible');
  tip.innerHTML=`<b>${p[1]} mm</b><br>${fmtDay(p[0]/288|0)} ${p[0]%288/12|0}:${String(p[0]%12*5).padStart(2,'0')} Uhr`;tip.hidden=false;
  tip.style.left=px/W*b.width+'px';tip.style.top=py/H*b.height+'px';
  tip.style.transform=px>W/2?'translate(calc(-100% - 12px),-50%)':'translate(12px,-50%)'};
 chart.onpointerleave=e=>{if(e.pointerType=='mouse'){tip.hidden=true;hl.setAttribute('visibility','hidden');hc.setAttribute('visibility','hidden')}};
}
onhashchange=draw;draw();
setInterval(()=>{if(tip.hidden&&state().a==TODAY)location.reload()},60000);  // only in the current view and without an open tip
</script></body></html>)HTML";

const char* errorText() {
  if (!adsOk) return "Fehler: ADS1115 nicht gefunden";
  if (lastMm < 0) return "Fehler: Strom ausserhalb 3,5-21 mA (Kabelbruch/Kurzschluss?)";
  return "";
}

void handleRoot() {  // no login
  tm t;
  char stamp[40] = "Uhrzeit noch nicht synchronisiert";
  if (getLocalTime(&t, 0)) strftime(stamp, sizeof stamp, "Stand %d.%m.%Y %H:%M", &t);
  // send in chunks, avoids a String copy of the page; the browser fetches history via /history
  const char* mark = strstr(PAGE, "/*DATA*/");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");
  server.sendContent(PAGE, mark - PAGE);
  server.sendContent("const A={mm:" + String(lastMm) + ",ma:" + String(isnan(lastMa) ? -1 : lastMa, 2) +
                     ",time:\"" + stamp + "\",err:\"" + errorText() + "\"};");
  server.sendContent(mark + 8, strlen(mark + 8));  // rest of the page after /*DATA*/
  server.sendContent("");                          // end
}

void sendConfig(const String& msg) {
  char live[80];
  if (lastMm < 0) snprintf(live, sizeof live, "%s", errorText());
  else snprintf(live, sizeof live, "%.2f mA = %d mm", lastMa, lastMm);
  String nets = "<datalist id='nets'>", seen = "\n";  // WiFi scan, takes ~2 s, strongest first
  for (int i = 0, n = WiFi.scanNetworks(); i < n; i++) {
    String id = WiFi.SSID(i);
    if (!id.length() || seen.indexOf("\n" + id + "\n") >= 0) continue;  // skip hidden/duplicates
    seen += id + "\n";
    nets += "<option value=\"" + esc(id) + "\">" + WiFi.RSSI(i) + " dBm</option>";
  }
  WiFi.scanDelete();
  nets += "</datalist>";
  String h = String(HEAD) + "<div class='hd'><h1>Einstellungen</h1><a href='/'>&larr; Füllstand</a></div>";
  if (apMode) h += "<div class='msg err'><b>Access-Point-Modus:</b> " + esc(apReason) + " Neuer Versuch alle 10 min.</div>";
  if (msg.length()) h += "<div class='msg " + String(msg.startsWith("Gespeichert") ? "ok" : "err") + "'>" + msg + "</div>";
  h += "<form method='post'>"
       "<div class='card'><h2>WLAN</h2>"
       "<label>Netz (Liste oder eintippen)<input name='ssid' list='nets' value=\"" + esc(ssid) + "\"></label>" + nets +
       "<label>Passwort<input name='wpass' type='password' placeholder='unverändert' autocomplete='new-password'></label></div>"
       "<div class='card'><h2>Kalibrierung</h2><div class='live'>" + live + "</div><div class='mut'>aktueller Messwert</div>"
       "<div class='row'><label>Messbereich Sensor (mm)<input name='range' type='number' step='any' value='" + String(rangeMm) + "'></label>"
       "<label>Shunt (Ω)<input name='shunt' type='number' step='any' value='" + String(shuntOhm) + "'></label></div>"
       "<p class='hint'>Shunt = Messwiderstand auf der Platine, an dem der Sensorstrom (4–20 mA) in eine Spannung umgewandelt wird "
       "(2 × 150 Ω parallel = 75 Ω). Muss normalerweise nicht geändert werden – nur nach Tausch des Widerstands "
       "oder zur Feinkalibrierung mit dem per Multimeter gemessenen Wert.</p>"
       "<div class='row'><label>Offset (mm)<input name='offset' type='number' step='any' value='" + String(offsetMm) + "'></label>"
       "<label>oder Ist-Füllstand jetzt (mm)<input name='actual' type='number' step='any' placeholder='→ Offset berechnen'></label></div>"
       "<label>Messintervall (s)<input name='measureSec' type='number' min='10' max='300' required value='" + String(measureSec) + "'></label>"
       "<p class='hint'>Wie oft der Sensor abgefragt wird, 10–300 s. Der Verlauf speichert unabhängig davon alle 5 min einen Wert.</p></div>"
       "<div class='card'><h2>MQTT</h2>"
       "<div class='row'><label>Broker<input name='mhost' placeholder='leer = MQTT aus' value=\"" + esc(mqttHost) + "\"></label>"
       "<label>Port<input name='mport' type='number' min='1' max='65535' value='" + String(mqttPort) + "'></label></div>"
       "<div class='row'><label>Benutzer<input name='muser' autocomplete='off' value=\"" + esc(mqttUser) + "\"></label>"
       "<label>Passwort<input name='mpass' type='password' placeholder='unverändert' autocomplete='new-password'></label></div>"
       "<div class='row'><label>Topic<input name='mtopic' value=\"" + esc(mqttTopic) + "\"></label>"
       "<label>Sendeintervall (s)<input name='mqttSec' type='number' min='10' max='3600' required value='" + String(mqttSec) + "'></label></div>"
       "<p class='hint'>Sendet den zuletzt gemessenen Wert, 10–3600 s. Kürzer als das Messintervall bringt keine neuen Werte.</p></div>"
       "<div class='card'><h2>Login Weboberfläche / OTA</h2>"
       "<div class='row'><label>Benutzer<input name='user' autocomplete='username' value=\"" + esc(webUser) + "\"></label>"
       "<label>Neues Passwort<input name='pass' type='password' placeholder='unverändert' autocomplete='new-password'></label></div></div>"
       "<div class='bar'><button class='btn'>Speichern</button></div></form></main></body></html>";
  server.send(200, "text/html", h);
}

void handleConfig() {
  if (!auth()) return;
  sendConfig("");
}

void handleSave() {
  if (!auth()) return;
  String msg = "Gespeichert.";
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
  if (server.arg("measureSec").length()) measureSec = constrain(server.arg("measureSec").toInt(), 10, 300);
  if (server.arg("mqttSec").length()) mqttSec = constrain(server.arg("mqttSec").toInt(), 10, 3600);
  if (server.arg("actual").length()) {  // measured actual level -> compute offset
    float mA = readMa();
    if (maOk(mA)) offsetMm = server.arg("actual").toFloat() - maToMmRaw(mA);
    else msg = "Sensorfehler - Ist-Wert nicht uebernommen, Rest gespeichert.";
  }
  savePrefs();
  ArduinoOTA.setPassword(webPass.c_str());
  mqtt.disconnect();                         // next publish reconnects with the new settings
  mqtt.setServer(mqttHost.c_str(), mqttPort);
  lastMa = readMa();
  lastMm = maToMm(lastMa);
  if (wifiChanged) {
    server.send(200, "text/html", String(HEAD) + "<div class='msg ok'>WLAN gespeichert, starte neu …</div></main></body></html>");
    delay(1000);
    restart();
  }
  sendConfig(msg);
}

//*********************************************************************
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

// Flash: one file per month /h/YYYYMM.bin, 31 days x 288 x int16 = 18 KB, each day at a fixed offset.
// (Daily files would exceed the LittleFS inline limit of 512 B and occupy a full 4 KB block each.)
void monthFile(char* path, uint32_t month) { sprintf(path, "/h/%06u.bin", (unsigned)month); }

void readDay(uint32_t date, int16_t* dst) {  // YYYYMMDD from RAM (newer than flash) or flash, else -1
  for (int i = 0; i < DAYS; i++)
    if (dayDate[i] == date) { memcpy(dst, level[i], sizeof level[i]); return; }
  char path[20];
  monthFile(path, date / 100);
  File f = fsOk && LittleFS.exists(path) ? LittleFS.open(path) : File();
  if (!f || !f.seek((date % 100 - 1) * sizeof level[0]) || f.read((uint8_t*)dst, sizeof level[0]) != sizeof level[0])
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

void restart() {  // planned restart: save history first
  if (fsOk) saveHistory();
  ESP.restart();
}

void saveHistory() {  // changed days only
  int n = 0;
  for (int i = 0; i < DAYS; i++) {
    if (!dirty[i]) continue;
    uint32_t month = dayDate[i] / 100;
    char path[20];
    monthFile(path, month);
    if (!LittleFS.exists(path)) {  // new month: delete files older than 1 year, prefill the new one with -1
      std::vector<String> old;     // collect first, deleting while iterating is unsafe
      File dir = LittleFS.open("/h");
      for (File f = dir.openNextFile(); f; f = dir.openNextFile())
        if ((uint32_t)String(f.name()).toInt() < month - 100) old.push_back(f.path());  // same month last year is kept
      dir.close();
      for (auto& p : old) LittleFS.remove(p);
      File f = LittleFS.open(path, "w");
      int16_t empty[SLOTS];
      memset(empty, 0xFF, sizeof empty);
      for (int d = 0; d < 31; d++) f.write((uint8_t*)empty, sizeof empty);
    }
    File f = LittleFS.open(path, "r+");
    if (f && f.seek((dayDate[i] % 100 - 1) * sizeof level[i]) &&
        f.write((uint8_t*)level[i], sizeof level[i]) == sizeof level[i]) {
      dirty[i] = false;
      n++;
    }  // otherwise retry next time
  }
  if (n) Serial.printf("History saved: %d day(s)\n", n);
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
           lastMa, lastMm, WiFi.RSSI(), apMode ? (";  ACCESS POINT MODE: " + apReason).c_str() : "", errorText());
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
  fsOk = LittleFS.begin(true);  // true: format on first use
  if (fsOk) {
    LittleFS.mkdir("/h");
    Serial.printf("LittleFS: %u of %u KB used\n", (unsigned)(LittleFS.usedBytes() / 1024), (unsigned)(LittleFS.totalBytes() / 1024));
  } else Serial.println("LittleFS not available - history in RAM only (today/yesterday)");

  Wire.begin(PIN_SDA, PIN_SCL);
  adsOk = ads.begin(0x48, &Wire);   // ADDR to GND
  if (adsOk) ads.setGain(GAIN_TWO); // +/-2.048 V, fits 0.3-1.5 V
  else Serial.println("ADS1115 not found - check wiring!");

  wifiStart();
  configTzTime(TZ_INFO, NTP_SERVER);
  MDNS.begin(HOSTNAME);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/history", HTTP_GET, handleHistory);
  server.on("/config", HTTP_GET, handleConfig);
  server.on("/config", HTTP_POST, handleSave);
  server.begin();
  telnet.begin();

  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.setPassword(webPass.c_str());
  ArduinoOTA.onStart([] {  // ESP reboots after the update -> save history first
    if (fsOk && ArduinoOTA.getCommand() == U_FLASH) saveHistory();
  });
  ArduinoOTA.begin();

  mqtt.setServer(mqttHost.c_str(), mqttPort);  // keeps only the pointer, mqttHost must stay global
  digitalWrite(LED_BUILTIN, apMode);  // LED stays on in AP mode
}

void loop() {
  ArduinoOTA.handle();
  server.handleClient();
  mqtt.loop();
  if (telnet.hasClient()) telnetClient = telnet.accept();  // new client replaces the old one
  // AP mode because the router was down at boot? Retry after 10 min.
  if (apMode && ssid.length() && millis() > 600000) restart();

  static unsigned long tMqtt = 0;  // MQTT: publishes the latest value, independent of the measuring interval
  if (!apMode && lastMm >= 0 && mqttHost.length() && millis() - tMqtt >= mqttSec * 1000UL) {
    tMqtt = millis();
    if (!mqtt.connected()) mqtt.connect(HOSTNAME, mqttUser.c_str(), mqttPass.c_str());
    mqtt.publish(mqttTopic.c_str(), String(lastMm).c_str());
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
  if (fsOk && (long)(millis() - tSave) >= 0) {
    saveHistory();
    tSave = millis() + 4 * 3600000UL;
  }
  report(t);
  Serial.print(buf);
  if (telnetClient.connected()) telnetClient.print(buf);
  digitalWrite(LED_BUILTIN, apMode);
}
