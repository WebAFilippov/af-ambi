// ambilight.js - discovery + UDP streaming to the ESP8266 board
//
// Lives in the main process (Node side). It owns the UDP socket, tracks the
// board's IP via mDNS and/or the UDP announce reply, and exposes a small
// state machine to the rest of the app.
//
//   - sendFrame(leds)   encode and stream one frame (skips if not connected)
//   - clear()           blank the strip
//   - start(host, port) begin streaming to a resolved board
//   - stop()            stop streaming
//   - discover()        broadcast a probe, collect announce replies
//   - state             'idle' | 'discovering' | 'streaming' | 'error'

'use strict';

const dgram = require('dgram');
const proto = require('./protocol');

const DEFAULT_PORT = 7777;
const DISCOVER_PORT = 7777;
const BROADCAST_ADDR = '255.255.255.255';

class AmbiLight {
  constructor() {
    this.port = DEFAULT_PORT;
    this.host = null;
    this.seq = 0;
    this.state = 'idle';
    this.boards = new Map(); // mac -> { host, ip, port, status, seen }
    this.onEvent = null;     // (event, data) => void, for IPC forward

    this.socket = dgram.createSocket('udp4');
    this.socket.on('message', (msg, rinfo) => this._onMessage(msg, rinfo));
    this.socket.on('error', (err) => {
      this._emit('error', { message: err.message });
    });
    this.socket.bind(DEFAULT_PORT, () => {
      try { this.socket.setBroadcast(true); } catch (_) { /* not fatal */ }
    });
  }

  _emit(event, data) {
    if (this.onEvent) this.onEvent(event, data);
  }

  // --- discovery ---------------------------------------------------------

  // Broadcasts a probe. Announce replies populate `boards` and fire 'board'.
  discover() {
    if (this.state === 'discovering') return;
    this.state = 'discovering';
    this.boards.clear();

    const probe = proto.buildDiscover();
    const send = () => {
      this.socket.send(probe, 0, probe.length, DISCOVER_PORT, BROADCAST_ADDR,
        () => { /* best effort */ });
    };
    send();
    // A couple of repeats helps on lossy/broadcast-throttled LANs.
    const t1 = setTimeout(send, 300);
    const t2 = setTimeout(send, 900);

    setTimeout(() => {
      clearTimeout(t1);
      clearTimeout(t2);
      this.state = 'idle';
      this._emit('discover-done', { boards: this.listBoards() });
    }, 1500);
  }

  listBoards() {
    return Array.from(this.boards.values()).map((b) => ({ ...b }));
  }

  // --- streaming ---------------------------------------------------------

  // Start streaming to a resolved host. `host` may be a name (ambilight.local)
  // which dgram resolves on each send.
  start(host, port = DEFAULT_PORT) {
    this.host = host;
    this.port = port;
    this.seq = 0;
    this.state = 'streaming';
    this._emit('status', { streaming: true, host, port });
  }

  stop() {
    const wasStreaming = this.state === 'streaming';
    this.state = 'idle';
    if (wasStreaming) {
      // Blank the strip on stop so it does not freeze on the last frame.
      this._sendRaw(proto.buildClear());
    }
    this._emit('status', { streaming: false });
  }

  clear() {
    this._sendRaw(proto.buildClear());
  }

  // Stream one frame. `leds` is a flat [r,g,b, ...] array.
  sendFrame(leds) {
    if (this.state !== 'streaming' || !this.host) return false;
    const pkt = proto.buildFrame(leds, this.seq++);
    this._sendRaw(pkt);
    return true;
  }

  _sendRaw(pkt) {
    if (!this.host) return;
    this.socket.send(pkt, 0, pkt.length, this.port, this.host, (err) => {
      if (err) this._emit('error', { message: 'UDP send: ' + err.message });
    });
  }

  // --- receive -----------------------------------------------------------

  _onMessage(msg, rinfo) {
    const parsed = proto.parseDatagram(msg);
    if (!parsed) return;

    if (parsed.flags & proto.FLAG_ANNOUNCE) {
      const status = this._tryParseStatus(parsed.payload);
      const key = (status && status.mac) || rinfo.address;
      this.boards.set(key, {
        host: (status && status.host) || 'ambilight',
        ip: rinfo.address,
        port: rinfo.port,
        status,
        seen: Date.now(),
      });
      this._emit('board', { host: (status && status.host) || 'ambilight',
                             ip: rinfo.address, port: rinfo.port, status });
    }
  }

  _tryParseStatus(payload) {
    try {
      return JSON.parse(payload.toString('utf8'));
    } catch (_) {
      return null;
    }
  }
}

module.exports = { AmbiLight };
