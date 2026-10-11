// protocol.h - AmbiLight WiFi frame protocol (shared by firmware builds)
//
// Keep this in sync with electron-app/protocol.js. The two are small and
// deliberately duplicated so the firmware has no dependency on the app repo.
//
// All multi-byte fields are little-endian.
//
//   offset  size  field
//   ------  ----  -----
//        0     2  magic   0xAA 0x55
//        2     1  version (kProtoVersion)
//        3     1  flags   (kFlagClear, ...)
//        4     4  seq     monotonic frame counter, wraps
//        8     2  count   number of LEDs in this frame
//       10  n*3  pixels  R,G,B per LED, in strip order
//
// Datagrams are self-contained: a lost one just means a dropped frame, so
// there is no checksum and no retransmit. UDP already carries a CRC.
//
// Discovery uses the same header with count == 0, which keeps the parser in
// one piece:
//   PC  -> board : flags = kFlagDiscover, payload empty
//   board -> PC  : flags = kFlagAnnounce, payload = UTF-8 JSON status
//
// The status JSON is also what the board logs over serial, so there is a
// single status representation everywhere.

#pragma once

#include <stdint.h>

static const uint8_t  kProtoMagic0  = 0xAA;
static const uint8_t  kProtoMagic1  = 0x55;
static const uint8_t  kProtoVersion = 1;

// Header size in bytes; pixels start here.
static const uint16_t kProtoHeaderLen = 10;

// Largest datagram we ever send or accept: header + full strip.
// 10 + 512*3 = 1546, comfortably under the 1500-byte Ethernet MTU only if the
// strip is short. Above ~495 LEDs the datagram would fragment, so the app is
// expected to respect kProtoSafeMaxLeds for a single datagram.
static const uint16_t kProtoSafeMaxLeds = 490;

enum ProtoFlags : uint8_t {
    kFlagNone     = 0x00,
    kFlagClear    = 0x01,  // stop output, blank the strip (all pixels ignored)
    kFlagDiscover = 0x02,  // PC -> board: "who is out there?"
    kFlagAnnounce = 0x04,  // board -> PC: "here I am", payload is JSON status
};

// Status JSON field names, shared with the Electron side.
//   {"host":"ambilight","ip":"192.168.1.42","mac":"...","fw":"1.0.0",
//    "maxLeds":512,"leds":300,"fps":42,"uptime":1234,"rssi":-55}
