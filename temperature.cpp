#include "temperature.h"

#include <OneWire.h>
#include <DallasTemperature.h>

#include "config.h"
#include "wifi_mqtt.h"

#if DS18B20_ENABLED
namespace {
  OneWire oneWire(DS18B20_PIN);
  DallasTemperature sensors(&oneWire);
  unsigned long lastTemperatureRead = 0;
  float lastTemperatureC = NAN;
}
#endif

void temperatureSetup() {
#if DS18B20_ENABLED
  sensors.begin();
  Serial.printf("[TEMP] DS18B20 enabled on GPIO %d; devices=%d\n",
                DS18B20_PIN, sensors.getDeviceCount());
#endif
}

void temperaturePublish() {
#if DS18B20_ENABLED
  if (!mqttIsConnected()) return;

  sensors.requestTemperatures();
  const float c = sensors.getTempCByIndex(0);

  if (c == DEVICE_DISCONNECTED_C) {
    Serial.println("[TEMP] DS18B20 disconnected");
    return;
  }

  lastTemperatureC = c;

  String base = gConfig.mqttBase;
  if (!base.endsWith("/")) base += "/";

  mqttPublishRaw((base + "temperature").c_str(), String(c, 2).c_str(), true);
  Serial.printf("[TEMP] %.2f C published\n", c);
#endif
}

void temperatureLoop() {
#if DS18B20_ENABLED
  const unsigned long now = millis();
  if (now - lastTemperatureRead >= DS18B20_INTERVAL_MS) {
    lastTemperatureRead = now;
    temperaturePublish();
  }
#endif
}
