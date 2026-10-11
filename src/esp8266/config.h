// config.h - persisted board settings (littleFS)
//
// Stores the handful of values that must survive a power cycle. WiFi
// credentials are handled by WiFiManager (ESP8266 SDK storage), this holds
// everything the app configured.
//
// Missing or corrupt file falls back to defaults rather than refusing to
// boot, so a bad write can never brick the strip.

#pragma once

#include <stdint.h>

struct BoardConfig {
    uint16_t ledCount;   // LEDs actually driven; <= AMBI_MAX_LEDS
    uint8_t  brightness; // global scale, 0..255
    uint16_t powerLimit; // milliamps; 0 = unlimited
};

void configBegin();
const BoardConfig& configGet();
void configSetLeds(uint16_t count);
void configSetBrightness(uint8_t value);
void configSetPowerLimit(uint16_t milliamps);
void configSave();

// Unlinks the config file; used by the factory-reset button.
void configReset();
