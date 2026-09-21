#pragma once
#include "config.h"

void ledsSetup();
void showModeColor(NoiseMode mode);
void showMute();
void showSetupMode();
void updateVolumeLEDs(float ui);
void handleModeFlash();
extern bool modeFlashActive;
extern uint32_t modeFlashUntil;
