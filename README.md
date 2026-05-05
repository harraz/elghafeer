# Ghafeer Sensor Hub (ESP8266)

An ESP8266 (Thing/ESP-01S) firmware that controls a PIR motion sensor and a relay, reports events over MQTT, and accepts control commands.

## Hardware
- Board: ESP8266 (PlatformIO `env:thing`)
- PIR sensor on `PIR_PIN` (RX/GPIO3 on ESP-01S)
- Relay on `RELAY_PIN` (GPIO0/D3)
- Serial debug uses TX/GPIO1 only, leaving RX/GPIO3 available for the PIR input.

## MQTT
- Broker: set in `config/local/esp01_regular.json`
- Topics (built from `GHAFEER_NAME` + MAC):
  - Status: `home/<GHAFEER_NAME>/<MAC>/status`
  - Motion: `home/<GHAFEER_NAME>/<MAC>/motion`
  - Commands: `home/<GHAFEER_NAME>/<MAC>/cmd`
- Max packet size: 2048 bytes (`MQTT_MAX_PACKET_SIZE`) and client buffer set to 2048 for HELP payloads.

### Commands (publish to `.../cmd`)
- `REL_ON` / `REL_OFF` / `REL_STATUS`
- `PIR_INTERVAL:<ms>` — set the minimum gap between accepted motion events (0..MAX_PIR_INTERVAL_MS)
- `SKIP_LOCAL_RELAY:<true|false>` — bypass local relay when motion detected
- `RELAY_MAX_ON_DURATION:<ms>` — auto-off window within the internal relay duration limits
- `DEBUG:<true|false>`
- `GHAFEER_NAME:<name>` — updates MQTT topics dynamically
- `STATUS` — returns device status JSON
- `RESTART` / `REBOOT`
- `HELP` — returns available commands (JSON array)

### Motion publishing
Motion is detected on the PIR input's LOW-to-HIGH transition. On accepted motion, the device publishes one JSON event to `.../motion`. The event includes whether the local relay was activated, respecting `SKIP_LOCAL_RELAY`. Relay auto-off is enforced via `RELAY_MAX_ON_DURATION`.

`RESTART` / `REBOOT` responses are briefly serviced through MQTT before the device restarts so the command acknowledgement is less likely to be lost.

## Building & Uploading
1) Ensure PlatformIO is installed (`platformio run`, `platformio run -t upload`).
2) Wi-Fi credentials live in `src/secrets.h` (`WIFI_SSID`, `WIFI_PASSWORD`).
3) Copy `config/device_config.example.json` to `config/local/esp01_regular.json` and set the device name, broker host, broker port, and debug default for the board you are flashing.
4) The build generates `include/settings.h` automatically from `config/local/esp01_regular.json`.
5) Connect the ESP8266 (upload port is `/dev/ttyUSB0` by default in `platformio.ini`).

The ESP-01 regular branch uses `config/local/esp01_regular.json`, so switching
to or from deep-sleep branches does not reuse the wrong ignored local config.

## Files of interest
- `src/main.cpp` — setup, MQTT wiring, PIR/relay logic, auto-off timer.
- `src/handlecmds.h` — command parsing, HELP payload, MQTT responses.
- `src/globals.h` — shared globals, MQTT packet size, bounds for durations/intervals.
- `config/board_id.txt` — branch-specific id used to select the ignored local config.
- `config/device_config.example.json` — template for the local JSON config used during build.
- `scripts/generate_settings_header.py` — generates `include/settings.h` from `config/local/esp01_regular.json`.
- `platformio.ini` — PlatformIO environment configuration.
