# Ghafeer Sensor Hub (ESP8266)

An ESP8266 (Thing/ESP-01S) firmware that controls a PIR motion sensor and a relay, reports events over MQTT, and accepts control commands.

## Hardware
- Board: ESP8266 (PlatformIO `env:thing`)
- PIR sensor on `PIR_PIN` (GPIO2/D4)
- Relay on `RELAY_PIN` (GPIO0/D3)

## MQTT
- Broker: set in `src/main.cpp` (`client.setServer("192.168.1.246", 1883)`)
- Topics (built from `GHAFEER_NAME` + MAC):
  - Status: `home/<GHAFEER_NAME>/<MAC>/status`
  - Motion: `home/<GHAFEER_NAME>/<MAC>/motion`
  - Commands: `home/<GHAFEER_NAME>/<MAC>/cmd`
- Max packet size: 2048 bytes (`MQTT_MAX_PACKET_SIZE`) and client buffer set to 2048 for HELP payloads.

### Commands (publish to `.../cmd`)
- `REL_ON` / `REL_OFF` / `REL_STATUS`
- `PIR_INTERVAL:<ms>` — set motion check interval (0..MAX_PIR_INTERVAL_MS)
- `SKIP_LOCAL_RELAY:<true|false>` — bypass local relay when motion detected
- `RELAY_MAX_ON_DURATION:<ms>` — auto-off window (MIN..MAX bounds)
- `DEBUG:<true|false>`
- `GHAFEER_NAME:<name>` — updates MQTT topics dynamically
- `STATUS` — returns device status JSON
- `RESTART` / `REBOOT`
- `HELP` — returns available commands (JSON array)

### Motion publishing
On motion, publishes JSON to `.../motion` and a status message indicating whether the relay was toggled (respecting `SKIP_LOCAL_RELAY`). Relay auto-off is enforced via `RELAY_MAX_ON_DURATION`.

## Building & Uploading
1) Ensure PlatformIO is installed (`platformio run`, `platformio run -t upload`).
2) Wi-Fi credentials live in `src/secrets.h` (`WIFI_SSID`, `WIFI_PASSWORD`).
3) Adjust pins or broker IP in `src/main.cpp`/`src/globals.h` as needed.
4) Connect the ESP8266 (upload port is `/dev/ttyUSB0` by default in `platformio.ini`).

## Files of interest
- `src/main.cpp` — setup, MQTT wiring, PIR/relay logic, auto-off timer.
- `src/handlecmds.h` — command parsing, HELP payload, MQTT responses.
- `src/globals.h` — shared globals, MQTT packet size, bounds for durations/intervals.
- `platformio.ini` — PlatformIO environment configuration.
