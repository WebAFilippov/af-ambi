// main.cpp - AmbiLight firmware for WeMos D1 Mini V2 (ESP8266)
//
// Receives LED frames over WiFi (UDP) and drives a WS2815 strip using
// FastLED's hardware-UART output so WiFi and the strip do not fight for the
// CPU. First boot (no stored credentials) raises a captive portal for Wi-Fi
// setup; afterwards it reconnects automatically and advertises itself via
// mDNS at <hostname>.local.
//
// See src/esp8266/README.md for wiring, protocol and build notes.

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiManager.h>

#include "config.h"
#include "leds.h"
#include "net.h"
#include "udp_rx.h"
#include "web.h"

// If no frames arrive in this window, blank the strip so it does not freeze
// on a stale image after the PC disconnects or sleeps.
static const uint32_t kWatchdogMs = 3000;

// Period for the periodic status log (ms).
static const uint32_t kStatusLogMs = 5000;

static uint32_t g_lastStatusLog = 0;

// Config portal settings (WiFiManager). We pass the plain object so the
// library owns lifecycle; it survives for the whole program.
static WiFiManager g_wm;

// Runs the setup portal. Blocks until credentials are saved or the timeout
// expires, then reboots so the board starts cleanly on the chosen network.
static void runSetupPortal() {
    Serial.println(F("[main] entering Wi-Fi setup portal"));
    netSetSetupMode(true);

    // A reset wipes any half-written SDK config so a re-flash starts clean.
    g_wm.resetSettings();

    // 5 minutes is plenty; on timeout the board reboots and retries the portal.
    g_wm.setConfigPortalTimeout(300);

    // Parameters that can also be set from the portal, not just the app.
    // Keep them simple: the main UI drives the strip, the portal only needs
    // the network.
    if (!g_wm.startConfigPortal("AmbiLight-Setup")) {
        Serial.println(F("[main] setup portal timed out, rebooting"));
        ESP.restart();
    }

    // Credentials stored; reboot onto the network.
    Serial.println(F("[main] credentials saved, rebooting"));
    delay(500);
    ESP.restart();
}

void setup() {
    Serial.begin(115200);
    delay(50);

    Serial.println();
    Serial.println(F("[main] AmbiLight ESP8266 (WS2815 over WiFi)"));

    configBegin();
    ledsBegin();

    netBegin();

    // If we are not connected, that means there were no stored credentials or
    // they failed: raise the captive portal. netBegin already signalled this.
    if (!netConnected()) {
        runSetupPortal();
        // runSetupPortal reboots; this line is not normally reached.
    }

    webBegin();
    Serial.println(F("[main] ready"));
}

void loop() {
    netLoop();
    webLoop();

    // Apply frames as fast as they arrive, but never starve the other tasks:
    // the poll is internally bounded.
    udpRxPoll();

    ledsTickFps();

    // Watchdog: silence from the PC blanks the strip.
    if (ledsLastFrameMs() && millis() - ledsLastFrameMs() > kWatchdogMs) {
        ledsClear();
    }

    // Periodic status line so `pio device monitor` shows liveness.
    if (millis() - g_lastStatusLog >= kStatusLogMs) {
        g_lastStatusLog = millis();
        char status[256];
        netWriteStatusJson(status, sizeof(status));
        Serial.printf("[status] frames=%lu dropped=%lu bad=%lu fps=%u %s\n",
                      (unsigned long)udpRxFrames(),
                      (unsigned long)udpRxDropped(),
                      (unsigned long)udpRxBadPackets(),
                      (unsigned)ledsFps(),
                      status);
    }
}
