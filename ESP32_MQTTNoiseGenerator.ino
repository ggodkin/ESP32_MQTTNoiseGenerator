#include <Arduino.h>
#include "wifi_mqtt.h"
#include "encoder.h"
#include "button.h"
#include "leds.h"
#include "audio_engine.h"

void setup() {
  Serial.begin(115200);
  delay(300);

  encoderSetup();
  buttonSetup();
  ledsSetup();
  audioSetup();

  updateVolumeLEDs(0.0f);
}

void loop() {
  static bool wifiInitDone = false;
  if (!wifiInitDone) {
    wifiMqttSetup();
    wifiInitDone = true;
  }

  handleButton();   // extra-long press must call startConfigPortal()
  handleGain();
  handleModeFlash();

  fillAudioBuffer();
  writeAudioBuffer();

  wifiMqttLoop();
}
