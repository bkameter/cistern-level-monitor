// SPDX-License-Identifier: GPL-3.0-or-later
// Web basics and the public level page /

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
  ".nv button,.scan button{background:var(--bg);color:var(--fg);border:1px solid var(--line);border-radius:8px;padding:8px 14px;font:inherit;cursor:pointer}" \
  "button:disabled{opacity:.4;cursor:default}" \
  ".cw{position:relative}svg{width:100%;height:auto;overflow:visible;touch-action:pan-y}svg text{fill:var(--mut);font-size:11px}" \
  ".g{stroke:var(--line)}.l{fill:none;stroke:var(--acc);stroke-width:2;stroke-linejoin:round}circle{fill:var(--acc)}" \
  ".hl{stroke:var(--mut);stroke-dasharray:3 3}#hc{fill:var(--card);stroke:var(--acc);stroke-width:2.5}" \
  ".tip{position:absolute;pointer-events:none;background:var(--fg);color:var(--bg);padding:6px 9px;border-radius:6px;font-size:13px;white-space:nowrap}" \
  /* form */ \
  "label{display:block;margin-top:12px;font-size:13px;color:var(--mut)}" \
  "input,select,textarea{display:block;width:100%;box-sizing:border-box;margin-top:4px;padding:9px 10px;font:inherit;color:var(--fg);" \
  "background:var(--bg);border:1px solid var(--line);border-radius:8px}" \
  "textarea{font:12px monospace}input:focus,select:focus,textarea:focus{outline:2px solid var(--acc);outline-offset:-1px}" \
  ".scan{display:flex;gap:8px;margin-top:4px}.scan input{margin:0}" \
  ".spin::before{content:'';display:inline-block;width:.8em;height:.8em;margin-right:6px;vertical-align:-1px;" \
  "border:2px solid;border-right-color:transparent;border-radius:50%;animation:sp 1s linear infinite}@keyframes sp{to{transform:rotate(1turn)}}" \
  ".row{display:grid;grid-template-columns:repeat(auto-fit,minmax(140px,1fr));gap:0 12px}" \
  ".live{font-size:22px;font-weight:600;margin-top:4px}.hint{margin:6px 0 0;font-size:12px;color:var(--mut)}" \
  ".msg{padding:10px 14px;border-radius:8px;margin-bottom:16px}.ok{background:#2e9d4f22;border:1px solid #2e9d4f66}" \
  ".msg.err{color:inherit;background:#d3333322;border:1px solid #d3333366}" \
  ".bar{position:sticky;bottom:0;padding:12px 0;background:var(--bg)}" \
  ".tabs{margin-bottom:16px}.tabs button{flex:1;padding:8px}" \
  ".btn{background:var(--acc);color:var(--card);border:0;border-radius:8px;padding:10px 24px;font:inherit;font-weight:600;cursor:pointer}"

String T(const char* de, const char* e) { return en ? e : de; }

void setLang() {  // ?lang=de|en (remembered in a cookie), else cookie, else browser language
  String l = server.arg("lang");
  if (l == "de" || l == "en") server.sendHeader("Set-Cookie", "lang=" + l + "; Path=/; Max-Age=31536000");
  else {
    String c = server.header("Cookie");
    int i = c.indexOf("lang=");
    l = i >= 0 ? c.substring(i + 5, i + 7) : server.header("Accept-Language");
  }
  en = !l.startsWith("de");
}

String head() {
  return "<!DOCTYPE html><html lang='" + T("de", "en") + "'><head><meta charset='utf-8'>"
         "<meta name='viewport' content='width=device-width,initial-scale=1'><title>Zisterne</title>"
         "<style>" CSS "</style></head><body><main>";
}

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
<div class="card"><div class="hd"><span class="mut" data-en="History (mm)">Verlauf (mm)</span>
<nav class="seg"><button data-m="d" data-en="Day">Tag</button><button data-m="w" data-en="Week">Woche</button><button data-m="m" data-en="Month">Monat</button></nav></div>
<div class="nv"><button id="prev" aria-label="zurück">‹</button><input type="date" id="date"><button id="next" aria-label="vor">›</button></div>
<div class="cw"><svg id="chart" viewBox="0 0 600 260"></svg><div id="tip" class="tip" hidden></div></div></div>
<p class="mut"><a href="/config" data-en="Settings">Einstellungen</a> · <a href="?lang=en" id="lng">English</a></p></main>
<script>/*DATA*/
const t=(d,e)=>A.en?e:d;
if(A.en){document.documentElement.lang='en';document.querySelectorAll('[data-en]').forEach(e=>e.textContent=e.dataset.en);
 prev.ariaLabel='back';next.ariaLabel='forward';lng.href='?lang=de';lng.textContent='Deutsch'}
cur.textContent=A.mm>=0?A.mm+' mm':t('Fehler','Error');
info.innerHTML=(A.ma>=0?A.ma.toFixed(2)+' mA · ':'')+A.time+(A.err?'<br><span class="err">'+A.err+'</span>':'');
// days as day numbers (UTC, since 1970); value index = day number*288 + 5-min slot
const J=new Date(),TODAY=Date.UTC(J.getFullYear(),J.getMonth(),J.getDate())/864e5,MIN=TODAY-366,
 dt=n=>new Date(n*864e5),iso=n=>dt(n).toISOString().slice(0,10),
 fmtDay=n=>dt(n).toLocaleDateString(t('de','en-GB'),{day:'2-digit',month:'2-digit',timeZone:'UTC'}),
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
 if(!P.length){chart.innerHTML='<text x="300" y="130" text-anchor="middle">'+t('Keine Daten in diesem Zeitraum','No data in this period')+'</text>';return}
 const x0=first*288,x1=(last+1)*288-1,
  mn=Math.min(...P.map(p=>p[1])),mx=Math.max(...P.map(p=>p[1])),
  S=[10,20,50,100,200,500,1000].find(s=>s*4>=mx-mn)||2000,  // grid step, y axis adapts to the visible range
  lo=Math.floor(mn/S)*S,hi=Math.max(Math.ceil(mx/S)*S,lo+S),
  x=i=>L+(W-L-R)*(i-x0)/Math.max(x1-x0,1),y=v=>T+(H-T-B)*(hi-v)/(hi-lo);
 let s='',d='';
 for(let v=lo;v<=hi;v+=S)s+=`<line class="g" x1="${L}" x2="${W-R}" y1="${y(v)}" y2="${y(v)}"/><text x="${L-6}" y="${y(v)+4}" text-anchor="end">${v}</text>`;
 const st=x1-x0<288?72:288*Math.ceil((last-first+1)/8);  // x labels: 6 h grid (day) or at most 8 days
 for(let i=x0;i<=x1;i+=st)s+=`<text x="${x(i)}" y="${H-6}" text-anchor="middle">${i%288?i%288/12+t(' Uhr',':00'):fmtDay(i/288)}</text>`;
 P.forEach((p,j)=>d+=(j&&p[0]-P[j-1][0]==1?'L':'M')+x(p[0]).toFixed(1)+','+y(p[1]).toFixed(1));  // don't bridge gaps
 s+=`<path class="l" d="${d}"/>`;
 if(P.length<60)s+=P.map(p=>`<circle cx="${x(p[0])}" cy="${y(p[1])}" r="3"/>`).join('');
 chart.innerHTML=s+`<line id="hl" class="hl" y1="${T}" y2="${H-B}" visibility="hidden"/><circle id="hc" r="5" visibility="hidden"/>`;
 chart.onpointermove=chart.onpointerdown=e=>{  // mouse: hover and click, touch: tap/swipe; tip stays visible
  const b=chart.getBoundingClientRect(),i=x0+((e.clientX-b.left)/b.width*W-L)/(W-L-R)*(x1-x0),
   p=P.reduce((a,q)=>Math.abs(q[0]-i)<Math.abs(a[0]-i)?q:a),px=x(p[0]),py=y(p[1]);
  hl.setAttribute('x1',px);hl.setAttribute('x2',px);hc.setAttribute('cx',px);hc.setAttribute('cy',py);
  hl.setAttribute('visibility','visible');hc.setAttribute('visibility','visible');
  tip.innerHTML=`<b>${p[1]} mm</b><br>${fmtDay(p[0]/288|0)} ${p[0]%288/12|0}:${String(p[0]%12*5).padStart(2,'0')}${t(' Uhr','')}`;tip.hidden=false;
  tip.style.left=px/W*b.width+'px';tip.style.top=py/H*b.height+'px';
  tip.style.transform=px>W/2?'translate(calc(-100% - 12px),-50%)':'translate(12px,-50%)'};
 chart.onpointerleave=e=>{if(e.pointerType=='mouse'){tip.hidden=true;hl.setAttribute('visibility','hidden');hc.setAttribute('visibility','hidden')}};
}
onhashchange=draw;draw();
setInterval(()=>{if(tip.hidden&&state().a==TODAY)location.reload()},60000);  // only in the current view and without an open tip
</script></body></html>)HTML";

void handleRoot() {  // no login
  setLang();
  tm t;
  char stamp[40];
  if (!getLocalTime(&t, 0) || !strftime(stamp, sizeof stamp, T("Stand %d.%m.%Y %H:%M", "As of %Y-%m-%d %H:%M").c_str(), &t))
    strcpy(stamp, T("Uhrzeit noch nicht synchronisiert", "Time not synchronized yet").c_str());
  // send in chunks, avoids a String copy of the page; the browser fetches history via /history
  const char* mark = strstr(PAGE, "/*DATA*/");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");
  server.sendContent(PAGE, mark - PAGE);
  server.sendContent("const A={mm:" + String(lastMm) + ",ma:" + String(isnan(lastMa) ? -1 : lastMa, 2) +
                     ",en:" + (en ? "1" : "0") + ",time:\"" + stamp + "\",err:\"" + errorText(en) + "\"};");
  server.sendContent(mark + 8, strlen(mark + 8));  // rest of the page after /*DATA*/
  server.sendContent("");                          // end
}
