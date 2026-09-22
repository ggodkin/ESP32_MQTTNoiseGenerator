#include "temperature.h"

#if DS18B20_ENABLED
#include <OneWire.h>
#include <DallasTemperature.h>
#endif

#include "config.h"
#include "wifi_mqtt.h"

#if DS18B20_ENABLED
namespace {
OneWire oneWire(DS18B20_PIN);
DallasTemperature sensors(&oneWire);
unsigned long lastRead = 0;
bool conversionPending = false;
}
#endif

void temperatureSetup() {
#if DS18B20_ENABLED
  sensors.begin();
  sensors.setWaitForConversion(false);
  Serial.printf("[TEMP] DS18B20 driver ready on GPIO %d; enabled=%s; devices=%d\n",
                DS18B20_PIN, gConfig.tempEnabled ? "yes" : "no", sensors.getDeviceCount());
#endif
}

void temperaturePublish() {
#if DS18B20_ENABLED
  if (!gConfig.tempEnabled || !mqttIsConnected() || sensors.getDeviceCount() < 1) return;
  sensors.requestTemperatures();
  lastRead = millis();
  conversionPending = true;
#endif
}

void temperatureLoop() {
#if DS18B20_ENABLED
  if (!gConfig.tempEnabled || !mqttIsConnected()) {
    conversionPending = false;
    return;
  }

  const unsigned long now = millis();

  if (conversionPending && now - lastRead >= 800) {
    const float c = sensors.getTempCByIndex(0);
    conversionPending = false;

    if (c == DEVICE_DISCONNECTED_C || c < -55.0f || c > 125.0f) {
      Serial.println("[TEMP] Invalid/disconnected DS18B20 reading");
      return;
    }

    String base = gConfig.deviceName + "/noise/";
    mqttPublishRaw((base + "temperature").c_str(), String(c, 2).c_str(), true);
    Serial.printf("[TEMP] %.2f C published\n", c);
  }

  if (!conversionPending && now - lastRead >= ((unsigned long)gConfig.tempIntervalSec * 1000UL)) {
    temperaturePublish();
  }
#endif
}
