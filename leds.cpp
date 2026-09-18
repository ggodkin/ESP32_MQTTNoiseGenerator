#include "leds.h"
#include <Adafruit_NeoPixel.h>
#include "audio_engine.h"

Adafruit_NeoPixel led(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

bool modeFlashActive = false;
uint32_t modeFlashUntil = 0;

extern volatile NoiseMode g_noiseMode;
extern bool g_muted;
// g_detentCount is declared in audio_engine.h

uint32_t modeColor(NoiseMode mode) {
  return
    (mode == MODE_WHITE) ? led.Color(255,255,255) :
    (mode == MODE_PINK)  ? led.Color(255,20,147)  :
    (mode == MODE_BROWN) ? led.Color(255,80,0)    :
                           led.Color(0,80,255);
}

void ledsSetup() {
  led.begin();
  led.setBrightness(40);
  led.clear();
  led.show();
}

void showModeColor(NoiseMode mode) {
  uint32_t c = modeColor(mode);
  for (int i = 0; i < LED_COUNT; i++)
    led.setPixelColor(i, c);
  led.show();
}

void showMute() {
  for (int i = 0; i < LED_COUNT; i++)
    led.setPixelColor(i, led.Color(255, 0, 0));
  led.show();
}

void updateVolumeLEDs(float ui) {
  uint32_t c = modeColor(g_noiseMode);

  uint8_t baseR = (c >> 16) & 0xFF;
  uint8_t baseG = (c >>  8) & 0xFF;
  uint8_t baseB =  c        & 0xFF;

  float level = ui * 4.0f;
  int full = (int)level;
  float frac = level - full;

  for (int i = 0; i < 4; i++) {
    float scale = 0.0f;

    if (ui == 0.0f) {
      scale = (i == 0) ? 0.50f : 0.0f;
    }
    else {
      if (i < full) scale = 1.0f;
      else if (i == full) scale = frac;
    }

    uint8_t r = (uint8_t)(baseR * scale);
    uint8_t g = (uint8_t)(baseG * scale);
    uint8_t b = (uint8_t)(baseB * scale);

    led.setPixelColor(i, led.Color(r, g, b));
  }

  led.show();
}

void handleModeFlash() {
  if (modeFlashActive && millis() > modeFlashUntil) {
    modeFlashActive = false;
    if (!g_muted) {
      float ui = fminf(fmaxf((float)(g_detentCount - ENC_MIN) /
                             (float)(ENC_MAX - ENC_MIN), 0.0f), 1.0f);
      updateVolumeLEDs(ui);
    }
  }
}
