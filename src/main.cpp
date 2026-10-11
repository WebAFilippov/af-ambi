// ESP32-C3 SuperMini — WS2815 через USB CDC
#include <Arduino.h>
#include <FastLED.h>

#define LED_PIN        5
#define MAX_LEDS       512
#define BAUD           921600
#define RX_TIMEOUT_MS  3000   // если 3 с нет кадров — гасим ленту

static CRGB    leds[MAX_LEDS];
static uint8_t rxBuf[MAX_LEDS * 3];

static uint16_t activeLeds  = 0;
static uint32_t lastFrameMs = 0;

// ---- парсер ----
enum RxState : uint8_t {
  RX_MAGIC1, RX_MAGIC2, RX_LEN_LO, RX_LEN_HI,
  RX_FLAGS, RX_SEQ, RX_PAYLOAD, RX_CHECKSUM
};
static RxState  rxState = RX_MAGIC1;
static uint8_t  rxXor   = 0;
static uint16_t rxLen   = 0;
static uint16_t rxPos   = 0;

static inline void resetParser() {
  rxState = RX_MAGIC1;
  rxXor   = 0;
  rxLen   = 0;
  rxPos   = 0;
}

static void applyFrame() {
  for (uint16_t i = 0; i < rxLen; i++) {
    leds[i].r = rxBuf[i * 3 + 0];
    leds[i].g = rxBuf[i * 3 + 1];
    leds[i].b = rxBuf[i * 3 + 2];
  }
  // если количество уменьшилось — погасим хвост
  if (rxLen < activeLeds) {
    for (uint16_t i = rxLen; i < activeLeds; i++) leds[i] = CRGB::Black;
  }
  activeLeds  = rxLen;
  FastLED.show();
  lastFrameMs = millis();
}

void setup() {
  Serial.begin(BAUD);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 2000) delay(10);

  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, MAX_LEDS);
  FastLED.setBrightness(255);
  FastLED.clear(true);

  // Boot-анимация: R/G/B по первым 10 LED — быстрая проверка пайки
  for (int phase = 0; phase < 3; phase++) {
    FastLED.clear();
    for (int i = 0; i < 10 && i < MAX_LEDS; i++) {
      leds[i] = (phase == 0) ? CRGB::Red
              : (phase == 1) ? CRGB::Green
                             : CRGB::Blue;
    }
    FastLED.show();
    delay(150);
  }
  FastLED.clear(true);

  // Сообщаем ПК, что готовы
  Serial.println("{\"event\":\"ready\",\"maxLeds\":512}");
  lastFrameMs = millis();
}

void loop() {
  // ---- читаем входящие байты ----
  while (Serial.available() > 0) {
    uint8_t b = (uint8_t)Serial.read();
    switch (rxState) {
      case RX_MAGIC1:
        if (b == 0xAA) { rxXor = b; rxState = RX_MAGIC2; }
        break;

      case RX_MAGIC2:
        if (b == 0x55) { rxXor ^= b; rxState = RX_LEN_LO; }
        else            { resetParser(); }
        break;

      case RX_LEN_LO:
        rxXor ^= b; rxLen = b; rxState = RX_LEN_HI;
        break;

      case RX_LEN_HI:
        rxXor ^= b;
        rxLen |= (uint16_t)b << 8;
        if (rxLen == 0 || rxLen > MAX_LEDS) { resetParser(); break; }
        rxState = RX_FLAGS;
        break;

      case RX_FLAGS:
        rxXor ^= b;                 // пока игнорируем содержимое
        rxState = RX_SEQ;
        break;

      case RX_SEQ:
        rxXor ^= b;                 // счётчик кадров — для диагностики
        rxPos = 0;
        rxState = RX_PAYLOAD;
        break;

      case RX_PAYLOAD:
        rxXor ^= b;
        rxBuf[rxPos++] = b;
        if (rxPos >= rxLen * 3) rxState = RX_CHECKSUM;
        break;

      case RX_CHECKSUM:
        if (b == rxXor) applyFrame();
        resetParser();
        break;
    }
  }

  // ---- watchdog: ПК замолчал — гасим ----
  if (activeLeds > 0 && millis() - lastFrameMs > RX_TIMEOUT_MS) {
    FastLED.clear(true);
    activeLeds = 0;
  }
}