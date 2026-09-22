#include "wifi_mqtt.h"

#include <WiFi.h>
#include <WiFiClient.h>
#include <PubSubClient.h>
#include <WiFiManager.h>
#include <Preferences.h>
#include <time.h>

#include "config.h"
#include "audio_engine.h"
#include "leds.h"
#include "temperature.h"

AppConfig gConfig;
Preferences cfgPrefs;
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
WiFiManager wm;

// PubSubClient defaults to a small packet buffer. Home Assistant discovery
// payloads are larger than the default when using configurable device IDs and
// MQTT base topics, so size the buffer explicitly.
constexpr uint16_t MQTT_PACKET_BUFFER_SIZE = 768;

void saveConfigToNvs();

// WiFiManager custom parameter pointers. They remain valid while the
// blocking configuration portal is active and allow the Save action to
// persist custom parameters immediately.
namespace {
WiFiManagerParameter* pMqttServer = nullptr;
WiFiManagerParameter* pMqttPort = nullptr;
WiFiManagerParameter* pMqttUser = nullptr;
WiFiManagerParameter* pMqttPass = nullptr;
WiFiManagerParameter* pDeviceName = nullptr;
WiFiManagerParameter* pTempEnabled = nullptr;
WiFiManagerParameter* pTempInterval = nullptr;

void savePortalParameters() {
  if (!pMqttServer || !pMqttPort || !pMqttUser || !pMqttPass ||
      !pDeviceName || !pTempEnabled || !pTempInterval) return;
  gConfig.mqttServer = pMqttServer->getValue();
  gConfig.mqttPort = pMqttPort->getValue();
  gConfig.mqttUser = pMqttUser->getValue();
  gConfig.mqttPass = pMqttPass->getValue();
  gConfig.deviceName = pDeviceName->getValue();
  gConfig.deviceName.trim();
  gConfig.deviceName.replace("/", "_");
  if (!gConfig.deviceName.length()) gConfig.deviceName = "noisegen";
  String tempValue = pTempEnabled->getValue();
  tempValue.trim();
  gConfig.tempEnabled = tempValue.equalsIgnoreCase("T") || tempValue == "1" || tempValue.equalsIgnoreCase("true");
  uint32_t interval = String(pTempInterval->getValue()).toInt();
  if (interval < 1) interval = 30;
  if (interval > 3600) interval = 3600;
  gConfig.tempIntervalSec = interval;
  saveConfigToNvs();
}
}

bool mqttReady = false;
bool wifiTried = false;
unsigned long lastWifiAttempt = 0;
const unsigned long WIFI_RETRY_INTERVAL = 15000;

unsigned long lastMqttReconnectAttempt = 0;
unsigned long lastMqttHeartbeat = 0;
const unsigned long MQTT_HEARTBEAT_INTERVAL = 90000;

bool mqttNeedsPublish = false;
bool ntpSyncStarted = false;

namespace {
void setDefaultConfig() {
  gConfig.wifiSsid   = "";
  gConfig.wifiPass   = "";
  gConfig.mqttServer = "";
  gConfig.mqttPort   = "1883";
  gConfig.mqttUser   = "";
  gConfig.mqttPass   = "";
  gConfig.deviceName = "noisegen";
  gConfig.tempEnabled = false;
  gConfig.tempIntervalSec = 30;
}
}

// g_detentCount is declared in audio_engine.h
extern volatile NoiseMode g_noiseMode;
extern bool g_muted;

void mqttPublishDiscovery() {
  if (!mqtt.connected()) {
    Serial.println("[DISCOVERY] MQTT not connected, skipping");
    return;
  }

  String base = (gConfig.deviceName + "/noise");
  if (base.endsWith("/")) base.remove(base.length() - 1);

  String deviceId = "noisegen-" + gConfig.deviceName;
  String dev = "\"dev\":{\"ids\":[\"" + deviceId +
               "\"],\"name\":\"NoiseGen - " + deviceId + "\"}";
  String gainId = deviceId + "_gain";
  String modeId = deviceId + "_mode";
  String muteId = deviceId + "_mute";
  String onlineId = deviceId + "_online";
  String tempUpdatedId = deviceId + "_temperature_last_update";

  // Arduino String concatenation returns StringSumHelper for some expressions.
  // Accept String here so discovery topics compile correctly.
  auto dbgPub = [&](const String& topic, const String& payload) {
    bool ok = mqtt.publish(topic.c_str(), payload.c_str(), true);
    Serial.println("--------------------------------------------------");
    Serial.printf("[DISCOVERY] PUBLISH\n  topic:   %s\n  result:  %s\n  payload:\n%s\n",
                  topic.c_str(), ok ? "OK" : "FAIL", payload.c_str());
  };

  dbgPub(
    "homeassistant/number/" + gainId + "/config",
    "{\"name\":\"Gain\","
    "\"uniq_id\":\"" + gainId + "\","
    "\"~\":\"" + base + "\","
    "\"stat_t\":\"~/gain\","
    "\"cmd_t\":\"~/gain/set\","
    "\"min\":0,"
    "\"max\":20,"
    + dev + "}"
  );

  dbgPub(
    "homeassistant/select/" + modeId + "/config",
    "{\"name\":\"Mode\","
    "\"uniq_id\":\"" + modeId + "\","
    "\"~\":\"" + base + "\","
    "\"stat_t\":\"~/mode\","
    "\"cmd_t\":\"~/mode/set\","
    "\"options\":[\"White\",\"Pink\",\"Brown\",\"Blue\"],"
    + dev + "}"
  );

  dbgPub(
    "homeassistant/switch/" + muteId + "/config",
    "{\"name\":\"Mute\","
    "\"uniq_id\":\"" + muteId + "\","
    "\"~\":\"" + base + "\","
    "\"stat_t\":\"~/mute\","
    "\"cmd_t\":\"~/mute/set\","
    "\"pl_on\":\"ON\","
    "\"pl_off\":\"OFF\","
    + dev + "}"
  );

#if DS18B20_ENABLED
  if (gConfig.tempEnabled) {
    dbgPub(
    "homeassistant/sensor/" + deviceId + "_temperature/config",
    "{\"name\":\"Temperature\","
    "\"uniq_id\":\"" + deviceId + "_temperature\","
    "\"~\":\"" + base + "\","
    "\"stat_t\":\"~/temperature\","
    "\"unit_of_meas\":\"°C\","
    "\"dev_cla\":\"temperature\","
    "\"state_class\":\"measurement\","
    + dev + "}"
    );
  }
#endif

#if DS18B20_ENABLED
  if (gConfig.tempEnabled) {
    String tempUpdatedPayload =
      "{\"name\":\"Temperature Last Update\","
      "\"uniq_id\":\"" + tempUpdatedId + "\","
      "\"~\":\"" + base + "\","
      "\"stat_t\":\"~/temperature_last_update\","
      "\"device_class\":\"timestamp\","
      "\"entity_category\":\"diagnostic\","
      "\"enabled_by_default\":true,"
      + dev + "}";
    Serial.printf("[DISCOVERY] Temperature Last Update config topic: homeassistant/sensor/%s/config\\n", tempUpdatedId.c_str());
    dbgPub(
      "homeassistant/sensor/" + tempUpdatedId + "/config",
      tempUpdatedPayload
    );
  }
#endif

  dbgPub(
    "homeassistant/binary_sensor/" + onlineId + "/config",
    "{\"name\":\"Online\","
    "\"uniq_id\":\"" + onlineId + "\","
    "\"~\":\"" + base + "\","
    "\"stat_t\":\"~/online\","
    "\"pl_on\":\"1\","
    "\"pl_off\":\"0\","
    "\"avty_t\":\"~/online\","
    "\"pl_avail\":\"1\","
    "\"pl_not_avail\":\"0\","
    + dev + "}"
  );

  Serial.println("[DISCOVERY] All discovery messages attempted");
}

void loadConfigFromNvs() {
  setDefaultConfig();
  if (!cfgPrefs.begin("app_cfg", true)) {
    Serial.println("[CFG] NVS unavailable; using built-in defaults");
    return;
  }
  gConfig.wifiSsid = cfgPrefs.getString("wifiSsid", gConfig.wifiSsid);
  gConfig.wifiPass = cfgPrefs.getString("wifiPass", gConfig.wifiPass);
  gConfig.mqttServer = cfgPrefs.getString("mqttServer", gConfig.mqttServer);
  gConfig.mqttPort = cfgPrefs.getString("mqttPort", gConfig.mqttPort);
  gConfig.mqttUser = cfgPrefs.getString("mqttUser", gConfig.mqttUser);
  gConfig.mqttPass = cfgPrefs.getString("mqttPass", gConfig.mqttPass);
  if (cfgPrefs.isKey("deviceName")) {
    gConfig.deviceName = cfgPrefs.getString("deviceName", gConfig.deviceName);
  } else {
    String oldBase = cfgPrefs.getString("mqttBase", "");
    String oldId = cfgPrefs.getString("mqttId", "");
    if (oldBase.length()) {
      int slash = oldBase.indexOf('/');
      gConfig.deviceName = slash > 0 ? oldBase.substring(0, slash) : oldBase;
    } else if (oldId.startsWith("noisegen-")) {
      gConfig.deviceName = oldId.substring(9);
    } else if (oldId.length()) {
      gConfig.deviceName = oldId;
    }
  }
  if (cfgPrefs.isKey("tempEnabled")) gConfig.tempEnabled = cfgPrefs.getBool("tempEnabled", gConfig.tempEnabled);
  if (cfgPrefs.isKey("tempInterval")) gConfig.tempIntervalSec = cfgPrefs.getUInt("tempInterval", gConfig.tempIntervalSec);
  gConfig.deviceName.trim();
  gConfig.deviceName.replace("/", "_");
  if (!gConfig.deviceName.length()) gConfig.deviceName = "noisegen";
  if (gConfig.tempIntervalSec < 1 || gConfig.tempIntervalSec > 3600) gConfig.tempIntervalSec = 30;
  cfgPrefs.end();
  Serial.printf("[CFG] Loaded deviceName=%s tempEnabled=%d tempInterval=%lu\n",
                gConfig.deviceName.c_str(), gConfig.tempEnabled ? 1 : 0,
                (unsigned long)gConfig.tempIntervalSec);
  Serial.println("[CFG] Configuration loaded from NVS");
}

void saveConfigToNvs() {
  if (!cfgPrefs.begin("app_cfg", false)) {
    Serial.println("[CFG] NVS unavailable; configuration not saved");
    return;
  }
  cfgPrefs.putString("wifiSsid", gConfig.wifiSsid.substring(0, 31));
  cfgPrefs.putString("wifiPass", gConfig.wifiPass.substring(0, 63));
  cfgPrefs.putString("mqttServer", gConfig.mqttServer.substring(0, 31));
  cfgPrefs.putString("mqttPort", gConfig.mqttPort.substring(0, 5));
  cfgPrefs.putString("mqttUser", gConfig.mqttUser.substring(0, 31));
  cfgPrefs.putString("mqttPass", gConfig.mqttPass.substring(0, 63));
  cfgPrefs.putString("deviceName", gConfig.deviceName.substring(0, 31));
  cfgPrefs.remove("mqttBase");
  cfgPrefs.remove("mqttId");
  cfgPrefs.remove("haUniqueId");
  cfgPrefs.putBool("tempEnabled", gConfig.tempEnabled);
  cfgPrefs.putUInt("tempInterval", gConfig.tempIntervalSec);
  cfgPrefs.end();
  Serial.println("[CFG] Configuration saved to NVS");
}

void mqttPublishState() {
  if (!mqtt.connected()) {
    Serial.println("[STATE] MQTT not connected, skipping");
    return;
  }

  String base = (gConfig.deviceName + "/noise");
  if (!base.endsWith("/")) base += "/";

  Serial.printf("[STATE] Publishing gain=%d mode=%d mute=%d\n",
                g_detentCount, (int)g_noiseMode, g_muted);

  mqtt.publish((base + "gain").c_str(), String(g_detentCount).c_str(), true);

  const char* modeName =
    (g_noiseMode == MODE_WHITE) ? "White" :
    (g_noiseMode == MODE_PINK)  ? "Pink"  :
    (g_noiseMode == MODE_BROWN) ? "Brown" :
                                   "Blue";
  mqtt.publish((base + "mode").c_str(), modeName, true);

  mqtt.publish((base + "mute").c_str(), g_muted ? "ON" : "OFF", true);
}

bool mqttIsConnected() { return mqtt.connected(); }

bool mqttPublishRaw(const char* topic, const char* payload, bool retained) {
  if (!mqtt.connected()) return false;
  return mqtt.publish(topic, payload, retained);
}

void mqttHeartbeat() {
  if (!mqtt.connected()) return;

  unsigned long now = millis();
  if (now - lastMqttHeartbeat >= MQTT_HEARTBEAT_INTERVAL) {
    lastMqttHeartbeat = now;
    String base = (gConfig.deviceName + "/noise");
    if (!base.endsWith("/")) base += "/";
    mqtt.publish((base + "heartbeat").c_str(), "1", false);
    Serial.println("[MQTT] Heartbeat sent");
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String t = String(topic);
  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];

  Serial.printf("[MQTT] RX topic=%s payload=%s\n", t.c_str(), msg.c_str());

  String base = (gConfig.deviceName + "/noise");
  if (!base.endsWith("/")) base += "/";

  bool changed = false;

  if (t == base + "gain/set") {
    g_detentCount = constrain(msg.toInt(), ENC_MIN, ENC_MAX);
    changed = true;
    float ui = fminf(fmaxf(
      (float)(g_detentCount - ENC_MIN) / (float)(ENC_MAX - ENC_MIN),
      0.0f), 1.0f);
    updateVolumeLEDs(ui);
  }
  else if (t == base + "mode/set") {
    int m = -1;
    if (msg == "0" || msg == "1" || msg == "2" || msg == "3") m = msg.toInt();
    else if (msg.equalsIgnoreCase("white")) m = 0;
    else if (msg.equalsIgnoreCase("pink"))  m = 1;
    else if (msg.equalsIgnoreCase("brown")) m = 2;
    else if (msg.equalsIgnoreCase("blue"))  m = 3;

    if (m >= 0 && m < MODE_COUNT) {
      g_noiseMode = (NoiseMode)m;
      changed = true;
      if (!g_muted) {
        float ui = (float)(g_detentCount - ENC_MIN) / (float)(ENC_MAX - ENC_MIN);
        showModeColor(g_noiseMode);
        updateVolumeLEDs(ui);
      }
    }
  }
  else if (t == base + "mute/set") {
    bool newMute =
      msg.equalsIgnoreCase("ON") ||
      msg.equalsIgnoreCase("1") ||
      msg.equalsIgnoreCase("true");

    if (newMute != g_muted) {
      g_muted = newMute;
      changed = true;
      if (g_muted) {
        showMute();
      } else {
        float ui = (float)(g_detentCount - ENC_MIN) / (float)(ENC_MAX - ENC_MIN);
        showModeColor(g_noiseMode);
        updateVolumeLEDs(ui);
      }
    }
  }

  if (changed) {
    Serial.println("[MQTT] State changed -> scheduling publish");
    mqttNeedsPublish = true;
  }
}

bool mqttReconnect() {
  if (!gConfig.mqttServer.length()) {
    Serial.println("[MQTT] No server configured");
    return false;
  }

  Serial.printf("[MQTT] Attempting connect to %s:%d as '%s'\n",
                gConfig.mqttServer.c_str(),
                gConfig.mqttPort.toInt(),
                (String("noisegen-") + gConfig.deviceName).c_str());

  String onlineTopic = (gConfig.deviceName + "/noise");
  if (!onlineTopic.endsWith("/")) onlineTopic += "/";
  onlineTopic += "online";

  bool ok;
  if (gConfig.mqttUser.length() > 0) {
    ok = mqtt.connect(
      (String("noisegen-") + gConfig.deviceName).c_str(),
      gConfig.mqttUser.c_str(),
      gConfig.mqttPass.c_str(),
      onlineTopic.c_str(), 1, true, "0"
    );
  } else {
    ok = mqtt.connect(
      (String("noisegen-") + gConfig.deviceName).c_str(),
      onlineTopic.c_str(), 1, true, "0"
    );
  }

  Serial.printf("[MQTT] Connect result: %s\n", ok ? "SUCCESS" : "FAIL");

  if (ok) {
    String base = (gConfig.deviceName + "/noise");
    if (!base.endsWith("/")) base += "/";

    mqtt.subscribe((base + "gain/set").c_str());
    mqtt.subscribe((base + "mode/set").c_str());
    mqtt.subscribe((base + "mute/set").c_str());

    mqttPublishDiscovery();
    mqtt.publish(onlineTopic.c_str(), "1", true);
    mqttPublishState();
#if DS18B20_ENABLED
    temperaturePublish();
#endif
  }

  return ok;
}

void startConfigPortal() {
  Serial.println("[WIFI] Starting config portal...");
  showSetupMode();
  mqtt.setBufferSize(MQTT_PACKET_BUFFER_SIZE);
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP_STA);
  delay(200);
  loadConfigFromNvs();
  wm.setDebugOutput(true);
  wm.setConfigPortalBlocking(true);
  wm.setBreakAfterConfig(true);

  WiFiManagerParameter p_mqtt_server("mqtt_server", "MQTT Server", gConfig.mqttServer.c_str(), 40);
  WiFiManagerParameter p_mqtt_port("mqtt_port", "MQTT Port", gConfig.mqttPort.c_str(), 6);
  WiFiManagerParameter p_mqtt_user("mqtt_user", "MQTT Username", gConfig.mqttUser.c_str(), 32);
  WiFiManagerParameter p_mqtt_pass("mqtt_pass", "MQTT Password", gConfig.mqttPass.c_str(), 64, "input type=\"password\"");
  WiFiManagerParameter p_device_name("device_name", "Device Name", gConfig.deviceName.c_str(), 31);
  WiFiManagerParameter p_temp_enabled("temp_enabled", "Enable DS18B20 temperature (T/F)", gConfig.tempEnabled ? "T" : "F", 2);
  WiFiManagerParameter p_temp_interval("temp_interval", "Temperature update interval (seconds)", String(gConfig.tempIntervalSec).c_str(), 6);

  wm.addParameter(&p_mqtt_server);
  wm.addParameter(&p_mqtt_port);
  wm.addParameter(&p_mqtt_user);
  wm.addParameter(&p_mqtt_pass);
  wm.addParameter(&p_device_name);
  wm.addParameter(&p_temp_enabled);
  wm.addParameter(&p_temp_interval);

  pMqttServer=&p_mqtt_server; pMqttPort=&p_mqtt_port; pMqttUser=&p_mqtt_user; pMqttPass=&p_mqtt_pass;
  pDeviceName=&p_device_name; pTempEnabled=&p_temp_enabled; pTempInterval=&p_temp_interval;
  wm.setSaveParamsCallback(savePortalParameters);

  bool ok=wm.startConfigPortal("ESP32-Noise");
  Serial.printf("[WIFI] Portal finished, ok=%d\n",ok);
  if(ok){
    gConfig.wifiSsid=WiFi.SSID(); gConfig.wifiPass=WiFi.psk();
    gConfig.mqttServer=p_mqtt_server.getValue(); gConfig.mqttPort=p_mqtt_port.getValue();
    gConfig.mqttUser=p_mqtt_user.getValue(); gConfig.mqttPass=p_mqtt_pass.getValue();
    gConfig.deviceName=p_device_name.getValue(); gConfig.deviceName.trim(); gConfig.deviceName.replace("/","_");
    if(!gConfig.deviceName.length()) gConfig.deviceName="noisegen";
    String tv=p_temp_enabled.getValue(); tv.trim();
    gConfig.tempEnabled=tv.equalsIgnoreCase("T")||tv=="1"||tv.equalsIgnoreCase("true");
    uint32_t iv=String(p_temp_interval.getValue()).toInt();
    if(iv<1) iv=30;
    if(iv>3600) iv=3600;
    gConfig.tempIntervalSec=iv;
    saveConfigToNvs();
    mqtt.setServer(gConfig.mqttServer.c_str(),gConfig.mqttPort.toInt());
    mqtt.setCallback(mqttCallback); mqttReady=true;
  }
  wm.setDebugOutput(false);
}
void restoreUiState() {
  if (g_muted) {
    showMute();
  } else {
    // Restore the normal volume indication. updateVolumeLEDs() already
    // applies the current mode color, so do not call showModeColor() here;
    // that would light all LEDs regardless of the gain setting.
    float ui = fminf(fmaxf(
      (float)(g_detentCount - ENC_MIN) / (float)(ENC_MAX - ENC_MIN),
      0.0f), 1.0f);
    updateVolumeLEDs(ui);
  }
}

void wifiMqttSetup() {
  mqtt.setBufferSize(MQTT_PACKET_BUFFER_SIZE);
  loadConfigFromNvs();

  if (gConfig.wifiSsid.length() > 0) {
    Serial.printf("[WIFI] Boot: connecting to SSID '%s'\n", gConfig.wifiSsid.c_str());
    WiFi.mode(WIFI_STA);
    WiFi.begin(gConfig.wifiSsid.c_str(), gConfig.wifiPass.c_str());
    wifiTried = true;
    lastWifiAttempt = millis();
  }

  if (gConfig.mqttServer.length() > 0) {
    mqtt.setServer(gConfig.mqttServer.c_str(), gConfig.mqttPort.toInt());
    mqtt.setCallback(mqttCallback);
    mqttReady = true;
  }

  restoreUiState();
}

void wifiMqttLoop() {
  // No configured SSID is a valid offline-only configuration. Do not start
  // or repeatedly restart the Wi-Fi station in that case.
  if (gConfig.wifiSsid.length() == 0) {
    return;
  }

  wl_status_t st = WiFi.status();

  if (st != WL_CONNECTED) {
    unsigned long now = millis();
    if (!wifiTried || (now - lastWifiAttempt > WIFI_RETRY_INTERVAL)) {
      wifiTried = true;
      lastWifiAttempt = now;
      Serial.println("[WIFI] Attempting reconnect...");
      WiFi.mode(WIFI_STA);
      WiFi.begin(gConfig.wifiSsid.c_str(), gConfig.wifiPass.c_str());
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    // Start SNTP once Wi-Fi is available. Temperature timestamps are
    // published in UTC and therefore do not depend on local timezone settings.
    if (!ntpSyncStarted) {
      configTime(0, 0, "pool.ntp.org", "time.nist.gov");
      ntpSyncStarted = true;
      Serial.println("[TIME] NTP synchronization started");
    }
  }

  if (WiFi.status() == WL_CONNECTED && mqttReady) {
    if (!mqtt.connected()) {
      unsigned long now = millis();
      if (now - lastMqttReconnectAttempt > 5000) {
        lastMqttReconnectAttempt = now;
        Serial.println("[MQTT] Not connected -> reconnecting...");
        mqttReconnect();
      }
    } else {
      mqtt.loop();
      mqttHeartbeat();

      if (mqttNeedsPublish) {
        mqttNeedsPublish = false;
        mqttPublishState();
      }
    }
  }
}
