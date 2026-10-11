const { app, BrowserWindow, ipcMain } = require('electron');
const { spawn } = require('child_process');
const path = require('path');
const readline = require('readline');
const { AmbiLight } = require('./ambilight');
const { resampleZones } = require('./resample');

let win = null;
let proc = null;
let ambi = null;

// Current strip configuration (set from the renderer).
let strip = { host: null, port: 7777, leds: 144 };

function toRenderer(channel, data) {
  if (win && !win.isDestroyed()) win.webContents.send(channel, data);
}

// Resample per-zone colours onto the configured LED count, then stream to the
// board. `zones` is the flat [r,g,b,...] array from capture.exe.
function streamToStrip(zones) {
  if (!ambi || ambi.state !== 'streaming') return;
  const target = strip.leds || zones.length / 3;
  const leds = resampleZones(zones, target);
  ambi.sendFrame(leds);
}

function startChild() {
  const exe = path.join(__dirname, 'capture.exe');
  proc = spawn(exe, [], { stdio: ['pipe', 'pipe', 'pipe'], windowsHide: true });

  readline.createInterface({ input: proc.stdout }).on('line', line => {
    let msg;
    try { msg = JSON.parse(line); } catch { return; }
    if      (msg.event === 'monitors')    toRenderer('monitors', msg.list);
    else if (msg.event === 'started')     toRenderer('status', { running: true, ...msg });
    else if (msg.event === 'stopped')     toRenderer('status', { running: false });
    else if (msg.event === 'reconnected') toRenderer('log', '[capture] reconnected');
    else if (msg.event === 'error')       toRenderer('log', '[capture] error: ' + msg.msg);
    else if (msg.leds) {
      toRenderer('frame', msg);
      streamToStrip(msg.leds);
    }
  });

  proc.stderr.on('data', d => toRenderer('log', d.toString().trim()));
  proc.on('exit', code => {
    toRenderer('status', { running: false, exited: true, code });
    proc = null;
  });
}

function send(obj) {
  if (proc && proc.stdin.writable) proc.stdin.write(JSON.stringify(obj) + '\n');
}

app.whenReady().then(() => {
  ambi = new AmbiLight();
  ambi.onEvent = (event, data) => toRenderer('ambi', { event, data });

  win = new BrowserWindow({
    width: 1000, height: 760,
    backgroundColor: '#0a0b0d',
    webPreferences: {
      preload: path.join(__dirname, 'preload.js'),
      contextIsolation: true,
      nodeIntegration: false,
    },
  });
  win.loadFile('index.html');
  startChild();
});

// Commands from the renderer. Distinguish capture controls from strip/network
// controls by the `cmd` namespace.
ipcMain.on('cmd', (_, obj) => {
  if (obj.cmd === 'ambi-discover')        { ambi && ambi.discover(); return; }
  if (obj.cmd === 'ambi-start')           { strip.host = obj.host; strip.port = obj.port || 7777; ambi && ambi.start(strip.host, strip.port); return; }
  if (obj.cmd === 'ambi-stop')            { ambi && ambi.stop(); return; }
  if (obj.cmd === 'ambi-clear')           { ambi && ambi.clear(); return; }
  if (obj.cmd === 'ambi-leds')            { strip.leds = Math.max(1, obj.leds | 0); return; }
  // Everything else is a capture.exe command (start/stop/exit).
  send(obj);
});

app.on('will-quit', () => {
  if (ambi) ambi.stop();
  send({ cmd: 'exit' });
  setTimeout(() => { if (proc) proc.kill(); }, 400);
});