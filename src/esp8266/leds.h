// leds.h - WS2815 output
//
// Uses FastLED's hardware-UART driver (FASTLED_ESP8266_UART) so the CPU is not
// tied up shifting pixels. See src/esp8266/README.md for why this matters.

#pragma once

#include <stdint.h>

void ledsBegin();
void ledsShow();
void ledsClear();

// Applies brightness/power limits and pushes the buffer to the strip.
void ledsApply();

// Number of LEDs currently driven (from config).
uint16_t ledsCount();

// Raw pixel buffer access for the UDP receiver. Writes straight into the
// FastLED buffer to avoid a second copy on a RAM-constrained chip.
void ledsWritePixel(uint16_t index, uint8_t r, uint8_t g, uint8_t b);

// Millis timestamp of the last accepted frame; used by the watchdog.
uint32_t ledsLastFrameMs();
void     ledsMarkFrame();

// Rolling output rate, frames per second.
uint16_t ledsFps();
void     ledsTickFps();
