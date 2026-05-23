#include "wifi_mqtt.h"

#include <WiFi.h>
#include <WiFiClient.h>
#include <PubSubClient.h>
#include <WiFiManager.h>
#include <Preferences.h>

#include "config.h"
#include "audio_engine.h"
#include "leds.h"

// ---------- globals ----------

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

// external state from other modules
extern int32_t g_detentCount;
extern volatile NoiseMode g_noiseMode;
extern bool g_muted;

// ---------------------------------------------------------------------------
// Home Assistant MQTT Discovery (compact dev + debug)
// ---------------------------------------------------------------------------

void mqttPublishDiscovery() {
  if (!mqtt.connected()) {
    Serial.println("[DISCOVERY] MQTT not connected, skipping");
    return;
  }

  String base = gConfig.mqttBase;
  if (base.endsWith("/")) base.remove(base.length() - 1);

  // Compact device block
  String dev = "\"dev\":{\"ids\":[\"noise_gen\"],\"name\":\"NoiseGen\"}";

  auto dbgPub = [&](const char* topic, const String& payload) {
    bool ok = mqtt.publish(topic, payload.c_str(), true);
    Serial.println("--------------------------------------------------");
    Serial.printf("[DISCOVERY] PUBLISH\n  topic:   %s\n  result:  %s\n  payload:\n%s\n",
                  topic,
                  ok ? "OK" : "FAIL",
                  payload.c_str());
  };

  // Gain
  dbgPub(
    "homeassistant/number/noise_gen_gain/config",
    "{\"name\":\"Gain\","
    "\"uniq_id\":\"noise_gen_gain\","
    "\"~\":\"" + base + "\","
    "\"stat_t\":\"~/gain\","
    "\"cmd_t\":\"~/gain/set\","
    "\"min\":0,"
    "\"max\":19,"
     + dev +
    "}"
  );

  // Mode
  // Mode (as sensor, not select)
  dbgPub(
    "homeassistant/sensor/noise_gen_mode/config",
    "{\"name\":\"Mode\","
    "\"uniq_id\":\"noise_gen_mode\","
    "\"~\":\"" + base + "\","
    "\"stat_t\":\"~/mode\","
    "\"dev_cla\":\"enum\","
    "\"options\":[\"0\",\"1\",\"2\",\"3\"],"
    + dev +
    "}"
  );

  // Mute
  dbgPub(
    "homeassistant/switch/noise_gen_mute/config",
    "{\"name\":\"Mute\","
    "\"uniq_id\":\"noise_gen_mute\","
    "\"~\":\"" + base + "\","
    "\"stat_t\":\"~/mute\","
    "\"cmd_t\":\"~/mute/set\","
    "\"pl_on\":\"ON\","
    "\"pl_off\":\"OFF\","
    + dev +
    "}"
  );

  // Online
  dbgPub(
    "homeassistant/binary_sensor/noise_gen_online/config",
    "{\"name\":\"Online\","
    "\"uniq_id\":\"noise_gen_online\","
    "\"~\":\"" + base + "\","
    "\"stat_t\":\"~/heartbeat\","
    "\"pl_on\":\"1\","
    "\"pl_off\":\"0\","
    + dev +
    "}"
  );

  Serial.println("[DISCOVERY] All discovery messages attempted");
}

// ---------------------------------------------------------------------------
// Config NVS helpers
// ---------------------------------------------------------------------------

void loadConfigFromNvs() {
  cfgPrefs.begin("app_cfg", true);
  gConfig.wifiSsid   = cfgPrefs.getString("wifiSsid", "");
  gConfig.wifiPass   = cfgPrefs.getString("wifiPass", "");
  gConfig.mqttServer = cfgPrefs.getString("mqttServer", "");
  gConfig.mqttPort   = cfgPrefs.getString("mqttPort", "1883");
  gConfig.mqttUser   = cfgPrefs.getString("mqttUser", "");
  gConfig.mqttPass   = cfgPrefs.getString("mqttPass", "");
  gConfig.mqttBase   = cfgPrefs.getString("mqttBase", "bedroom/noise");
  gConfig.mqttId     = cfgPrefs.getString("mqttId", "esp32-noise-1");
  cfgPrefs.end();

  Serial.println("[CFG] Loaded from NVS");
}

void saveConfigToNvs() {
  cfgPrefs.begin("app_cfg", false);

  cfgPrefs.putString("wifiSsid",   gConfig.wifiSsid.substring(0, 31));
  cfgPrefs.putString("wifiPass",   gConfig.wifiPass.substring(0, 63));
  cfgPrefs.putString("mqttServer", gConfig.mqttServer.substring(0, 31));
  cfgPrefs.putString("mqttPort",   gConfig.mqttPort.substring(0, 5));
  cfgPrefs.putString("mqttUser",   gConfig.mqttUser.substring(0, 31));
  cfgPrefs.putString("mqttPass",   gConfig.mqttPass.substring(0, 63));
  cfgPrefs.putString("mqttBase",   gConfig.mqttBase.substring(0, 31));
  cfgPrefs.putString("mqttId",     gConfig.mqttId.substring(0, 31));

  cfgPrefs.end();
  delay(200);

  Serial.println("[CFG] Saved to NVS");
}

// ---------------------------------------------------------------------------
// MQTT helpers
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// MQTT callback (LED behavior restored)
// ---------------------------------------------------------------------------

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String t = String(topic);
  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];

  Serial.printf("[MQTT] RX topic=%s payload=%s\n", t.c_str(), msg.c_str());

  String base = gConfig.mqttBase;
  if (!base.endsWith("/")) base += "/";

  bool changed = false;

  // ---- GAIN ----
  if (t == base + "gain/set") {
    g_detentCount = constrain(msg.toInt(), ENC_MIN, ENC_MAX);
    changed = true;

    float ui = (float)(g_detentCount - ENC_MIN) / (float)(ENC_MAX - ENC_MAX);
    updateVolumeLEDs(ui);
    if (!g_muted) showModeColor(g_noiseMode);
  }

  // ---- MODE ----
  else if (t == base + "mode/set") {
    int m = -1;
    if (msg == "0" || msg == "1" || msg == "2" || msg == "3") {
      m = msg.toInt();
    } else if (msg.equalsIgnoreCase("white")) m = 0;
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

  // ---- MUTE ----
  else if (t == base + "mute/set") {
    bool newMute =
      msg.equalsIgnoreCase("ON") ||
      msg.equalsIgnoreCase("1")  ||
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
    Serial.println("[MQTT] State changed → scheduling publish");
    mqttNeedsPublish = true;
  }
}

// ---------------------------------------------------------------------------
// MQTT reconnect
// ---------------------------------------------------------------------------

bool mqttReconnect() {
  if (!gConfig.mqttServer.length()) {
    Serial.println("[MQTT] No server configured");
    return false;
  }

  Serial.printf("[MQTT] Attempting connect to %s:%d as '%s'\n",
                gConfig.mqttServer.c_str(),
                gConfig.mqttPort.toInt(),
                gConfig.mqttId.c_str());

  bool ok;
  if (gConfig.mqttUser.length() > 0)
    ok = mqtt.connect(gConfig.mqttId.c_str(),
                      gConfig.mqttUser.c_str(),
                      gConfig.mqttPass.c_str());
  else
    ok = mqtt.connect(gConfig.mqttId.c_str());

  Serial.printf("[MQTT] Connect result: %s\n", ok ? "SUCCESS" : "FAIL");

  if (ok) {
    String base = gConfig.mqttBase;
    if (!base.endsWith("/")) base += "/";

    mqtt.subscribe((base + "gain/set").c_str());
    mqtt.subscribe((base + "mode/set").c_str());
    mqtt.subscribe((base + "mute/set").c_str());

    mqttPublishDiscovery();
    mqttPublishState();
  }

  return ok;
}

// ---------------------------------------------------------------------------
// WiFiManager config portal
// ---------------------------------------------------------------------------

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

  wm.addParameter(&p_mqtt_server);
  wm.addParameter(&p_mqtt_port);
  wm.addParameter(&p_mqtt_user);
  wm.addParameter(&p_mqtt_pass);
  wm.addParameter(&p_mqtt_base);

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

    gConfig.wifiSsid.trim();
    gConfig.wifiPass.trim();
    gConfig.mqttServer.trim();
    gConfig.mqttPort.trim();
    gConfig.mqttUser.trim();
    gConfig.mqttPass.trim();
    gConfig.mqttBase.trim();

    saveConfigToNvs();
    cfgPrefs.end();
    delay(200);

    Serial.println("[WIFI] Config saved");
    mqtt.setServer(gConfig.mqttServer.c_str(), gConfig.mqttPort.toInt());
    mqtt.setCallback(mqttCallback);
    mqttReady = true;
  }

  wm.setDebugOutput(false);
}

// ---------------------------------------------------------------------------
// Restore LED state
// ---------------------------------------------------------------------------

void restoreUiState() {
  if (g_muted) {
    showMute();
  } else {
    float ui = (float)(g_detentCount - ENC_MIN) / (float)(ENC_MAX - ENC_MIN);
    updateVolumeLEDs(ui);
    showModeColor(g_noiseMode);
  }
}

// ---------------------------------------------------------------------------
// Boot-time init
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Loop-time WiFi + MQTT
// ---------------------------------------------------------------------------

void wifiMqttLoop() {
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
        Serial.println("[MQTT] Not connected → reconnecting...");
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
