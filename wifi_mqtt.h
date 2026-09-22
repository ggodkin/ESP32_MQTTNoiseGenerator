#pragma once

#include <Arduino.h>
#include "config.h"   // <-- this brings in NoiseMode

extern bool mqttNeedsPublish;

struct AppConfig {
  String wifiSsid;
  String wifiPass;
  String mqttServer;
  String mqttPort;
  String mqttUser;
  String mqttPass;
  String deviceName;
  bool tempEnabled;
  uint32_t tempIntervalSec;
};

extern AppConfig gConfig;

void wifiMqttSetup();
void wifiMqttLoop();
void startConfigPortal();
void mqttPublishState();
void mqttPublishDiscovery();
void restoreUiState();
bool mqttIsConnected();
bool mqttPublishRaw(const char* topic, const char* payload, bool retained);
