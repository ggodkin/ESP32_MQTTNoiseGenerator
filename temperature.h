#pragma once

#include <Arduino.h>

// Set to true to enable an optional DS18B20 on the configured GPIO.
constexpr bool DS18B20_ENABLED = false;
constexpr int DS18B20_PIN = 4;
constexpr unsigned long DS18B20_INTERVAL_MS = 30000;

void temperatureSetup();
void temperatureLoop();
void temperaturePublish();
