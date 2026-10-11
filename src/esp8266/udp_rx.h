// udp_rx.h - frame receiver

#pragma once

#include <stdint.h>

// Drains pending datagrams. Returns the number of frames applied.
uint16_t udpRxPoll();

// Counters for the status JSON / diagnostics.
uint32_t udpRxFrames();
uint32_t udpRxDropped();
uint32_t udpRxBadPackets();
