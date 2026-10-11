#include "udp_rx.h"

#include <Arduino.h>
#include <WiFiUdp.h>

#include "config.h"
#include "leds.h"
#include "net.h"
#include "protocol.h"

// Datagrams larger than this are rejected outright. The buffer is static to
// keep the heap untouched on a chip with ~40 KB of usable RAM.
//   10 header + 490*3 payload = 1480, inside a 1500-byte MTU.
static uint8_t g_rxBuf[kProtoHeaderLen + kProtoSafeMaxLeds * 3];

static uint32_t g_frames  = 0;
static uint32_t g_dropped = 0;
static uint32_t g_bad     = 0;
static uint32_t g_lastSeq = 0;
static bool     g_haveSeq = false;

extern WiFiUDP g_udp;

static inline uint16_t readLe16(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static inline uint32_t readLe32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// Answers a discovery probe with our status JSON so the PC can find the board
// without mDNS. The reply goes to the sender, not to broadcast.
static void replyAnnounce(IPAddress to, uint16_t port) {
    char status[256];
    netWriteStatusJson(status, sizeof(status));

    uint8_t pkt[kProtoHeaderLen + sizeof(status)];
    pkt[0] = kProtoMagic0;
    pkt[1] = kProtoMagic1;
    pkt[2] = kProtoVersion;
    pkt[3] = kFlagAnnounce;
    // seq is meaningless for an announce
    pkt[4] = pkt[5] = pkt[6] = pkt[7] = 0;
    const uint16_t len = (uint16_t)strlen(status);
    pkt[8] = (uint8_t)(len & 0xFF);
    pkt[9] = (uint8_t)(len >> 8);
    memcpy(pkt + kProtoHeaderLen, status, len);

    g_udp.beginPacket(to, port);
    g_udp.write(pkt, kProtoHeaderLen + len);
    g_udp.endPacket();
}

uint16_t udpRxPoll() {
    uint16_t applied = 0;

    // Bounded drain: one slow producer must not monopolise loop().
    for (int guard = 0; guard < 8; ++guard) {
        const int size = g_udp.parsePacket();
        if (size <= 0) break;

        if ((size_t)size > sizeof(g_rxBuf)) {
            // Too big to be one of ours, or a fragmented stream we cannot use.
            g_udp.flush();
            ++g_bad;
            continue;
        }

        const int read = g_udp.read(g_rxBuf, sizeof(g_rxBuf));
        if (read < kProtoHeaderLen) { ++g_bad; continue; }

        if (g_rxBuf[0] != kProtoMagic0 || g_rxBuf[1] != kProtoMagic1) {
            ++g_bad;
            continue;
        }
        if (g_rxBuf[2] != kProtoVersion) { ++g_bad; continue; }

        const uint8_t  flags = g_rxBuf[3];
        const uint32_t seq   = readLe32(g_rxBuf + 4);
        const uint16_t count = readLe16(g_rxBuf + 8);

        if (flags & kFlagDiscover) {
            replyAnnounce(g_udp.remoteIP(), g_udp.remotePort());
            continue;
        }

        if (flags & kFlagClear) {
            ledsClear();
            continue;
        }

        // Payload must match the declared count exactly; anything else means a
        // truncated or corrupted datagram.
        if (count == 0 || count > kProtoSafeMaxLeds) { ++g_bad; continue; }
        if (read != kProtoHeaderLen + count * 3)     { ++g_bad; continue; }

        // Track gaps so packet loss is visible in the log without spamming it.
        if (g_haveSeq && seq != g_lastSeq + 1) {
            g_dropped += (seq > g_lastSeq) ? (seq - g_lastSeq - 1) : 1;
        }
        g_lastSeq = seq;
        g_haveSeq = true;

        // Adopt the producer's count. This is what makes the app's "number of
        // LEDs" control take effect without a separate command channel.
        configSetLeds(count);

        const uint8_t* px = g_rxBuf + kProtoHeaderLen;
        for (uint16_t i = 0; i < count; ++i) {
            ledsWritePixel(i, px[i * 3 + 0], px[i * 3 + 1], px[i * 3 + 2]);
        }

        ledsApply();
        ++g_frames;
        ++applied;
    }

    return applied;
}

uint32_t udpRxFrames()  { return g_frames; }
uint32_t udpRxDropped() { return g_dropped; }
uint32_t udpRxBadPackets() { return g_bad; }
