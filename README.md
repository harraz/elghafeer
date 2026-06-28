# Ghafeer Sensor Hub (ESP8266)

An ESP8266 (Thing/ESP-01S) firmware that controls a relay over MQTT and accepts runtime control commands.

## Hardware
- Board: ESP8266 (PlatformIO `env:thing`)
- Relay on `RELAY_PIN` (GPIO0/D3)
- Serial debug uses TX/GPIO1 only.

## MQTT
- Broker: set in `config/local/esp01_builtin_relay.json`
- Topics (built from `GHAFEER_NAME` + MAC):
  - Status: `home/<GHAFEER_NAME>/<MAC>/status`
  - Commands: `home/<GHAFEER_NAME>/<MAC>/cmd`
- Max packet size: 2048 bytes (`MQTT_MAX_PACKET_SIZE`) and client buffer set to 2048 for HELP payloads.

### Commands (publish to `.../cmd`)
- `REL_ON` / `REL_OFF` / `REL_STATUS`
- `RELAY_MAX_ON_DURATION:<ms>` — auto-off window within the internal relay duration limits
- `DEBUG:<true|false>`
- `GHAFEER_NAME:<name>` — updates MQTT topics dynamically
- `STATUS` — returns device status JSON
- `RESTART` / `REBOOT`
- `HELP` — returns available commands (JSON array)

### Relay timeout
When the relay is turned on, auto-off is enforced via `RELAY_MAX_ON_DURATION`.

`RESTART` / `REBOOT` responses are briefly serviced through MQTT before the device restarts so the command acknowledgement is less likely to be lost.

## Building & Uploading
1) Ensure PlatformIO is installed (`platformio run`, `platformio run -t upload`).
2) Wi-Fi credentials live in `src/secrets.h` (`WIFI_SSID`, `WIFI_PASSWORD`).
3) Copy `config/device_config.example.json` to `config/local/esp01_builtin_relay.json` and set the device name, broker host, broker port, and debug default for the board you are flashing.
4) The build generates `include/settings.h` automatically from `config/local/esp01_builtin_relay.json`.
5) Connect the ESP8266 (upload port is `/dev/ttyUSB0` by default in `platformio.ini`).

This branch uses `config/local/esp01_builtin_relay.json`, so it does not reuse the PIR-capable ESP-01 regular config.

## Files of interest
- `src/main.cpp` — setup, MQTT wiring, relay control, auto-off timer.
- `src/handlecmds.h` — command parsing, HELP payload, MQTT responses.
- `src/globals.h` — shared globals, MQTT packet size, bounds for durations/intervals.
- `config/board_id.txt` — branch-specific id used to select the ignored local config.
- `config/device_config.example.json` — template for the local JSON config used during build.
- `scripts/generate_settings_header.py` — generates `include/settings.h` from `config/local/<board_id>.json`.
- `platformio.ini` — PlatformIO environment configuration.
