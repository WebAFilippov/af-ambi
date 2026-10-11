#include "net.h"

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>

#include "config.h"

#ifndef AMBI_HOSTNAME
#define AMBI_HOSTNAME "ambilight"
#endif
#ifndef AMBI_UDP_PORT
#define AMBI_UDP_PORT 7777
#endif
#ifndef AMBI_MAX_LEDS
#define AMBI_MAX_LEDS 512
#endif

static const char* kFirmwareVersion = "2.0.0-wifi";

// How long to wait for the stored network before offering the setup portal
// again. Long enough to ride out a router reboot, short enough that a wrong
// password does not strand the board.
static const uint32_t kStaTimeoutMs     = 30000;
static const uint32_t kReconnectDelayMs = 5000;

WiFiUDP g_udp;

static bool     g_mdnsStarted = false;
static uint32_t g_lastAttempt = 0;
static bool     g_setupMode   = false;

static void startMdns() {
    if (g_mdnsStarted) return;

    // The hostname is what the PC resolves: ambilight.local
    if (!MDNS.begin(AMBI_HOSTNAME)) {
        Serial.println(F("[net] mDNS failed to start"));
        return;
    }
    MDNS.addService("ambilight", "udp", AMBI_UDP_PORT);
    g_mdnsStarted = true;
    Serial.printf("[net] mDNS up: http://%s.local/\n", AMBI_HOSTNAME);
}

// Reads the stored SSID without depending on WiFiManager being included here.
static bool haveStoredCredentials() {
    return WiFi.SSID().length() > 0;
}

// Blocks until connected or the timeout expires. Returns true on success.
static bool connectStation(uint32_t timeoutMs) {
    if (haveStoredCredentials() && WiFi.status() != WL_CONNECTED) {
        Serial.printf("[net] connecting to \"%s\"\n", WiFi.SSID().c_str());
        WiFi.begin();  // reuse the credentials stored by WiFiManager

        const uint32_t start = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
            delay(200);
            Serial.print('.');
        }
        Serial.println();
    }
    return WiFi.status() == WL_CONNECTED;
}

void netBegin() {
    WiFi.persistent(false);       // we manage our own flash writes
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.setSleepMode(WIFI_NONE_SLEEP);  // latency matters more than power here
    WiFi.hostname(AMBI_HOSTNAME);

    if (connectStation(kStaTimeoutMs)) {
        Serial.printf("[net] connected, ip=%s rssi=%d\n",
                      WiFi.localIP().toString().c_str(), WiFi.RSSI());
        startMdns();
    } else {
        // No usable network. The captive portal is started from main.cpp so it
        // can own the blocking configuration loop.
        Serial.println(F("[net] no network; setup portal required"));
    }

    g_udp.begin(AMBI_UDP_PORT);
    Serial.printf("[net] UDP listening on %u\n", (unsigned)AMBI_UDP_PORT);
}

void netLoop() {
    if (WiFi.status() == WL_CONNECTED) {
        if (!g_mdnsStarted) {
            startMdns();
            Serial.printf("[net] reconnected, ip=%s\n",
                          WiFi.localIP().toString().c_str());
        }
        MDNS.update();
        return;
    }

    // Dropped. Stop advertising, then retry at a bounded rate so a long outage
    // does not hammer the radio.
    if (g_mdnsStarted) {
        MDNS.end();
        g_mdnsStarted = false;
    }

    const uint32_t now = millis();
    if (now - g_lastAttempt < kReconnectDelayMs) return;
    g_lastAttempt = now;

    Serial.println(F("[net] reconnecting"));
    WiFi.reconnect();
}

bool netConnected() { return WiFi.status() == WL_CONNECTED; }

uint16_t netUdpPort() { return AMBI_UDP_PORT; }

int8_t netRssi() { return (int8_t)WiFi.RSSI(); }

void netWriteStatusJson(char* out, size_t outLen) {
    const bool up = netConnected();
    snprintf(out, outLen,
             "{\"host\":\"%s\",\"ip\":\"%s\",\"mac\":\"%s\",\"fw\":\"%s\","
             "\"maxLeds\":%d,\"leds\":%u,\"rssi\":%d,\"setup\":%s}",
             AMBI_HOSTNAME,
             up ? WiFi.localIP().toString().c_str() : "0.0.0.0",
             WiFi.macAddress().c_str(),
             kFirmwareVersion,
             AMBI_MAX_LEDS,
             configGet().ledCount,
             up ? (int)WiFi.RSSI() : 0,
             g_setupMode ? "true" : "false");
}

// Set by main.cpp when it hands control to the setup portal.
void netSetSetupMode(bool on) { g_setupMode = on; }
