#include "encoder.h"
#include "audio_engine.h"
#include "leds.h"
#include "wifi_mqtt.h"   // ⭐ Needed for mqttPublishState()

extern volatile int32_t g_detentCount;
extern bool g_muted;
extern bool modeFlashActive;

extern float targetGain;

inline float clamp01(float x) { return fminf(fmaxf(x, 0.0f), 1.0f); }
inline float clamp(float x, float lo, float hi) { return fminf(fmaxf(x, lo), hi); }

// -----------------------------------------------------------------------------
// ISR — DO NOT MODIFY (your original working quadrature decoder)
// -----------------------------------------------------------------------------
void IRAM_ATTR readEncoder() {
  static uint8_t lastState = 0;

  const uint8_t a = static_cast<uint8_t>(digitalRead(ENC_A_PIN));
  const uint8_t b = static_cast<uint8_t>(digitalRead(ENC_B_PIN));
  const uint8_t state = static_cast<uint8_t>((a << 1) | b);

  // Standard 2-bit quadrature transition table. Only count a full
  // 00 -> ... -> 00 cycle, which gives one count per detent for the
  // encoder used by this project.
  static const int8_t transition[4][4] = {
    { 0, -1, +1,  0},
    {+1,  0,  0, -1},
    {-1,  0,  0, +1},
    { 0, +1, -1,  0}
  };

  const int8_t delta = transition[lastState][state];
  static int8_t accumulator = 0;
  accumulator += delta;

  if (state == 0 && lastState != 0) {
    if (accumulator >= 3) {
      g_detentCount++;
    } else if (accumulator <= -3) {
      g_detentCount--;
    }
    accumulator = 0;
  }

  lastState = state;
}

// -----------------------------------------------------------------------------
// Setup
// -----------------------------------------------------------------------------
void encoderSetup() {
  pinMode(ENC_A_PIN, INPUT_PULLUP);
  pinMode(ENC_B_PIN, INPUT_PULLUP);

  attachInterrupt(ENC_A_PIN, readEncoder, CHANGE);
  attachInterrupt(ENC_B_PIN, readEncoder, CHANGE);
}

// -----------------------------------------------------------------------------
// Handle gain changes (MAIN FIX: publish MQTT state when detent changes)
// -----------------------------------------------------------------------------
void handleGain() {
  static int32_t lastStep = -1;

  int32_t step;
  noInterrupts();
  step = g_detentCount;
  interrupts();

  step = clamp(step, ENC_MIN, ENC_MAX);

  noInterrupts();
  g_detentCount = step;
  interrupts();

  if (step != lastStep) {
    Serial.printf("[ENC] Gain detent = %ld\n", (long)step);
    float ui = clamp01((float)(step - ENC_MIN) / (float)(ENC_MAX - ENC_MIN));

    float shaped = powf(ui, GAIN_SHAPE_EXP);
    float gain_dB = GAIN_DB_MIN + (GAIN_DB_MAX - GAIN_DB_MIN) * shaped;
    targetGain = dbToLin(gain_dB);

    if (!g_muted && !modeFlashActive)
      updateVolumeLEDs(ui);

    lastStep = step;

    mqttNeedsPublish = true;

  }
}
