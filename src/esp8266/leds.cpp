#include "leds.h"

#include <Arduino.h>

// FASTLED_ESP8266_UART must be defined before <FastLED.h> - it is set in
// platformio.ini. It pulls in UARTController_ESP8266, which encodes WS2812
// bits into UART bytes so the hardware shifter produces the waveform.
#include <FastLED.h>

#include "config.h"
#include "protocol.h"

#ifndef AMBI_MAX_LEDS
#define AMBI_MAX_LEDS 512
#endif

// UART1 TX-only on ESP8266 is GPIO2, which is D4 on the D1 Mini. The driver
// takes the pin but cannot move it: the UART peripheral decides.
static const int kDataPin = 2;

static CRGB g_leds[AMBI_MAX_LEDS];

// The UART controller is instantiated explicitly because there is no
// addLeds<> template overload whose chipset shape matches this driver
// (it is template<EOrder>, not template<u8, EOrder>). The CLEDController*
// overload accepts a controller instance directly. It has no per-pixel state,
// so a single file-scope static is safe.
static fl::UARTController_ESP8266<GRB> g_controller;

static uint32_t g_lastFrameMs = 0;
static uint32_t g_fpsWindowStart = 0;
static uint16_t g_fpsFrames = 0;
static uint16_t g_fps = 0;

void ledsBegin() {
    // Explicit controller instance: the UART driver cannot be selected via the
    // addLeds<> chipset template (see note above the controller declaration).
    FastLED.addLeds(&g_controller, g_leds, AMBI_MAX_LEDS);
    FastLED.setBrightness(configGet().brightness);

    if (configGet().powerLimit > 0) {
        FastLED.setMaxPowerInVoltsAndMilliamps(12, configGet().powerLimit);
    }

    FastLED.clear(true);
    g_lastFrameMs = millis();
    g_fpsWindowStart = g_lastFrameMs;

    Serial.printf("[led] WS2815 on GPIO%d (UART hw), max %d LEDs\n",
                  kDataPin, AMBI_MAX_LEDS);
}

void ledsApply() {
    FastLED.setBrightness(configGet().brightness);

    const uint16_t n = configGet().ledCount;

    // LEDs beyond the configured count keep their old colour unless we blank
    // them, which looks like a lit tail after the app lowers the count.
    for (uint16_t i = n; i < AMBI_MAX_LEDS; ++i) {
        g_leds[i] = CRGB::Black;
    }

    FastLED.show();

    g_lastFrameMs = millis();
    ++g_fpsFrames;
}

void ledsShow() { ledsApply(); }

void ledsClear() {
    FastLED.clear(true);
    g_lastFrameMs = millis();
}

uint16_t ledsCount() { return configGet().ledCount; }

void ledsWritePixel(uint16_t index, uint8_t r, uint8_t g, uint8_t b) {
    if (index >= AMBI_MAX_LEDS) return;
    // FastLED is configured GRB; assign named channels so the strip order is
    // handled by the driver, not by us.
    g_leds[index].r = r;
    g_leds[index].g = g;
    g_leds[index].b = b;
}

uint32_t ledsLastFrameMs() { return g_lastFrameMs; }

void ledsMarkFrame() { g_lastFrameMs = millis(); }

void ledsTickFps() {
    const uint32_t now = millis();
    const uint32_t elapsed = now - g_fpsWindowStart;
    if (elapsed < 1000) return;

    g_fps = (uint16_t)((uint32_t)g_fpsFrames * 1000UL / elapsed);
    g_fpsFrames = 0;
    g_fpsWindowStart = now;
}

uint16_t ledsFps() { return g_fps; }
