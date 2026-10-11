const { app, BrowserWindow, ipcMain } = require('electron');
const { spawn } = require('child_process');
const path = require('path');
const readline = require('readline');

let win = null;
let proc = null;

function toRenderer(channel, data) {
  if (win && !win.isDestroyed()) win.webContents.send(channel, data);
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
    else if (msg.leds)                    toRenderer('frame', msg);
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

ipcMain.on('cmd', (_, obj) => send(obj));

app.on('will-quit', () => {
  send({ cmd: 'exit' });
  setTimeout(() => { if (proc) proc.kill(); }, 400);
});