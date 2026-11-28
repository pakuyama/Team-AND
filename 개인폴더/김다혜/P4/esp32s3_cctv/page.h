// AiHand CCTV 웹 화면 (http://<IP>/)  -  김다혜 P4
#pragma once
#include <pgmspace.h>

const char PAGE_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="ko"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>AiHand CCTV</title>
<style>
:root{--bg:#0f1115;--card:#1a1d24;--line:#2a2f3a;--txt:#e6e8ec;--sub:#9aa3b2;--ok:#3ecf8e;--bad:#ff5d5d;--acc:#4da3ff}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--txt);font:14px/1.5 system-ui,sans-serif}
header{display:flex;align-items:center;gap:10px;padding:10px 14px;border-bottom:1px solid var(--line)}
header h1{font-size:16px;margin:0}#clock{margin-left:auto;font-variant-numeric:tabular-nums;color:var(--sub)}
main{display:grid;grid-template-columns:minmax(0,1fr) 320px;gap:14px;padding:14px;max-width:1300px;margin:auto}
@media(max-width:860px){main{grid-template-columns:1fr}}
.card{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:12px}
.card h2{font-size:14px;margin:0 0 8px}
#view{position:relative;background:#000;border-radius:10px;overflow:hidden;aspect-ratio:4/3}
#view img{width:100%;height:100%;object-fit:contain;display:block}
.ov{position:absolute;padding:3px 8px;border-radius:6px;background:rgba(0,0,0,.55);font-size:13px}
#ovTime{left:8px;top:8px;font-variant-numeric:tabular-nums}
#ovRec{right:8px;top:8px;color:var(--bad);display:none}
#ovRec::before{content:"";display:inline-block;width:9px;height:9px;border-radius:50%;background:var(--bad);margin-right:6px;animation:b 1s infinite}
@keyframes b{50%{opacity:.2}}
#ovMotion{left:8px;bottom:8px}#ovMotion.on{background:rgba(255,93,93,.8)}
#ovMsg{inset:0;display:none;align-items:center;justify-content:center;text-align:center;background:rgba(0,0,0,.7);border-radius:0}
.row{display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin-top:10px}
button,select,input[type=text],input[type=password]{background:#232834;color:var(--txt);border:1px solid var(--line);border-radius:8px;padding:7px 10px;font:inherit}
button{cursor:pointer}button:hover{border-color:var(--acc)}button.danger{color:var(--bad)}
label{display:flex;align-items:center;gap:8px;margin:6px 0}label input[type=text],label input[type=password]{flex:1;min-width:0}label span{min-width:92px;color:var(--sub)}
input[type=range]{flex:1}
.stat{display:grid;grid-template-columns:auto 1fr;gap:2px 12px;color:var(--sub)}.stat b{color:var(--txt);font-weight:500}
.bar{height:6px;background:#2a2f3a;border-radius:3px;overflow:hidden}.bar i{display:block;height:100%;background:var(--acc)}
#events{display:grid;grid-template-columns:repeat(auto-fill,minmax(130px,1fr));gap:8px}
.ev{background:#111;border:1px solid var(--line);border-radius:8px;overflow:hidden;cursor:pointer}
.ev img{width:100%;aspect-ratio:4/3;object-fit:cover;display:block;background:#222}
.ev div{padding:4px 6px;font-size:12px;color:var(--sub)}
.hint{color:var(--sub);font-size:12px}
#modal{position:fixed;inset:0;background:rgba(0,0,0,.85);display:none;flex-direction:column;align-items:center;justify-content:center;padding:14px;z-index:9}
#modal img{max-width:100%;max-height:72vh;border-radius:8px}
#apBanner{display:none;background:#3a2f12;color:#ffd77a;padding:8px 14px}
</style></head><body>
<div id="apBanner">설정 모드예요. 아래 <b>와이파이 설정</b>에서 공유기를 저장하면 공유기에 연결돼요. (이 모드에서도 CCTV는 그대로 동작)</div>
<header><h1>📹 AiHand CCTV</h1><span id="conn" class="hint"></span><span id="clock"></span></header>
<main>
<section>
  <div id="view">
    <img id="live" alt="">
    <div class="ov" id="ovTime"></div>
    <div class="ov" id="ovRec">REC</div>
    <div class="ov" id="ovMotion">움직임 없음</div>
    <div class="ov" id="ovMsg"></div>
  </div>
  <div class="row">
    <a id="snap" href="/capture?dl=1"><button>📸 스냅샷 내려받기</button></a>
    <button id="full">⛶ 전체화면</button>
    <button id="pause">⏸ 영상 멈춤</button>
    <span class="hint" id="streamUrl"></span>
  </div>
  <div class="card" style="margin-top:14px">
    <h2>녹화된 사건 <span class="hint" id="evInfo"></span></h2>
    <div id="events"></div>
  </div>
</section>
<aside>
  <div class="card">
    <h2>상태</h2>
    <div class="stat">
      <span>와이파이</span><b id="sWifi">-</b>
      <span>영상</span><b id="sFps">-</b>
      <span>보는 사람</span><b id="sView">-</b>
      <span>움직임</span><b id="sMotion">-</b>
      <span>microSD</span><b id="sSd">-</b>
    </div>
    <div class="bar" style="margin-top:6px"><i id="sdBar" style="width:0"></i></div>
  </div>
  <div class="card" style="margin-top:14px">
    <h2>카메라</h2>
    <label><span>해상도</span><select id="size">
      <option value="5">320×240 (빠름)</option><option value="8">640×480</option>
      <option value="9">800×600</option><option value="11">1280×720</option><option value="12">1280×1024</option>
    </select></label>
    <label><span>화질</span><input type="range" id="quality" min="10" max="40"><b id="qualityV"></b></label>
    <label><span>상하 반전</span><input type="checkbox" id="vflip"></label>
    <label><span>좌우 반전</span><input type="checkbox" id="hmirror"></label>
  </div>
  <div class="card" style="margin-top:14px">
    <h2>움직임 감지 · 녹화</h2>
    <label><span>움직임 감지</span><input type="checkbox" id="motion"></label>
    <label><span>자동 녹화</span><input type="checkbox" id="record"></label>
    <label><span>민감도</span><input type="range" id="sens" min="5" max="80"><b id="sensV"></b></label>
    <label><span>감지 면적</span><input type="range" id="area" min="1" max="30"><b id="areaV"></b></label>
    <p class="hint">민감도 숫자가 작을수록 작은 밝기 변화도 잡아요. 감지 면적은 화면의 몇 %가 바뀌어야 움직임으로 볼지예요. 지금 바뀐 비율: <b id="lvl">0</b>%</p>
  </div>
  <div class="card" style="margin-top:14px">
    <h2>와이파이 설정</h2>
    <form id="wifi">
      <label><span>이름</span><input type="text" name="ssid" required maxlength="32"></label>
      <label><span>비밀번호</span><input type="password" name="pass" maxlength="64"></label>
      <div class="row"><button>저장하고 다시 시작</button></div>
      <p class="hint">2.4GHz 와이파이만 돼요. 연결에 실패하면 설정 모드(AiHand-CCTV-xxxx)로 다시 켜져요.</p>
    </form>
  </div>
</aside>
</main>
<div id="modal">
  <div class="hint" id="mTitle" style="margin-bottom:8px"></div>
  <img id="mImg" alt="">
  <input type="range" id="mSeek" min="0" value="0" style="width:min(640px,100%);margin-top:10px">
  <div class="row">
    <button id="mPrev">◀</button><button id="mPlay">▶ 재생</button><button id="mNext">▶</button>
    <a id="mDl"><button>⬇ 이 사진</button></a>
    <button class="danger" id="mDel">🗑 사건 지우기</button>
    <button id="mClose">닫기</button>
  </div>
</div>
<script>
const $ = id => document.getElementById(id);
const host = location.hostname;
const streamSrc = "http://" + host + ":81/stream";
$("streamUrl").textContent = "영상 주소: " + streamSrc;

// ---------- 실시간 영상 ----------
let paused = false, retry = null;
function showMsg(t) { $("ovMsg").style.display = t ? "flex" : "none"; $("ovMsg").innerHTML = t || ""; }
function startLive() {
  clearTimeout(retry);
  $("live").src = streamSrc + "?t=" + Date.now();
}
$("live").onload = () => showMsg("");
$("live").onerror = () => {
  if (paused) return;
  showMsg("영상을 받을 수 없어요.<br><span class='hint'>보는 사람이 너무 많거나(최대 3명) 연결이 끊겼어요. 3초 뒤 다시 시도해요.</span>");
  retry = setTimeout(startLive, 3000);
};
$("pause").onclick = () => {
  paused = !paused;
  $("pause").textContent = paused ? "▶ 영상 다시" : "⏸ 영상 멈춤";
  if (paused) { $("live").src = ""; showMsg("영상 멈춤 (다른 사람이 볼 수 있게 자리를 비워 둬요)"); }
  else startLive();
};
$("full").onclick = () => { const v = $("view"); (v.requestFullscreen || v.webkitRequestFullscreen).call(v); };
startLive();

// ---------- 상태 ----------
let cfgLoaded = false, timeSent = false, lastEvRec = null;
function setText(id, t) { $(id).textContent = t; }
async function poll() {
  try {
    const s = await (await fetch("/api/status", { cache: "no-store" })).json();
    setText("conn", "");
    if (!s.time && !timeSent) {           // 시계가 안 맞으면 이 기기 시간으로 맞춤
      timeSent = true;
      fetch("/api/time?t=" + Math.floor(Date.now() / 1000)).catch(() => {});
    }
    const t = s.time || "시간 맞추는 중...";
    setText("clock", t); setText("ovTime", t);
    $("apBanner").style.display = s.ap ? "block" : "none";
    setText("sWifi", s.ap ? "설정 모드 · " + s.ssid : s.ssid + " (" + s.rssi + " dBm) · " + s.ip);
    setText("sFps", s.fps + " fps");
    setText("sView", s.viewers + " / " + s.maxViewers + "명");
    setText("sMotion", !s.cfg.motion ? "끔" : s.motion ? "감지됨 (" + s.level + "%)" : "없음 (" + s.level + "%)");
    setText("lvl", s.level);
    $("ovMotion").className = "ov" + (s.motion ? " on" : "");
    setText("ovMotion", !s.cfg.motion ? "움직임 감지 꺼짐" : s.motion ? "⚠ 움직임 감지" : "움직임 없음");
    $("ovRec").style.display = s.recording ? "block" : "none";
    if (s.sd) {
      setText("sSd", s.sdUsedMB + " / " + s.sdTotalMB + " MB" + (s.recording ? " · 녹화 중 " + s.recCount + "장" : ""));
      $("sdBar").style.width = (100 * s.sdUsedMB / Math.max(1, s.sdTotalMB)) + "%";
    } else setText("sSd", "없음 (녹화 안 됨)");
    if (!cfgLoaded) { fillCfg(s.cfg); cfgLoaded = true; }
    const rec = s.recording ? s.recDir : "";
    if (rec !== lastEvRec) { lastEvRec = rec; loadEvents(); }   // 녹화 시작/끝나면 목록 새로고침
  } catch (e) {
    setText("conn", "⚠ CCTV에 연결 안 됨");
  }
}
setInterval(poll, 1000); poll();

// ---------- 설정 ----------
const keys = ["size", "quality", "vflip", "hmirror", "motion", "record", "sens", "area"];
function fillCfg(c) {
  for (const k of keys) {
    const el = $(k);
    if (el.type === "checkbox") el.checked = !!c[k]; else el.value = c[k];
  }
  showVals();
}
function showVals() {
  setText("qualityV", $("quality").value);
  setText("sensV", $("sens").value);
  setText("areaV", $("area").value + "%");
}
for (const k of keys) {
  const el = $(k);
  el.oninput = showVals;
  el.onchange = async () => {
    const v = el.type === "checkbox" ? (el.checked ? 1 : 0) : el.value;
    try { const s = await (await fetch("/api/set?" + k + "=" + encodeURIComponent(v))).json(); fillCfg(s.cfg); } catch (e) {}
  };
}

// ---------- 와이파이 ----------
$("wifi").onsubmit = async e => {
  e.preventDefault();
  if (!confirm("저장하고 다시 시작할까요? 잠깐 영상이 끊겨요.")) return;
  const body = new URLSearchParams(new FormData($("wifi")));
  const r = await fetch("/api/wifi", { method: "POST", body });
  document.body.innerHTML = await r.text();
};

// ---------- 녹화된 사건 ----------
function evLabel(id) {
  const m = id.match(/^(\d{4})(\d{2})(\d{2})_(\d{2})(\d{2})(\d{2})$/);
  return m ? `${m[2]}/${m[3]} ${m[4]}:${m[5]}:${m[6]}` : id;
}
async function loadEvents() {
  try {
    const d = await (await fetch("/api/events", { cache: "no-store" })).json();
    const box = $("events");
    box.innerHTML = "";
    if (!d.sd) { setText("evInfo", "· microSD가 없어서 녹화하지 않아요"); return; }
    setText("evInfo", "· " + d.events.length + "개 (최근 50개까지)");
    if (!d.events.length) box.innerHTML = "<span class='hint'>아직 없어요. 카메라 앞에서 움직이면 녹화돼요.</span>";
    for (const id of d.events) {
      const el = document.createElement("div");
      el.className = "ev";
      const rec = id === d.recording;
      el.innerHTML = `<img loading="lazy" src="/sd?id=${id}&f=0000.jpg"><div>${evLabel(id)}${rec ? " · 🔴 녹화 중" : ""}</div>`;
      el.onclick = () => openEvent(id);
      box.appendChild(el);
    }
  } catch (e) {}
}

let cur = null, files = [], idx = 0, timer = null;
function show(i) {
  if (!files.length) return;
  idx = (i + files.length) % files.length;
  const src = `/sd?id=${cur}&f=${files[idx]}`;
  $("mImg").src = src;
  $("mDl").href = src + "&dl=1";
  $("mSeek").value = idx;
  setText("mTitle", `${evLabel(cur)} · ${idx + 1} / ${files.length}장 (0.5초 간격)`);
}
function stopPlay() { clearInterval(timer); timer = null; setText("mPlay", "▶ 재생"); }
async function openEvent(id) {
  cur = id; idx = 0; stopPlay();
  const d = await (await fetch("/api/event?id=" + id, { cache: "no-store" })).json();
  files = d.files;
  $("mSeek").max = Math.max(0, files.length - 1);
  $("modal").style.display = "flex";
  show(0);
}
$("mPrev").onclick = () => { stopPlay(); show(idx - 1); };
$("mNext").onclick = () => { stopPlay(); show(idx + 1); };
$("mSeek").oninput = () => { stopPlay(); show(+$("mSeek").value); };
$("mPlay").onclick = () => {
  if (timer) return stopPlay();
  setText("mPlay", "⏸ 멈춤");
  timer = setInterval(() => { if (idx >= files.length - 1) stopPlay(); else show(idx + 1); }, 500);
};
$("mClose").onclick = () => { stopPlay(); $("modal").style.display = "none"; };
$("mDel").onclick = async () => {
  if (!confirm(evLabel(cur) + " 사건의 사진을 모두 지울까요? 되돌릴 수 없어요.")) return;
  const r = await fetch("/api/event?delete=1&id=" + cur);
  if (!r.ok) { alert(r.status === 409 ? "지금 녹화 중이라 못 지워요" : "지우기 실패"); return; }
  $("mClose").onclick();
  loadEvents();
};
document.addEventListener("keydown", e => {
  if ($("modal").style.display !== "flex") return;
  if (e.key === "ArrowLeft") $("mPrev").onclick();
  if (e.key === "ArrowRight") $("mNext").onclick();
  if (e.key === "Escape") $("mClose").onclick();
});
</script></body></html>)HTML";
