#include <Arduino.h>
#include "wifi_mqtt.h"
#include "encoder.h"
#include "button.h"
#include "leds.h"
#include "audio_engine.h"
#include "temperature.h"

// -----------------------------------------------------------------------------
// LOCAL-FIRST STARTUP
//
// Wi-Fi, MQTT, and NVS are background services. They must never be required
// for the noise generator, LEDs, encoder, or button to operate.
// -----------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(300);

  Serial.println("[BOOT] Initializing local controls");
  encoderSetup();
  buttonSetup();
  ledsSetup();

  Serial.println("[BOOT] Initializing audio");
  audioSetup();

  Serial.println("[BOOT] Temperature sensor configuration will be loaded from NVS");

  // Establish a visible local state before any network/NVS work.
  updateVolumeLEDs(0.0f);

  Serial.println("[BOOT] Local hardware initialization complete");
}

void loop() {
  // The first loop performs network initialization only after all local
  // hardware has already been initialized. It is intentionally deferred
  // from setup() to preserve responsive local startup.
  static bool networkInitDone = false;
  if (!networkInitDone) {
    Serial.println("[BOOT] Starting optional WiFi/MQTT services");
    wifiMqttSetup();
    temperatureSetup();
    networkInitDone = true;
    Serial.println("[BOOT] Optional WiFi/MQTT initialization complete");
  }

  // LOCAL CONTROL PATH -- never conditional on Wi-Fi/MQTT/NVS.
  handleButton();
  handleGain();
  handleModeFlash();

  fillAudioBuffer();
  writeAudioBuffer();

  // BACKGROUND SERVICES.
  wifiMqttLoop();
  temperatureLoop();
}
