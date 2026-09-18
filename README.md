ESP32 MQTT Noise Generator
==========================

A high-quality ESP32-based broadband noise generator with rotary encoder control, LED volume bar, multiple noise modes, MQTT integration, WiFiManager configuration portal, and persistent NVS configuration for WiFi and MQTT. Designed for stable 24/7 operation with clean boot behavior, deterministic LED/UI state, and safe non-blocking MQTT operation.

Features
--------
- White, Pink, Brown, and Blue noise generation
- Rotary encoder with detents for precise gain control
- LED volume bar with mode-color overlay
- Mute function with LED indication
- WiFiManager configuration portal (long-press button)
- Unified WiFi + MQTT configuration stored in NVS
- Automatic WiFi reconnect and MQTT reconnect
- MQTT control and state reporting
- Heartbeat topic for device monitoring
- Home Assistant MQTT Discovery with per-device unique IDs
- Optional DS18B20 temperature sensor with MQTT + Home Assistant + Node-RED support
- Fully non-blocking audio, UI, WiFi, and MQTT loops

Hardware Requirements
---------------------
- ESP32 DevKitC or equivalent module
- MAX98357A I2S amplifier
- Rotary encoder with push button
- WS2812B LED bar (4 LEDs)
- Momentary push button for configuration mode
- 5V power supply

Button Functions
----------------
Short press: cycle noise mode
Long press: toggle mute
Extra-long press (3+ seconds): start WiFiManager AP mode

LED Behavior
------------
Volume bar: LED brightness proportional to gain
Mode flash: brief color flash when mode changes
Mute: all LEDs dim red
Boot restore: LEDs restored to last known state

MQTT Topics
-----------
Base topic example:
bedroom/noise/

State topics (published by device):
bedroom/noise/gain
bedroom/noise/mode
bedroom/noise/mute
bedroom/noise/heartbeat

Command topics (subscribed by device):
bedroom/noise/gain/set
bedroom/noise/mode/set
bedroom/noise/mute/set

Payload Examples
----------------
Gain:
12

Mode:
pink

Mute:
ON

Node-RED Temperature
-------------------
Import `Node-Red/Noise-Generator-Temperature.json` into the existing Noise Generator flow/tab. It listens to `<mqttBase>/temperature` and displays °C and °F in the existing Dashboard status group. If your base topic differs from `bedroom/noise`, change the MQTT input topic.

Node-RED Examples
-----------------
Set mode to pink:
topic: bedroom/noise/mode/set
payload: pink

Set gain to 12:
topic: bedroom/noise/gain/set
payload: 12

Mute:
topic: bedroom/noise/mute/set
payload: ON

WiFi + MQTT Configuration
-------------------------
Configuration is stored in NVS under the app_cfg namespace:
WiFi SSID
WiFi password
MQTT server
MQTT port
MQTT username
MQTT password
MQTT base topic
MQTT client ID

To enter configuration mode:
Hold the button for 3+ seconds
ESP32 starts AP mode with SSID ESP32-Noise
Connect and open http://192.168.4.1
Enter WiFi and MQTT settings
Device saves configuration and reboots

Boot Behavior
-------------
1. Load configuration from NVS
2. Restore LED/UI state
3. Begin WiFi connection
4. Begin MQTT connection
5. Start audio engine
6. Enter main loop

Home Assistant
---------------
HA integration files are in the `HA/` directory:
- `noise_generator_package.yaml`: mode helper and IKEA E1810 automation
- `dashboard_noise_generator.yaml`: Lovelace dashboard card

The firmware publishes MQTT Discovery automatically. The MQTT client ID (`mqttId`) is used as the Home Assistant device ID, so each generator should have a unique ID such as `noisegen-bedroom`, `noisegen-office`, or `noisegen-nursery`.

For multiple generators, give each device its own MQTT base topic, for example:
- `bedroom/noise`
- `office/noise`
- `nursery/noise`

Also give each associated IKEA E1810 remote a unique Zigbee2MQTT friendly name and create one automation per generator. Do not use the same MQTT command topics for multiple generators unless you intentionally want them to operate together.

Optional DS18B20
----------------
The optional sensor is disabled by default in `temperature.h`:
`DS18B20_ENABLED = false`.
Set it to `true`, connect the DS18B20 data line to `DS18B20_PIN` (default GPIO 4), and use a 4.7 kOhm pull-up from data to 3.3 V. Install the locked OneWire and DallasTemperature libraries. When enabled, the firmware publishes temperature in °C to `<mqttBase>/temperature` and advertises it through Home Assistant MQTT Discovery. Node-RED support is provided by `Node-Red/Noise-Generator-Temperature.json`.

Heartbeat
---------
Device publishes a heartbeat every 90 seconds to:
bedroom/noise/heartbeat
Payload: 1

File Overview
-------------
wifi_mqtt.cpp: WiFiManager portal, unified NVS config, WiFi reconnect, MQTT reconnect, MQTT callback, LED restore
wifi_mqtt.h: Configuration struct, function declarations
encoder.cpp: Rotary encoder handling, gain updates, MQTT publish flag
button.cpp: Button state machine, short/long/extra-long press, config portal trigger
leds.cpp: Volume bar, mode flash, mute indication, boot restore
audio_engine.cpp: Noise generation, I2S output, buffer fill/write
ESP32_MQTTNoiseGenerator.ino: Main setup/loop, initialization, integration

Known Good Behavior
-------------------
WiFi and MQTT reconnect automatically
LED state is correct after reboot
Mode changes from MQTT flash briefly, then restore volume bar
No blocking operations in audio loop
No recursive MQTT publishes
No reliance on WiFiManager’s internal WiFi NVS

License
-------
MIT License
