#pragma once
#include <Arduino.h>

// I2S
constexpr int I2S_SAMPLE_RATE     = 44100;
constexpr int I2S_BCK_PIN         = 26;
constexpr int I2S_WS_PIN          = 25;
constexpr int I2S_DATA_OUT_PIN    = 22;
constexpr int I2S_BUFFER_SAMPLES  = 256;

// Encoder + button
constexpr int ENC_A_PIN           = 32;
constexpr int ENC_B_PIN           = 33;
constexpr int ENC_SW_PIN          = 27;

// LEDs
constexpr int LED_PIN             = 16;
constexpr int LED_COUNT           = 8;

// Encoder range
constexpr int32_t ENC_MIN         = 0;
constexpr int32_t ENC_MAX         = 20;

// Gain / DSP
constexpr float GAIN_DB_MIN       = -36.0f;
constexpr float GAIN_DB_MAX       = -3.0f;
constexpr float GAIN_SHAPE_EXP    = 1.5f;
constexpr float SLEW_RATE         = 0.01f;

constexpr float DC_BLOCK_R        = 0.999f;

constexpr float BROWN_LEAK        = 0.999f;
constexpr float BROWN_STEP        = 0.02f;

constexpr float BLUE_NORM         = 0.5f;
constexpr float BLUE_LP_A         = 0.85f;
constexpr float BLUE_LP_B         = 0.15f;

constexpr float PINK_SCALE        = 0.2f;
constexpr float LIMITER_DRIVE     = 1.2f;

// Noise modes
enum NoiseMode : uint8_t {
  MODE_WHITE = 0,
  MODE_PINK  = 1,
  MODE_BROWN = 2,
  MODE_BLUE  = 3,
  MODE_COUNT = 4
};
