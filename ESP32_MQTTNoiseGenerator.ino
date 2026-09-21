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
  // LOCAL CONTROL PATH ALWAYS RUNS FIRST.
  // Never allow network initialization or network processing to delay
  // button/encoder handling or audio generation.
  handleButton();
  handleGain();
  handleModeFlash();

  fillAudioBuffer();
  writeAudioBuffer();

  // Optional services are initialized only after local controls/audio have
  // received their turn. This prevents Wi-Fi startup from blocking the UI.
  static bool networkInitDone = false;
  if (!networkInitDone) {
    Serial.println("[BOOT] Starting optional WiFi/MQTT services");
    wifiMqttSetup();
    temperatureSetup();
    networkInitDone = true;
    Serial.println("[BOOT] Optional WiFi/MQTT initialization complete");
  }

  // BACKGROUND SERVICES.
  wifiMqttLoop();
  temperatureLoop();
}
