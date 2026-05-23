#pragma once
#include "config.h"

void audioSetup();
void fillAudioBuffer();
void writeAudioBuffer();
extern volatile NoiseMode g_noiseMode;
extern bool g_muted;
extern int32_t g_detentCount;
float dbToLin(float db);
extern float targetGain;
extern float currentGain;

float dbToLin(float db);

