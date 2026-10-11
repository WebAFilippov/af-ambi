// net.h - WiFi station lifecycle, mDNS and UDP transport

#pragma once

#include <stddef.h>
#include <stdint.h>

// Brings up WiFi. If no credentials are stored, starts the captive portal
// instead. Returns once the board is either on the network or serving setup.
void netBegin();

// Call from loop(). Retries the connection when it drops and restarts the
// setup portal if the network stays unreachable.
void netLoop();

bool     netConnected();
uint16_t netUdpPort();
int8_t   netRssi();

// Human-readable JSON status, shared by the serial log, the UDP announce
// reply and the web UI.
void netWriteStatusJson(char* out, size_t outLen);

// Flags the board as serving the setup portal, which the status JSON reports.
void netSetSetupMode(bool on);
