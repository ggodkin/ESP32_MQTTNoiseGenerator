#include "audio_engine.h"
#include <driver/i2s.h>
#include "soc/gpio_reg.h"
#include <Adafruit_NeoPixel.h>
#include "leds.h"
#include "encoder.h"

// Shared globals from original code
volatile NoiseMode g_noiseMode = MODE_PINK;
bool g_muted = false;
int32_t g_detentCount = 0;

// PRNG state
static uint64_t xs_state[2] = {
  0x123456789abcdefULL,
  0xfedcba987654321ULL
};

// pink noise state
struct PinkState {
  float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
};
static PinkState pinkState;

// brown / blue state
static float brown = 0.0f;
static float lastWhite = 0.0f;
static float blueLP = 0.0f;

// dc block state
static float dc_prevX = 0.0f;
static float dc_prevY = 0.0f;

// gain state
float targetGain  = 0.5f;
float currentGain = 0.5f;

// audio buffer
static int32_t i2sBuffer[I2S_BUFFER_SAMPLES];

inline float clamp01(float x) { return fminf(fmaxf(x, 0.0f), 1.0f); }
inline float clamp(float x, float lo, float hi) { return fminf(fmaxf(x, lo), hi); }
float dbToLin(float db) {
    return powf(10.0f, db * 0.05f);
}


inline uint64_t rotl(const uint64_t x, int k) {
  return (x << k) | (x >> (64 - k));
}

inline uint32_t xoroshiro128plus() {
  uint64_t s0 = xs_state[0];
  uint64_t s1 = xs_state[1];
  uint64_t result = s0 + s1;

  s1 ^= s0;
  xs_state[0] = rotl(s0, 55) ^ s1 ^ (s1 << 14);
  xs_state[1] = rotl(s1, 36);

  return static_cast<uint32_t>(result >> 32);
}

inline float whiteNoise() {
  return static_cast<int32_t>(xoroshiro128plus()) * (1.0f / 2147483648.0f);
}

inline float nextPinkSample(PinkState &ps) {
  float white = whiteNoise() + 1e-12f;

  ps.b0 = fmaf(0.99886f, ps.b0, white * 0.0555179f);
  ps.b1 = fmaf(0.99332f, ps.b1, white * 0.0750759f);
  ps.b2 = fmaf(0.96900f, ps.b2, white * 0.1538520f);
  ps.b3 = fmaf(0.86650f, ps.b3, white * 0.3104856f);
  ps.b4 = fmaf(0.55000f, ps.b4, white * 0.5329522f);
  ps.b5 = fmaf(-0.7616f, ps.b5, -white * 0.0168980f);

  float pink = ps.b0 + ps.b1 + ps.b2 + ps.b3 + ps.b4 + ps.b5 + ps.b6 + white * 0.5362f;
  ps.b6 = white * 0.115926f;

  return tanhf(pink * PINK_SCALE);
}

inline float generateNoiseSample() {
  float w = whiteNoise();

  switch (g_noiseMode) {
    case MODE_WHITE: return w;
    case MODE_PINK:  return nextPinkSample(pinkState);
    case MODE_BROWN:
      brown = fmaf(BROWN_LEAK, brown, BROWN_STEP * w);
      return brown;
    case MODE_BLUE: {
      float raw = (w - lastWhite) * BLUE_NORM;
      lastWhite = w;
      blueLP = fmaf(BLUE_LP_A, blueLP, BLUE_LP_B * raw);
      return blueLP;
    }
    default: return w;
  }
}

inline float dcBlock(float x) {
  float y = x - dc_prevX + DC_BLOCK_R * dc_prevY;
  dc_prevX = x;
  dc_prevY = y;
  return y;
}

inline float applySlewedGain(float s) {
  currentGain += (targetGain - currentGain) * SLEW_RATE;
  return s * currentGain;
}

void audioSetup() {
  i2s_config_t config = {
    .mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = I2S_SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S_MSB,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = I2S_BUFFER_SAMPLES,
    .use_apll = true,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pins = {
    .bck_io_num = I2S_BCK_PIN,
    .ws_io_num = I2S_WS_PIN,
    .data_out_num = I2S_DATA_OUT_PIN,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  i2s_driver_install(I2S_NUM_0, &config, 0, nullptr);
  i2s_set_pin(I2S_NUM_0, &pins);
  i2s_zero_dma_buffer(I2S_NUM_0);
}

void fillAudioBuffer() {
  for (int i = 0; i < I2S_BUFFER_SAMPLES; i++) {
    float s = generateNoiseSample();
    s = dcBlock(s);

    if (g_muted) {
      s = 0.0f;
    } else {
      s = applySlewedGain(s);
      s = tanhf(s * LIMITER_DRIVE);
    }

    s = clamp(s, -1.0f, 1.0f);
    int16_t pcm = static_cast<int16_t>(s * 32767.0f);
    i2sBuffer[i] = (static_cast<int32_t>(pcm) << 16);
  }
}

void writeAudioBuffer() {
  size_t written;
  i2s_write(I2S_NUM_0, i2sBuffer, sizeof(i2sBuffer), &written, portMAX_DELAY);
}
