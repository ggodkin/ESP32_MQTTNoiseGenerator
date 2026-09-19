#include "wifi_mqtt.h"

#include <WiFi.h>
#include <WiFiClient.h>
#include <PubSubClient.h>
#include <WiFiManager.h>
#include <Preferences.h>

#include "config.h"
#include "audio_engine.h"
#include "leds.h"
#include "temperature.h"

AppConfig gConfig;
Preferences cfgPrefs;
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
WiFiManager wm;

bool mqttReady = false;
bool wifiTried = false;
unsigned long lastWifiAttempt = 0;
const unsigned long WIFI_RETRY_INTERVAL = 15000;

unsigned long lastMqttReconnectAttempt = 0;
unsigned long lastMqttHeartbeat = 0;
const unsigned long MQTT_HEARTBEAT_INTERVAL = 90000;

bool mqttNeedsPublish = false;

namespace {
void setDefaultConfig() {
  gConfig.wifiSsid   = "";
  gConfig.wifiPass   = "";
  gConfig.mqttServer = "";
  gConfig.mqttPort   = "1883";
  gConfig.mqttUser   = "";
  gConfig.mqttPass   = "";
  gConfig.mqttBase   = "bedroom/noise";
  gConfig.mqttId     = "esp32-noise-1";
  gConfig.tempEnabled = false;
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

  String base = gConfig.mqttBase;
  if (base.endsWith("/")) base.remove(base.length() - 1);

  String deviceId = gConfig.mqttId.length() ? gConfig.mqttId : "esp32-noise-1";
  String dev = "\"dev\":{\"ids\":[\"" + deviceId +
               "\"],\"name\":\"NoiseGen - " + deviceId + "\"}";
  String gainId = deviceId + "_gain";
  String modeId = deviceId + "_mode";
  String muteId = deviceId + "_mute";
  String onlineId = deviceId + "_online";

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
    "homeassistant/sensor/" + modeId + "/config",
    "{\"name\":\"Mode\","
    "\"uniq_id\":\"" + modeId + "\","
    "\"~\":\"" + base + "\","
    "\"stat_t\":\"~/mode\","
    "\"dev_cla\":\"enum\","
    "\"options\":[\"0\",\"1\",\"2\",\"3\"],"
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

  gConfig.wifiSsid   = cfgPrefs.getString("wifiSsid", gConfig.wifiSsid);
  gConfig.wifiPass   = cfgPrefs.getString("wifiPass", gConfig.wifiPass);
  gConfig.mqttServer = cfgPrefs.getString("mqttServer", gConfig.mqttServer);
  gConfig.mqttPort   = cfgPrefs.getString("mqttPort", gConfig.mqttPort);
  gConfig.mqttUser   = cfgPrefs.getString("mqttUser", gConfig.mqttUser);
  gConfig.mqttPass   = cfgPrefs.getString("mqttPass", gConfig.mqttPass);
  gConfig.mqttBase   = cfgPrefs.getString("mqttBase", gConfig.mqttBase);
  gConfig.mqttId     = cfgPrefs.getString("mqttId", gConfig.mqttId);
  gConfig.tempEnabled = cfgPrefs.getBool("tempEnabled", gConfig.tempEnabled);
  cfgPrefs.end();

  Serial.println("[CFG] Configuration loaded from NVS");
}

void saveConfigToNvs() {
  if (!cfgPrefs.begin("app_cfg", false)) {
    Serial.println("[CFG] NVS unavailable; configuration not saved");
    return;
  }

  cfgPrefs.putString("wifiSsid",   gConfig.wifiSsid.substring(0, 31));
  cfgPrefs.putString("wifiPass",   gConfig.wifiPass.substring(0, 63));
  cfgPrefs.putString("mqttServer", gConfig.mqttServer.substring(0, 31));
  cfgPrefs.putString("mqttPort",   gConfig.mqttPort.substring(0, 5));
  cfgPrefs.putString("mqttUser",   gConfig.mqttUser.substring(0, 31));
  cfgPrefs.putString("mqttPass",   gConfig.mqttPass.substring(0, 63));
  cfgPrefs.putString("mqttBase",   gConfig.mqttBase.substring(0, 31));
  cfgPrefs.putString("mqttId",     gConfig.mqttId.substring(0, 31));
  cfgPrefs.putBool("tempEnabled",   gConfig.tempEnabled);
  cfgPrefs.end();

  Serial.println("[CFG] Configuration saved to NVS");
}

void mqttPublishState() {
  if (!mqtt.connected()) {
    Serial.println("[STATE] MQTT not connected, skipping");
    return;
  }

  String base = gConfig.mqttBase;
  if (!base.endsWith("/")) base += "/";

  Serial.printf("[STATE] Publishing gain=%d mode=%d mute=%d\n",
                g_detentCount, (int)g_noiseMode, g_muted);

  mqtt.publish((base + "gain").c_str(), String(g_detentCount).c_str(), true);
  mqtt.publish((base + "mode").c_str(), String((int)g_noiseMode).c_str(), true);
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
    String base = gConfig.mqttBase;
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

  String base = gConfig.mqttBase;
  if (!base.endsWith("/")) base += "/";

  bool changed = false;

  if (t == base + "gain/set") {
    g_detentCount = constrain(msg.toInt(), ENC_MIN, ENC_MAX);
    changed = true;
    float ui = (float)(g_detentCount - ENC_MIN) / (float)(ENC_MAX - ENC_MIN);
    updateVolumeLEDs(ui);
    if (!g_muted) showModeColor(g_noiseMode);
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
                gConfig.mqttId.c_str());

  String onlineTopic = gConfig.mqttBase;
  if (!onlineTopic.endsWith("/")) onlineTopic += "/";
  onlineTopic += "online";

  bool ok;
  if (gConfig.mqttUser.length() > 0) {
    ok = mqtt.connect(
      gConfig.mqttId.c_str(),
      gConfig.mqttUser.c_str(),
      gConfig.mqttPass.c_str(),
      onlineTopic.c_str(), 1, true, "0"
    );
  } else {
    ok = mqtt.connect(
      gConfig.mqttId.c_str(),
      onlineTopic.c_str(), 1, true, "0"
    );
  }

  Serial.printf("[MQTT] Connect result: %s\n", ok ? "SUCCESS" : "FAIL");

  if (ok) {
    String base = gConfig.mqttBase;
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
  WiFiManagerParameter p_mqtt_base("mqtt_base", "MQTT Base Topic", gConfig.mqttBase.c_str(), 40);
  WiFiManagerParameter p_temp_enabled("temp_enabled", "Enable DS18B20 temperature (T/F)", gConfig.tempEnabled ? "T" : "F", 2);

  wm.addParameter(&p_mqtt_server);
  wm.addParameter(&p_mqtt_port);
  wm.addParameter(&p_mqtt_user);
  wm.addParameter(&p_mqtt_pass);
  wm.addParameter(&p_mqtt_base);
  wm.addParameter(&p_temp_enabled);

  bool ok = wm.startConfigPortal("ESP32-Noise");
  Serial.printf("[WIFI] Portal finished, ok=%d\n", ok);

  if (ok) {
    gConfig.wifiSsid = WiFi.SSID();
    gConfig.wifiPass = WiFi.psk();

    gConfig.mqttServer = p_mqtt_server.getValue();
    gConfig.mqttPort   = p_mqtt_port.getValue();
    gConfig.mqttUser   = p_mqtt_user.getValue();
    gConfig.mqttPass   = p_mqtt_pass.getValue();
    gConfig.mqttBase   = p_mqtt_base.getValue();
    String tempValue = p_temp_enabled.getValue();
    tempValue.trim();
    gConfig.tempEnabled = tempValue.equalsIgnoreCase("T") || tempValue == "1" || tempValue.equalsIgnoreCase("true");

    gConfig.wifiSsid.trim();
    gConfig.wifiPass.trim();
    gConfig.mqttServer.trim();
    gConfig.mqttPort.trim();
    gConfig.mqttUser.trim();
    gConfig.mqttPass.trim();
    gConfig.mqttBase.trim();

    saveConfigToNvs();

    Serial.println("[WIFI] Config saved");
    mqtt.setServer(gConfig.mqttServer.c_str(), gConfig.mqttPort.toInt());
    mqtt.setCallback(mqttCallback);
    mqttReady = true;
  }

  wm.setDebugOutput(false);
}

void restoreUiState() {
  if (g_muted) {
    showMute();
  } else {
    float ui = (float)(g_detentCount - ENC_MIN) / (float)(ENC_MAX - ENC_MIN);
    updateVolumeLEDs(ui);
    showModeColor(g_noiseMode);
  }
}

void wifiMqttSetup() {
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
