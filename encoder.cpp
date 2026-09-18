#include "encoder.h"
#include "soc/gpio_reg.h"
#include "audio_engine.h"
#include "leds.h"
#include "wifi_mqtt.h"   // ⭐ Needed for mqttPublishState()

extern int32_t g_detentCount;
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
  static int8_t direction = 0;

  uint32_t inHigh = REG_READ(GPIO_IN1_REG);

  uint8_t a = (inHigh >> (ENC_A_PIN - 32)) & 1;
  uint8_t b = (inHigh >> (ENC_B_PIN - 32)) & 1;

  uint8_t state = (a << 1) | b;

  if ((lastState == 0b00 && state == 0b01) ||
      (lastState == 0b01 && state == 0b11) ||
      (lastState == 0b11 && state == 0b10) ||
      (lastState == 0b10 && state == 0b00)) {
    direction = +1;
  }
  else if ((lastState == 0b00 && state == 0b10) ||
           (lastState == 0b10 && state == 0b11) ||
           (lastState == 0b11 && state == 0b01) ||
           (lastState == 0b01 && state == 0b00)) {
    direction = -1;
  }

  if (state == 0b00 && lastState != 0b00) {
    g_detentCount += direction;
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
