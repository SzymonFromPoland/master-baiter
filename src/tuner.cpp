#include "tuner.h"
#include <WiFi.h>
#include <ESPAsyncWebServer.h>

static AsyncWebServer *server = nullptr;
static TuningParam *_params;
static int _numParams;
static VL53L1X_Result_t *_results;
static int _numSensors;
static Preferences *_prefs;

static const char HTML_HEAD[] PROGMEM = R"HTML(<!DOCTYPE html><html><head>
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=0">
<style>
  body { font-family: sans-serif; padding: 20px; max-width: 500px; margin: auto; background: #f4f4f9; color: #333; -webkit-user-select: none; user-select: none; }
  .card { background: white; padding: 15px; border-radius: 8px; box-shadow: 0 2px 5px rgba(0,0,0,0.1); margin-bottom: 20px; }
  .param-group { margin-bottom: 12px; padding: 10px; border-radius: 4px; border: 1px solid #eee; background: #fff; }
  .read-only { background: #fdfdfd; border-left: 4px solid #007bff; }
  label { display: flex; justify-content: space-between; font-weight: bold; font-size: 14px; margin-bottom: 8px; }
  .btn { background: #007bff; color: white; border: none; border-radius: 6px; padding: 12px; min-width: 60px; font-size: 20px; cursor: pointer; touch-action: manipulation; transition: 0.1s; }
  .btn:active { background: #0056b3; transform: scale(0.95); }
  .btn-action { width: 100%; background: #28a745; font-size: 16px; font-weight: bold; text-transform: uppercase; }
  .btn-action:active { background: #1e7e34; }
  .control-row { display: flex; align-items: center; justify-content: space-between; gap: 10px; }
  .val-display { font-family: monospace; font-size: 20px; color: #007bff; font-weight: bold; min-width: 80px; text-align: center; }
  input[type=range] { width: 100%; margin: 10px 0; }
  .switch { position: relative; display: inline-block; width: 50px; height: 26px; }
  .switch input { opacity: 0; width: 0; height: 0; }
  .slider-round { position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0; background-color: #ccc; transition: .4s; border-radius: 34px; }
  .slider-round:before { position: absolute; content: ""; height: 18px; width: 18px; left: 4px; bottom: 4px; background-color: white; transition: .4s; border-radius: 50%; }
  input:checked + .slider-round { background-color: #2196F3; }
  input:checked + .slider-round:before { transform: translateX(24px); }
  table { width: 100%; border-collapse: collapse; font-size: 11px; margin-top: 10px; }
  th, td { border: 1px solid #ddd; padding: 4px; text-align: center; }
</style></head><body>
<h2>Baiter Tuner</h2>
<div id="controls" class="card"></div>
<div class="card">
  <table><thead><tr><th>ID</th><th>Dist</th><th>Stat</th></tr></thead><tbody id="sensorBody"></tbody></table>
</div>

<script>
let holdTimer, repeatInterval, isInteracting = false, lockTimeout, configData = [];

function sendVal(id, val) { fetch(`/set?id=${id}&val=${val}`); }

function changeValue(id, step) {
  let el = document.getElementById(id+'Val');
  let cfg = configData.find(p => p.id === id);
  let newVal = Math.min(cfg.max, Math.max(cfg.min, parseFloat(el.innerText) + step));
  el.innerText = newVal.toFixed(2);
  sendVal(id, newVal.toFixed(2));
}

function startHold(e, id, dir) {
  if (e.cancelable) e.preventDefault();
  isInteracting = true; clearTimeout(lockTimeout);
  let cfg = configData.find(p => p.id === id);
  let step = cfg.step * dir;
  changeValue(id, step);
  holdTimer = setTimeout(() => { repeatInterval = setInterval(() => changeValue(id, step), 80); }, 400);
}

function stopHold() {
  clearTimeout(holdTimer); clearInterval(repeatInterval);
  lockTimeout = setTimeout(() => { isInteracting = false; }, 500);
}

fetch('/config').then(r=>r.json()).then(data=>{
  configData = data;
  let html = '';
  data.forEach(p => {
    html += `<div class="param-group ${p.type==3?'read-only':''}">`;
    if(p.type != 4) {
      html += `<label><span>${p.label}</span>${p.type!=1 ? `<span id="${p.id}Val">${p.val.toFixed(2)}</span>` : ''}</label>`;
    }
    if(p.type == 0) {
      html += `<input type="range" min="${p.min}" max="${p.max}" step="${p.step}" value="${p.val}" oninput="document.getElementById('${p.id}Val').innerText=this.value; sendVal('${p.id}',this.value)">`;
    } else if(p.type == 1) {
      html += `<div class="control-row">
        <button class="btn" onmousedown="startHold(event,'${p.id}',-1)" ontouchstart="startHold(event,'${p.id}',-1)" onmouseup="stopHold()" ontouchend="stopHold()">&larr;</button>
        <span id="${p.id}Val" class="val-display">${p.val.toFixed(2)}</span>
        <button class="btn" onmousedown="startHold(event,'${p.id}',1)" ontouchstart="startHold(event,'${p.id}',1)" onmouseup="stopHold()" ontouchend="stopHold()">&rarr;</button>
      </div>`;
    } else if(p.type == 2) {
      html += `<label class="switch"><input type="checkbox" ${p.val>0.5?'checked':''} onchange="sendVal('${p.id}', this.checked?1:0)"><span class="slider-round"></span></label>`;
    } else if(p.type == 4) {
      html += `<button class="btn btn-action" onclick="sendVal('${p.id}', ${p.min})">${p.label}</button>`;
    }
    html += `</div>`;
  });
  document.getElementById('controls').innerHTML = html;
});

setInterval(() => {
  if (isInteracting) return;
  fetch('/data').then(r=>r.json()).then(d => {
    for (const [id, val] of Object.entries(d.v)) {
      let el = document.getElementById(id+'Val');
      if(el) el.innerText = val.toFixed(2);
    }
    let sH = '';
    d.s.forEach((s, i) => {
      sH += `<tr><td>${i}</td><td>${s.d}</td><td style="color:${s.st==0?'green':'red'}">${s.st}</td></tr>`;
    });
    document.getElementById('sensorBody').innerHTML = sH;
  });
}, 250);
</script></body></html>)HTML";

static void tunerTask(void *param)
{
  WiFi.softAP("Baiter", "123456789");

  server = new AsyncWebServer(80);

  server->on("/", HTTP_GET, [](AsyncWebServerRequest *req)
             { req->send(200, "text/html", HTML_HEAD); });

  server->on("/config", HTTP_GET, [](AsyncWebServerRequest *req)
            {
    AsyncResponseStream *response = req->beginResponseStream("application/json");
    response->print("[");
    for(int i=0; i<_numParams; i++) {
      response->printf("{\"label\":\"%s\",\"id\":\"%s\",\"val\":%.2f,\"min\":%.1f,\"max\":%.1f,\"step\":%.2f,\"type\":%d}%s",
        _params[i].label, _params[i].id, *_params[i].value,
        _params[i].min, _params[i].max, _params[i].step,
        (int)_params[i].type, (i==_numParams-1?"":","));
    }
    response->print("]");
    req->send(response); });

  server->on("/data", HTTP_GET, [](AsyncWebServerRequest *req)
            {
    AsyncResponseStream *response = req->beginResponseStream("application/json");
    response->print("{\"v\":{");
    for(int i=0; i<_numParams; i++) {
      response->printf("\"%s\":%.2f%s", _params[i].id, *_params[i].value, (i==_numParams-1?"":","));
    }
    response->print("},\"s\":[");
    for(int i=0; i<_numSensors; i++) {
      response->printf("{\"d\":%d,\"st\":%d}%s",
        _results[i].Distance,
        _results[i].Status,
        (i==_numSensors-1?"":","));
    }
    response->print("]}");
    req->send(response); });

  server->on("/set", HTTP_GET, [](AsyncWebServerRequest *req)
            {
    if (req->hasParam("id") && req->hasParam("val")) {
      String id = req->getParam("id")->value();
      float val = req->getParam("val")->value().toFloat();
      for(int i=0; i<_numParams; i++) {
        if(id == _params[i].id && _params[i].type != TYPE_READONLY) {
          *_params[i].value = val;
          if (_prefs) {
            _prefs->begin("robot", false);
            _prefs->putFloat(_params[i].id, val);
            _prefs->end();
          }
          break;
        }
      }
    }
    req->send(200, "text/plain", "ok"); });

  server->begin();
  vTaskDelete(NULL);
}

void startTuner(TuningParam *params, int numParams, VL53L1X_Result_t *results, int numSensors, Preferences *prefs)
{
  _params = params;
  _numParams = numParams;
  _results = results;
  _numSensors = numSensors;
  _prefs = prefs;

  xTaskCreatePinnedToCore(tunerTask, "tuner", 16384, NULL, 1, NULL, 0);
}