#include "config.h"

#include <Arduino.h>
#include <LittleFS.h>

#include "protocol.h"

#ifndef AMBI_MAX_LEDS
#define AMBI_MAX_LEDS 512
#endif

static const char* kConfigPath = "/config.json";

static BoardConfig g_cfg = { 0, 255, 0 };
static bool g_dirty = false;

static void applyDefaults() {
    g_cfg.ledCount   = 144;   // sensible mid-size strip
    g_cfg.brightness = 255;
    g_cfg.powerLimit = 0;     // unlimited
}

void configBegin() {
    applyDefaults();

    if (!LittleFS.begin()) {
        // Formatting on first boot is expected: the filesystem starts empty.
        Serial.println(F("[cfg] littleFS mount failed, formatting"));
        if (!LittleFS.format() || !LittleFS.begin()) {
            Serial.println(F("[cfg] littleFS unavailable, using defaults"));
            return;
        }
    }

    File f = LittleFS.open(kConfigPath, "r");
    if (!f) {
        Serial.println(F("[cfg] no config file, using defaults"));
        return;
    }

    // The file is tiny; parse it with the same tolerant key lookup the rest of
    // the firmware uses rather than pulling in a JSON library.
    String body = f.readString();
    f.close();

    auto findInt = [&body](const char* key, long fallback) -> long {
        int p = body.indexOf(key);
        if (p < 0) return fallback;
        p = body.indexOf(':', p);
        if (p < 0) return fallback;
        return body.substring(p + 1).toInt();
    };

    long leds = findInt("\"ledCount\"", g_cfg.ledCount);
    long bright = findInt("\"brightness\"", g_cfg.brightness);
    long power = findInt("\"powerLimit\"", g_cfg.powerLimit);

    // Clamp rather than reject: a stale file from an older firmware must not
    // produce an out-of-range count that overruns the pixel buffer.
    if (leds < 1) leds = 1;
    if (leds > AMBI_MAX_LEDS) leds = AMBI_MAX_LEDS;
    if (bright < 0) bright = 0;
    if (bright > 255) bright = 255;
    if (power < 0) power = 0;
    if (power > 60000) power = 60000;

    g_cfg.ledCount   = (uint16_t)leds;
    g_cfg.brightness = (uint8_t)bright;
    g_cfg.powerLimit = (uint16_t)power;

    Serial.printf("[cfg] loaded leds=%u brightness=%u powerLimit=%u\n",
                  g_cfg.ledCount, g_cfg.brightness, g_cfg.powerLimit);
}

const BoardConfig& configGet() { return g_cfg; }

static void markDirty() { g_dirty = true; }

void configSetLeds(uint16_t count) {
    if (count < 1) count = 1;
    if (count > AMBI_MAX_LEDS) count = AMBI_MAX_LEDS;
    if (count == g_cfg.ledCount) return;
    g_cfg.ledCount = count;
    markDirty();
}

void configSetBrightness(uint8_t value) {
    if (value == g_cfg.brightness) return;
    g_cfg.brightness = value;
    markDirty();
}

void configSetPowerLimit(uint16_t milliamps) {
    if (milliamps == g_cfg.powerLimit) return;
    g_cfg.powerLimit = milliamps;
    markDirty();
}

void configSave() {
    if (!g_dirty) return;

    File f = LittleFS.open(kConfigPath, "w");
    if (!f) {
        Serial.println(F("[cfg] save failed"));
        return;
    }

    f.printf("{\"ledCount\":%u,\"brightness\":%u,\"powerLimit\":%u}\n",
             g_cfg.ledCount, g_cfg.brightness, g_cfg.powerLimit);
    f.close();
    g_dirty = false;

    Serial.printf("[cfg] saved leds=%u brightness=%u powerLimit=%u\n",
                  g_cfg.ledCount, g_cfg.brightness, g_cfg.powerLimit);
}

void configReset() {
    LittleFS.remove(kConfigPath);
    applyDefaults();
    g_dirty = false;
    Serial.println(F("[cfg] reset to defaults"));
}
