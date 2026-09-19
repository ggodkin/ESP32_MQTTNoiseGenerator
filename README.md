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
- Optional DS18B20 temperature sensor with NVS-configurable enable/disable state and MQTT + Home Assistant + Node-RED support
- Fully non-blocking audio, UI, WiFi, and MQTT loops

Hardware Requirements
---------------------
- ESP32 DevKitC or equivalent module
- MAX98357A I2S amplifier
- Rotary encoder with push button
- WS2812B LED bar (8 LEDs)
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
1. Initialize encoder, button, LEDs, and audio engine
2. Initialize the optional DS18B20 sensor
3. Restore configuration from NVS when available; otherwise use built-in defaults
4. Restore the local LED/UI state
5. Start WiFi/MQTT as an optional background service when credentials are configured
6. Enter the main loop

Offline / Local Operation
-------------------------
The noise generator does not require WiFi, MQTT, or NVS to operate locally. Audio generation, the rotary encoder, the push button, mute, mode selection, and LEDs are initialized before any network service is started.

If NVS is missing or unavailable, the firmware uses built-in defaults and continues running. If no WiFi SSID is configured, the firmware does not start WiFi or repeatedly attempt reconnects. If WiFi or MQTT becomes unavailable after startup, the device continues operating locally while the network services retry in the background.

GPIO23 / D23 is intentionally unused and reserved for the PCB layout. Do not assign GPIO23 to any firmware feature.

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
The DS18B20 driver is compiled in, but the sensor is disabled by default. Its enable/disable state is stored in NVS and can be changed from the WiFiManager configuration portal using **Enable DS18B20 temperature (T/F)**. The data line remains on `DS18B20_PIN` (default GPIO 4), with a 4.7 kOhm pull-up from data to 3.3 V. Install the locked OneWire and DallasTemperature libraries. When enabled, the firmware publishes temperature in °C to `<mqttBase>/temperature` and advertises it through Home Assistant MQTT Discovery. Node-RED support is provided by `Node-Red/Noise-Generator-Temperature.json`.

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
