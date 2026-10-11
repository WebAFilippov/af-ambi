// protocol.js - AmbiLight WiFi frame protocol (Electron side)
//
// Mirror of src/esp8266/protocol.h. Keep the two in sync.
//
// Frame (little-endian):
//   [0..1] magic 0xAA 0x55
//   [2]    version (1)
//   [3]    flags  (bit0 clear, bit1 discover, bit2 announce)
//   [4..7] seq (uint32 LE)
//   [8..9] count (uint16 LE)
//   [10..] count * 3 bytes of R,G,B
//
// A UDP datagram is self-contained; loss = dropped frame (no retransmit).
// Discovery: PC sends flags=kFlagDiscover + empty payload, board replies
// kFlagAnnounce with UTF-8 JSON status as the payload.

'use strict';

const MAGIC0 = 0xaa;
const MAGIC1 = 0x55;
const VERSION = 1;
const HEADER_LEN = 10;

const FLAG_NONE = 0x00;
const FLAG_CLEAR = 0x01;
const FLAG_DISCOVER = 0x02;
const FLAG_ANNOUNCE = 0x04;

// Header + 490*3 payload = 1480 bytes, under the 1500-byte Ethernet MTU so a
// single frame never fragments.
const SAFE_MAX_LEDS = 490;

function writeHeader(buf, offset, flags, seq, count) {
  buf[offset + 0] = MAGIC0;
  buf[offset + 1] = MAGIC1;
  buf[offset + 2] = VERSION;
  buf[offset + 3] = flags;
  buf.writeUInt32LE(seq >>> 0, offset + 4);
  buf.writeUInt16LE(count & 0xffff, offset + 8);
}

// Builds a frame datagram from a flat [r,g,b, ...] array. Returns a Buffer.
function buildFrame(leds, seq) {
  const count = Math.min(leds.length / 3, SAFE_MAX_LEDS);
  const buf = Buffer.allocUnsafe(HEADER_LEN + count * 3);
  writeHeader(buf, 0, FLAG_NONE, seq, count);
  for (let i = 0; i < count; i++) {
    buf[HEADER_LEN + i * 3 + 0] = leds[i * 3 + 0] & 0xff;
    buf[HEADER_LEN + i * 3 + 1] = leds[i * 3 + 1] & 0xff;
    buf[HEADER_LEN + i * 3 + 2] = leds[i * 3 + 2] & 0xff;
  }
  return buf;
}

// Builds a "clear the strip" datagram.
function buildClear() {
  const buf = Buffer.alloc(HEADER_LEN);
  writeHeader(buf, 0, FLAG_CLEAR, 0, 0);
  return buf;
}

// Builds a discovery probe.
function buildDiscover() {
  const buf = Buffer.alloc(HEADER_LEN);
  writeHeader(buf, 0, FLAG_DISCOVER, 0, 0);
  return buf;
}

// Parses a received datagram. Returns null if it is not ours.
//   { flags, seq, count, payload:Buffer }
function parseDatagram(buf) {
  if (!buf || buf.length < HEADER_LEN) return null;
  if (buf[0] !== MAGIC0 || buf[1] !== MAGIC1) return null;
  if (buf[2] !== VERSION) return null;
  const flags = buf[3];
  const seq = buf.readUInt32LE(4);
  const count = buf.readUInt16LE(8);
  const payload = buf.subarray(HEADER_LEN, HEADER_LEN + count * 3);
  return { flags, seq, count, payload };
}

module.exports = {
  HEADER_LEN,
  SAFE_MAX_LEDS,
  FLAG_NONE,
  FLAG_CLEAR,
  FLAG_DISCOVER,
  FLAG_ANNOUNCE,
  buildFrame,
  buildClear,
  buildDiscover,
  parseDatagram,
};
