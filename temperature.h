#pragma once

#include <Arduino.h>

// DS18B20 support is compiled in; enable/disable is stored in NVS.
constexpr bool DS18B20_ENABLED = true;
constexpr int DS18B20_PIN = 4;
constexpr unsigned long DS18B20_DEFAULT_INTERVAL_MS = 30000;\nconstexpr unsigned long DS18B20_MIN_INTERVAL_MS = 1000;\nconstexpr unsigned long DS18B20_MAX_INTERVAL_MS = 3600000;

void temperatureSetup();
void temperatureLoop();
void temperaturePublish();
