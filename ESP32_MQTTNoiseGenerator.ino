#include <Arduino.h>
#include "wifi_mqtt.h"
#include "encoder.h"
#include "button.h"
#include "leds.h"
#include "audio_engine.h"
#include "temperature.h"

// -----------------------------------------------------------------------------
// Startup order is intentional:
//
//   1. Local controls and LEDs
//   2. Audio engine
//   3. Optional sensor
//   4. Wi-Fi/MQTT services
//
// The noise generator must remain fully usable when Wi-Fi, MQTT, or NVS is
// unavailable. Networking is a background service and is never required for
// local operation.
// -----------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(300);

  // Local hardware must be ready before any network work.
  encoderSetup();
  buttonSetup();
  ledsSetup();
  audioSetup();
  temperatureSetup();

  // Start with the default local UI immediately.
  updateVolumeLEDs(0.0f);

  // Start networking only after all local functionality is initialized.
  wifiMqttSetup();
}

void loop() {
  // Local operation always runs, regardless of Wi-Fi/MQTT/NVS state.
  handleButton();
  handleGain();
  handleModeFlash();

  fillAudioBuffer();
  writeAudioBuffer();

  // Network and optional telemetry are background services.
  wifiMqttLoop();
  temperatureLoop();
}
