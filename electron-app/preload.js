const { contextBridge, ipcRenderer } = require('electron');

const allowed = ['monitors', 'status', 'frame', 'log'];

contextBridge.exposeInMainWorld('api', {
  send: obj => ipcRenderer.send('cmd', obj),
  on: (channel, fn) => {
    if (allowed.includes(channel)) ipcRenderer.on(channel, (_, data) => fn(data));
  },
});