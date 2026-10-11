#include "web.h"

#include <Arduino.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>

#include "config.h"
#include "leds.h"
#include "net.h"
#include "udp_rx.h"

static ESP8266WebServer g_server(80);
static ESP8266HTTPUpdateServer g_updater;

static const char kIndexHtml[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>AmbiLight</title>
<style>
  :root { color-scheme: dark; }
  body { margin:0; padding:24px; background:#0a0b0d; color:#e8eaed;
         font:14px/1.5 ui-monospace,Consolas,monospace; }
  h1 { font-size:16px; letter-spacing:.14em; margin:0 0 4px; }
  h1 span { color:#4b5563; }
  h2 { font-size:12px; text-transform:uppercase; letter-spacing:.1em;
       color:#8b93a1; margin:24px 0 8px; font-weight:600; }
  .card { border:1px solid #20242c; border-radius:8px; padding:16px;
          margin-bottom:16px; background:#101216; }
  table { border-collapse:collapse; width:100%; }
  td { padding:3px 0; }
  td:first-child { color:#8b93a1; width:40%; }
  label { display:block; color:#8b93a1; font-size:12px; margin-bottom:4px; }
  input[type=number] { width:100%; box-sizing:border-box; padding:8px;
      background:#0a0b0d; border:1px solid #2a2f38; border-radius:6px;
      color:#e8eaed; font:inherit; }
  input[type=range] { width:100%; }
  button { padding:9px 16px; border-radius:6px; border:1px solid #2a2f38;
      background:#1a1e25; color:#e8eaed; font:inherit; cursor:pointer; }
  button:hover { background:#232830; }
  button.primary { background:#2563eb; border-color:#2563eb; }
  .row { display:flex; gap:8px; align-items:center; }
  .ok { color:#34d399; } .warn { color:#fbbf24; } .err { color:#f87171; }
  #msg { margin-top:10px; font-size:12px; min-height:16px; }
</style></head><body>

<h1>AMBIENT<span>·</span>LIGHT</h1>
<div style="color:#8b93a1;font-size:12px" id="fwline">board</div>

<h2>Status</h2>
<div class="card"><table id="status"><tr><td>loading…</td><td></td></tr></table></div>

<h2>Strip</h2>
<div class="card">
  <label>LED count</label>
  <input id="leds" type="number" min="1" max="512" value="144">
  <label style="margin-top:12px">Brightness <span id="bval">255</span></label>
  <input id="bright" type="range" min="0" max="255" value="255">
  <div class="row" style="margin-top:14px">
    <button class="primary" onclick="save()">Save</button>
    <button onclick="clearStrip()">Clear strip</button>
  </div>
  <div id="msg"></div>
</div>

<h2>Firmware</h2>
<div class="card">
  <form method="POST" action="/update" enctype="multipart/form-data">
    <label>Upload .bin (OTA)</label>
    <input type="file" name="firmware" accept=".bin" style="margin-bottom:12px">
    <button class="primary" type="submit">Update firmware</button>
  </form>
  <div style="color:#8b93a1;font-size:12px;margin-top:8px">
    Build with <code>pio run -e esp8266-d1-mini</code>, then upload
    <code>.pio/build/esp8266-d1-mini/firmware.bin</code>.
  </div>
</div>

<h2>Wi-Fi</h2>
<div class="card">
  <div style="color:#8b93a1;font-size:12px;margin-bottom:10px">
    Forgetting the network reboots the board into its setup access point.
  </div>
  <button onclick="forget()">Forget Wi-Fi &amp; reboot</button>
</div>

<script>
const $ = id => document.getElementById(id);

async function refresh() {
  try {
    const s = await (await fetch('/api/status')).json();
    $('status').innerHTML = `
      <tr><td>Network</td><td class="${s.connected?'ok':'err'}">${s.connected?'connected':'offline'}</td></tr>
      <tr><td>IP</td><td>${s.ip}</td></tr>
      <tr><td>Hostname</td><td>${s.host}.local</td></tr>
      <tr><td>Signal</td><td>${s.rssi} dBm</td></tr>
      <tr><td>Firmware</td><td>${s.fw}</td></tr>
      <tr><td>Configured LEDs</td><td>${s.leds} / ${s.maxLeds}</td></tr>
      <tr><td>Frames received</td><td>${s.frames}</td></tr>
      <tr><td>Dropped</td><td>${s.dropped}</td></tr>
      <tr><td>Bad packets</td><td>${s.bad}</td></tr>
      <tr><td>Output rate</td><td>${s.fps} fps</td></tr>
      <tr><td>Uptime</td><td>${Math.floor(s.uptime/1000)} s</td></tr>`;
    $('fwline').textContent = s.host + '.local · UDP ' + s.port;
    if (document.activeElement !== $('leds')) $('leds').value = s.leds;
    if (document.activeElement !== $('bright')) {
      $('bright').value = s.brightness; $('bval').textContent = s.brightness;
    }
  } catch (e) { /* board busy; next tick will retry */ }
}

$('bright').addEventListener('input', () => $('bval').textContent = $('bright').value);

async function save() {
  const body = new URLSearchParams({ leds: $('leds').value, brightness: $('bright').value });
  const r = await fetch('/api/config', { method:'POST', body });
  $('msg').textContent = r.ok ? 'Saved.' : 'Save failed.';
  $('msg').className = r.ok ? 'ok' : 'err';
  refresh();
}

async function clearStrip() {
  await fetch('/api/clear', { method:'POST' });
  $('msg').textContent = 'Strip cleared.';
  $('msg').className = 'ok';
}

async function forget() {
  if (!confirm('Forget Wi-Fi and reboot into setup mode?')) return;
  await fetch('/api/forget', { method:'POST' });
  document.body.innerHTML = '<p style="padding:24px">Rebooting into setup mode… ' +
    'Connect to the <b>AmbiLight-Setup</b> network.</p>';
}

refresh();
setInterval(refresh, 2000);
</script>
</body></html>)HTML";

static void handleRoot() {
    g_server.send_P(200, "text/html", kIndexHtml);
}

static void handleStatus() {
    char status[256];
    netWriteStatusJson(status, sizeof(status));

    // Strip the trailing brace so the board-only fields can be appended
    // without parsing the JSON we just built.
    String body = status;
    if (body.endsWith("}")) body.remove(body.length() - 1);

    char extra[256];
    snprintf(extra, sizeof(extra),
             ",\"connected\":%s,\"port\":%u,\"brightness\":%u,"
             "\"frames\":%lu,\"dropped\":%lu,\"bad\":%lu,\"fps\":%u,\"uptime\":%lu}",
             netConnected() ? "true" : "false",
             (unsigned)netUdpPort(),
             (unsigned)configGet().brightness,
             (unsigned long)udpRxFrames(),
             (unsigned long)udpRxDropped(),
             (unsigned long)udpRxBadPackets(),
             (unsigned)ledsFps(),
             (unsigned long)millis());

    g_server.send(200, "application/json", body + extra);
}

static void handleConfig() {
    if (g_server.hasArg("leds")) {
        configSetLeds((uint16_t)g_server.arg("leds").toInt());
    }
    if (g_server.hasArg("brightness")) {
        const long b = g_server.arg("brightness").toInt();
        configSetBrightness((uint8_t)(b < 0 ? 0 : (b > 255 ? 255 : b)));
    }
    configSave();
    ledsApply();
    g_server.send(200, "application/json", "{\"ok\":true}");
}

static void handleClear() {
    ledsClear();
    g_server.send(200, "application/json", "{\"ok\":true}");
}

static void handleForget() {
    g_server.send(200, "application/json", "{\"ok\":true}");
    delay(200);

    // Wipe the SDK's stored credentials and reboot; WiFiManager will then find
    // no network and bring up the setup portal.
    WiFi.disconnect(true);
    ESP.restart();
}

void webBegin() {
    g_server.on("/", HTTP_GET, handleRoot);
    g_server.on("/api/status", HTTP_GET, handleStatus);
    g_server.on("/api/config", HTTP_POST, handleConfig);
    g_server.on("/api/clear", HTTP_POST, handleClear);
    g_server.on("/api/forget", HTTP_POST, handleForget);

    // OTA shares the same server on /update.
    g_updater.setup(&g_server, "/update", "admin", "ambilight");

    g_server.begin();
    Serial.println(F("[web] http server on :80"));
}

void webLoop() { g_server.handleClient(); }
