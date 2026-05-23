#include "button.h"
#include "config.h"
#include "audio_engine.h"
#include "leds.h"
#include "wifi_mqtt.h"

extern volatile NoiseMode g_noiseMode;
extern bool g_muted;
extern int32_t g_detentCount;

void buttonSetup() {
    pinMode(ENC_SW_PIN, INPUT_PULLUP);
}

void handleButton() {
    static bool lastStable = false;
    static bool lastRaw = false;
    static uint32_t lastChange = 0;
    static uint32_t pressTime = 0;

    bool raw = (digitalRead(ENC_SW_PIN) == LOW);
    uint32_t now = millis();

    if (raw != lastRaw) {
        lastRaw = raw;
        lastChange = now;
    }

    if ((now - lastChange) >= 5) {
        if (raw != lastStable) {
            lastStable = raw;

            if (raw) {
                pressTime = now;
            } else {
                uint32_t duration = now - pressTime;

                // ---------------- EXTRA LONG PRESS ----------------
                if (duration > 3000) {
                    Serial.println("[BUTTON] Extra long press → startConfigPortal()");
                    startConfigPortal();
                    return;
                }

                // ---------------- LONG PRESS (MUTE) ----------------
                if (duration > 600) {
                    Serial.println("[BUTTON] Long press → mute toggle");
                    g_muted = !g_muted;

                    if (g_muted) showMute();
                    else {
                        float ui = (float)(g_detentCount - ENC_MIN) /
                                   (float)(ENC_MAX - ENC_MIN);
                        updateVolumeLEDs(ui);
                    }

                    mqttNeedsPublish = true;
                    return;
                }

                // ---------------- SHORT PRESS (MODE) ----------------
                Serial.println("[BUTTON] Short press → mode change");

                g_noiseMode = static_cast<NoiseMode>((g_noiseMode + 1) % MODE_COUNT);
                showModeColor(g_noiseMode);

                modeFlashActive = true;
                modeFlashUntil = now + 150;

                mqttNeedsPublish = true;

            }
        }
    }
}
